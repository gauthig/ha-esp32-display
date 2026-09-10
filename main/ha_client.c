/*
 * ha_client.c — polls Home Assistant /api/template for all energy data in one
 * HTTP request and parses the pipe-delimited result.
 * Compiled only for DEVICE_TYPE_ENERGY builds.
 *
 * All sensor entity IDs and circuit definitions come from device_config.h,
 * which is selected per-device by tools/flash-device.ps1.
 *
 * Template index map (6 + HA_NUM_CIRCUITS values, '|' separated):
 *   [0]  ENT_GRID_KWH   — grid import kWh today
 *   [1]  ENT_EXPORT_KWH — grid export kWh (solar overage, reserved)
 *   [2]  ENT_SOLAR_W    — current solar generation W
 *   [3]  ENT_TOTAL_W    — total home consumption W
 *   [4]  ENT_NET_W      — net grid W (>0 = importing, <0 = exporting)
 *   [5]  ENT_SOLAR_KWH  — solar kWh generated today
 *   [6..6+HA_NUM_CIRCUITS-1] — CIRCUIT_ENTITIES_TEMPLATE
 */
#include "ha_client.h"
#include "ha_config.h"

#if defined(HAS_ENERGY)

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "esp_http_client.h"
#include "esp_log.h"

static const char *TAG = "ha_client";

#define RESP_BUF 768

static const char *s_names[HA_NUM_CIRCUITS] = { CIRCUIT_NAMES_INIT };

/* Template body built entirely at compile time via C string concatenation.  */
/* Edit entity IDs in devices/<name>/device_config.h, not here.              */
static const char s_body[] =
    "{\"template\": \""
    "{{ states('" ENT_GRID_KWH   "') }}|"
    "{{ states('" ENT_EXPORT_KWH "') }}|"
    "{{ states('" ENT_SOLAR_W    "') }}|"
    "{{ states('" ENT_TOTAL_W    "') }}|"
    "{{ states('" ENT_NET_W      "') }}|"
    "{{ states('" ENT_SOLAR_KWH  "') }}|"
    CIRCUIT_ENTITIES_TEMPLATE
    "\"}";

static char s_resp[RESP_BUF];
static int  s_resp_len;

static esp_err_t on_data(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        int room = RESP_BUF - 1 - s_resp_len;
        int copy = evt->data_len < room ? evt->data_len : room;
        if (copy > 0) {
            memcpy(s_resp + s_resp_len, evt->data, copy);
            s_resp_len += copy;
        }
    }
    return ESP_OK;
}

/* Consume next '|'-delimited token; returns -1.0f for unavailable/unknown. */
static float next_token(const char **pp)
{
    const char *p = *pp;
    if (!p || *p == '\0') return -1.0f;

    const char *end = strchr(p, '|');
    char tok[48];
    int  len;
    if (end) {
        len = (int)(end - p);
        *pp = end + 1;
    } else {
        len = (int)strlen(p);
        *pp = p + len;
    }
    if (len <= 0 || len >= (int)sizeof(tok)) return -1.0f;
    memcpy(tok, p, len);
    tok[len] = '\0';

    if (strcmp(tok, "unavailable") == 0 || strcmp(tok, "unknown") == 0)
        return -1.0f;

    char *endp;
    float v = strtof(tok, &endp);
    return (endp != tok) ? v : -1.0f;
}

/* ---------------------------------------------------------------- API --- */

const char **ha_circuit_names(void) { return s_names; }

esp_err_t ha_client_fetch(ha_data_t *out)
{
    s_resp_len = 0;

    char url[64];
    snprintf(url, sizeof(url), "http://%s:%d/api/template", HA_HOST, HA_PORT);

    char auth[300];
    snprintf(auth, sizeof(auth), "Bearer %s", HA_TOKEN);

    esp_http_client_config_t cfg = {
        .url           = url,
        .event_handler = on_data,
        .timeout_ms    = 10000,
        .buffer_size   = 512,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    esp_http_client_set_method(c, HTTP_METHOD_POST);
    esp_http_client_set_header(c, "Authorization", auth);
    esp_http_client_set_header(c, "Content-Type", "application/json");
    esp_http_client_set_post_field(c, s_body, sizeof(s_body) - 1);

    esp_err_t err    = esp_http_client_perform(c);
    int       status = esp_http_client_get_status_code(c);
    esp_http_client_cleanup(c);

    if (err != ESP_OK || status != 200) {
        ESP_LOGW(TAG, "fetch failed: err=%d status=%d", err, status);
        return ESP_FAIL;
    }

    s_resp[s_resp_len] = '\0';
    const char *p = s_resp;

    out->grid_kwh_today   = next_token(&p);   /* [0] */
    out->export_kwh_today = next_token(&p);   /* [1] */
    out->solar_power_w    = next_token(&p);   /* [2] */
    out->total_power_w   = next_token(&p);   /* [3] */
    out->net_grid_w      = next_token(&p);   /* [4] */
    out->solar_kwh_today = next_token(&p);   /* [5] */

    for (int i = 0; i < HA_NUM_CIRCUITS; i++)
        out->circuit_power[i] = next_token(&p);

    out->valid = true;

    ESP_LOGI(TAG, "grid %.1f kWh | net %.0f W | solar %.0f W (%.1f kWh today)",
             out->grid_kwh_today, out->net_grid_w,
             out->solar_power_w, out->solar_kwh_today);
    return ESP_OK;
}

#endif /* HAS_ENERGY */
