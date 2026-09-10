/*
 * ha_history.c — fetches 7-day daily kWh via HA /api/history/period.
 *
 * For each day we request a 15-minute window ending at 23:59:59 local time
 * (or "now" for the current day), then take the last state value in that
 * window.  This gives end-of-day accumulated kWh for past days and current
 * progress for today.
 *
 * ha_history_fetch_combined() fetches all three entities (grid import,
 * grid export, solar) in a single pass and returns a combined ha_history_t
 * with both grid[] and solar[] arrays populated.  It is called from the
 * background ha_hist_task in main.c so the UI never waits for the 21 HTTP
 * calls (~20 s) — the chart appears instantly from the cached result.
 *
 * Data volume per request: ~200-500 bytes (15 min x ~1 update/min x 30 B).
 * The same 8 KB buffer is reused across all requests.
 */
#include "ha_history.h"
#include "ha_config.h"

#if defined(HAS_ENERGY)

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"

static const char *TAG = "ha_history";

#define HIST_BUF_CAP 8192

typedef struct { char *buf; int len; int cap; } resp_ctx_t;

static esp_err_t on_data(esp_http_client_event_t *evt)
{
    if (evt->event_id != HTTP_EVENT_ON_DATA) return ESP_OK;
    resp_ctx_t *c = (resp_ctx_t *)evt->user_data;
    int room = c->cap - 1 - c->len;
    int copy = (evt->data_len < room) ? evt->data_len : room;
    if (copy > 0) { memcpy(c->buf + c->len, evt->data, copy); c->len += copy; }
    return ESP_OK;
}

/*
 * Fetch the last observed state of entity_id in the window [win_start, win_end].
 *
 * GET /api/history/period/<start_utc>
 *       ?filter_entity_id=<entity>
 *       &end_time=<end_utc>
 *       &minimal_response=true
 *       &no_attributes=true
 *
 * Response: [[{first-entry-with-entity_id}, {state,last_changed}, ...]]
 * We parse the last element of the inner array and return its "state" as float.
 */
static esp_err_t fetch_entity_window_last(const char *entity_id,
                                           time_t win_start, time_t win_end,
                                           char *buf,
                                           float *out_val)
{
    char start_z[24], end_z[24];
    struct tm gm;
    gmtime_r(&win_start, &gm);
    strftime(start_z, sizeof(start_z), "%Y-%m-%dT%H:%M:%SZ", &gm);
    gmtime_r(&win_end, &gm);
    strftime(end_z,   sizeof(end_z),   "%Y-%m-%dT%H:%M:%SZ", &gm);

    char url[320];
    snprintf(url, sizeof(url),
             "http://%s:%d/api/history/period/%s"
             "?filter_entity_id=%s&end_time=%s"
             "&minimal_response=true&no_attributes=true",
             HA_HOST, HA_PORT, start_z, entity_id, end_z);

    char auth[300];
    snprintf(auth, sizeof(auth), "Bearer %s", HA_TOKEN);

    resp_ctx_t ctx = { .buf = buf, .len = 0, .cap = HIST_BUF_CAP };
    esp_http_client_config_t cfg = {
        .url           = url,
        .event_handler = on_data,
        .user_data     = &ctx,
        .timeout_ms    = 10000,
        .buffer_size   = 1024,
    };
    esp_http_client_handle_t hc = esp_http_client_init(&cfg);
    esp_http_client_set_header(hc, "Authorization", auth);

    esp_err_t err  = esp_http_client_perform(hc);
    int       code = esp_http_client_get_status_code(hc);
    esp_http_client_cleanup(hc);

    if (err != ESP_OK || code != 200) {
        ESP_LOGW(TAG, "history HTTP err=%d status=%d entity=%s",
                 err, code, entity_id);
        return ESP_FAIL;
    }
    buf[ctx.len] = '\0';

    /* Parse [[{...}, {...}, ...]] */
    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        ESP_LOGW(TAG, "JSON parse failed (%d bytes)", ctx.len);
        return ESP_FAIL;
    }

    cJSON *entity_arr = cJSON_IsArray(root) ? cJSON_GetArrayItem(root, 0) : NULL;
    if (!entity_arr || !cJSON_IsArray(entity_arr)) {
        ESP_LOGW(TAG, "no history array for %s", entity_id);
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    int n = cJSON_GetArraySize(entity_arr);
    if (n == 0) {
        ESP_LOGW(TAG, "empty window [%s..%s] for %s", start_z, end_z, entity_id);
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    cJSON *last  = cJSON_GetArrayItem(entity_arr, n - 1);
    cJSON *state = cJSON_GetObjectItemCaseSensitive(last, "state");
    if (!state || !cJSON_IsString(state)) {
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    *out_val = (float)atof(state->valuestring);
    ESP_LOGI(TAG, "%s [%s..%s] -> %.3f kWh (%d entries)",
             entity_id, start_z, end_z, *out_val, n);

    cJSON_Delete(root);
    return ESP_OK;
}

/* ---------------------------------------------------------------- API ----- */

/*
 * Fetch combined 7-day history: grid net kWh (import - export) and solar kWh.
 * Makes 21 HTTP requests (3 entities x 7 day windows, ~20 s total).
 * Intended to be called from ha_hist_task (background) in main.c.
 */
esp_err_t ha_history_fetch_combined(ha_history_t *out)
{
    memset(out, 0, sizeof(*out));

    time_t now = time(NULL);
    if (now < 1000000UL) {
        ESP_LOGW(TAG, "SNTP not synced");
        return ESP_FAIL;
    }

    /* Local midnight (HISTORY_DAYS-1) days ago -- oldest day on the chart */
    struct tm tm_start;
    localtime_r(&now, &tm_start);
    tm_start.tm_mday -= (HISTORY_DAYS - 1);
    tm_start.tm_hour  = 0;
    tm_start.tm_min   = 0;
    tm_start.tm_sec   = 0;
    time_t start_midnight = mktime(&tm_start);

    /* Day labels (Mon, Tue, ...) for the x-axis */
    for (int i = 0; i < HISTORY_DAYS; i++) {
        time_t day_t = start_midnight + (time_t)i * 86400;
        struct tm tm_day;
        localtime_r(&day_t, &tm_day);
        strftime(out->day_labels[i], sizeof(out->day_labels[i]), "%a", &tm_day);
    }

    char *buf = heap_caps_malloc(HIST_BUF_CAP, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) buf = malloc(HIST_BUF_CAP);
    if (!buf) { ESP_LOGE(TAG, "OOM for resp buf"); return ESP_ERR_NO_MEM; }

    bool any_ok = false;

    for (int i = 0; i < HISTORY_DAYS; i++) {
        /*
         * End of day i:
         *   past days -> 23:59:59 local (1 s before next midnight)
         *   today     -> now
         */
        time_t next_midnight = start_midnight + (time_t)(i + 1) * 86400;
        time_t win_end   = (i < HISTORY_DAYS - 1) ? (next_midnight - 1) : now;
        time_t win_start = win_end - 15 * 60;   /* 15-minute window */

        /* Grid: net kWh = import - export */
        float import_v = 0.0f, export_v = 0.0f;
        esp_err_t r_imp = fetch_entity_window_last(ENT_GRID_KWH,
                                                    win_start, win_end,
                                                    buf, &import_v);
        fetch_entity_window_last(ENT_EXPORT_KWH,
                                  win_start, win_end,
                                  buf, &export_v);
        if (r_imp == ESP_OK) {
            out->grid[i] = import_v - export_v;
            any_ok = true;
        }

        /* Solar kWh generated */
        float solar_v = 0.0f;
        if (fetch_entity_window_last(ENT_SOLAR_KWH,
                                      win_start, win_end,
                                      buf, &solar_v) == ESP_OK) {
            out->solar[i] = solar_v;
            any_ok = true;
        }
    }

    free(buf);
    out->valid = any_ok;
    return any_ok ? ESP_OK : ESP_FAIL;
}

#endif /* HAS_ENERGY */
