/*
 * ha_light.c — grouped-light HA client for the office panel.
 *
 * Fetch: POST /api/template, pipe-delimited result
 *   [0] state                "on" / "off"
 *   [1] brightness (0..255)  or "None"
 *   [2] "r,g,b"              rgb_color, or "255,255,255" when the light has none
 *
 * Control (both entities at once), POST to /api/services/light/<service>:
 *   toggle      service "toggle"   body {"entity_id":[a,b]}
 *   brightness  service "turn_on"  body {"entity_id":[a,b],"brightness_pct":N}
 *   color       service "turn_on"  body {"entity_id":[a,b],"rgb_color":[r,g,b]}
 *
 * Entity IDs come from LIGHT_ENT_0 / LIGHT_ENT_1 in device_config.h.
 */
#include "ha_light.h"
#include "ha_config.h"

#if defined(HAS_LIGHT)

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "esp_http_client.h"
#include "esp_log.h"

static const char *TAG = "ha_light";

#define RESP_BUF 128

/* JSON array of both entity IDs, e.g. "light.a","light.b" */
#define LIGHT_ENTITY_JSON_LIST  "\"" LIGHT_ENT_0 "\",\"" LIGHT_ENT_1 "\""

static const char s_fetch_body[] =
    "{\"template\": \""
    "{{ states('" LIGHT_ENT_0 "') }}|"
    "{{ state_attr('" LIGHT_ENT_0 "','brightness') }}|"
    "{{ (state_attr('" LIGHT_ENT_0 "','rgb_color') or [255,255,255]) | join(',') }}"
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

/* Split *pp at the next '|' into tok[]; advance *pp past it. */
static void next_field(const char **pp, char *tok, int toksz)
{
    const char *p = *pp;
    tok[0] = '\0';
    if (!p) return;
    const char *end = strchr(p, '|');
    int len = end ? (int)(end - p) : (int)strlen(p);
    *pp = end ? end + 1 : p + len;
    if (len < 0 || len >= toksz) len = toksz - 1;
    memcpy(tok, p, len);
    tok[len] = '\0';
}

static bool is_nullish(const char *s)
{
    return s[0] == '\0' ||
           strcmp(s, "None") == 0 ||
           strcmp(s, "unknown") == 0 ||
           strcmp(s, "unavailable") == 0;
}

/* ---------------------------------------------------------- HTTP POST --- */

static esp_err_t post_json(const char *path, const char *body)
{
    s_resp_len = 0;

    char url[96];
    snprintf(url, sizeof(url), "http://%s:%d%s", HA_HOST, HA_PORT, path);

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
    esp_http_client_set_post_field(c, body, (int)strlen(body));

    esp_err_t err    = esp_http_client_perform(c);
    int       status = esp_http_client_get_status_code(c);
    esp_http_client_cleanup(c);

    if (err != ESP_OK || (status != 200 && status != 201)) {
        ESP_LOGW(TAG, "POST %s failed: err=%d status=%d", path, err, status);
        return ESP_FAIL;
    }
    return ESP_OK;
}

/* ---------------------------------------------------------------- API --- */

esp_err_t ha_light_fetch(ha_light_data_t *out)
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
    esp_http_client_set_post_field(c, s_fetch_body, sizeof(s_fetch_body) - 1);

    esp_err_t err    = esp_http_client_perform(c);
    int       status = esp_http_client_get_status_code(c);
    esp_http_client_cleanup(c);

    if (err != ESP_OK || status != 200) {
        ESP_LOGW(TAG, "fetch failed: err=%d status=%d", err, status);
        return ESP_FAIL;
    }

    s_resp[s_resp_len] = '\0';
    const char *p = s_resp;
    char tok[48];

    next_field(&p, tok, sizeof(tok));
    out->on = (strcmp(tok, "on") == 0);

    next_field(&p, tok, sizeof(tok));
    if (is_nullish(tok)) {
        out->brightness_pct = -1;
    } else {
        int raw = atoi(tok);                 /* 0..255 */
        if (raw < 0) raw = 0;
        if (raw > 255) raw = 255;
        out->brightness_pct = (raw * 100 + 127) / 255;
        if (out->brightness_pct < 1 && raw > 0) out->brightness_pct = 1;
    }

    next_field(&p, tok, sizeof(tok));
    {
        int r = 255, g = 255, b = 255;
        if (!is_nullish(tok)) sscanf(tok, "%d,%d,%d", &r, &g, &b);
        out->r = (uint8_t)(r < 0 ? 0 : r > 255 ? 255 : r);
        out->g = (uint8_t)(g < 0 ? 0 : g > 255 ? 255 : g);
        out->b = (uint8_t)(b < 0 ? 0 : b > 255 ? 255 : b);
    }

    out->valid = true;
    ESP_LOGI(TAG, "light: %s bri=%d%% rgb=%u,%u,%u",
             out->on ? "on" : "off", out->brightness_pct,
             out->r, out->g, out->b);
    return ESP_OK;
}

esp_err_t ha_light_toggle(void)
{
    return post_json("/api/services/light/toggle",
                     "{\"entity_id\":[" LIGHT_ENTITY_JSON_LIST "]}");
}

esp_err_t ha_light_set_brightness(int percent)
{
    if (percent < 1)   percent = 1;
    if (percent > 100) percent = 100;
    char body[192];
    snprintf(body, sizeof(body),
             "{\"entity_id\":[" LIGHT_ENTITY_JSON_LIST "],\"brightness_pct\":%d}",
             percent);
    return post_json("/api/services/light/turn_on", body);
}

esp_err_t ha_light_set_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    char body[224];
    snprintf(body, sizeof(body),
             "{\"entity_id\":[" LIGHT_ENTITY_JSON_LIST "],\"rgb_color\":[%u,%u,%u]}",
             r, g, b);
    return post_json("/api/services/light/turn_on", body);
}

#endif /* HAS_LIGHT */
