/*
 * ha_ham.c — Ham radio control panel HA client.
 *
 * Template index map (5 values, pipe separated):
 *   [0] HAM_SW_ENT_0    switch.radio_power_supply       (on/off)
 *   [1] HAM_SW_ENT_1    switch.shelly1g4_a085e3c0f2c0   (on/off)
 *   [2] HAM_SW_ENT_2    switch.palstar_amp               (on/off)
 *   [3] HAM_POWER_ENT_0 sensor.radio_power_supply_power  (W)
 *   [4] HAM_POWER_ENT_1 sensor.palstar_amp               (W)
 *
 * Toggle: POST /api/services/switch/toggle {"entity_id":"switch.xxx"}
 */
#include "ha_ham.h"
#include "ha_config.h"

#if defined(HAS_HAM)

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "esp_http_client.h"
#include "esp_log.h"

static const char *TAG = "ha_ham";

#define RESP_BUF 256

static const char s_fetch_body[] =
    "{\"template\": \""
    "{{ states('" HAM_SW_ENT_0    "') }}|"
    "{{ states('" HAM_SW_ENT_1    "') }}|"
    "{{ states('" HAM_SW_ENT_2    "') }}|"
    "{{ states('" HAM_POWER_ENT_0 "') }}|"
    "{{ states('" HAM_POWER_ENT_1 "') }}"
    "\"}";

static const char *s_sw_entities[HAM_NUM_SWITCHES] = {
    HAM_SW_ENT_0, HAM_SW_ENT_1, HAM_SW_ENT_2
};

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

static float next_float(const char **pp)
{
    const char *p = *pp;
    if (!p || *p == '\0') return -1.0f;
    const char *end = strchr(p, '|');
    char tok[48];
    int  len;
    if (end) { len = (int)(end - p); *pp = end + 1; }
    else      { len = (int)strlen(p); *pp = p + len; }
    if (len <= 0 || len >= (int)sizeof(tok)) return -1.0f;
    memcpy(tok, p, len);
    tok[len] = '\0';
    if (strcmp(tok, "unavailable") == 0 || strcmp(tok, "unknown") == 0)
        return -1.0f;
    char *endp;
    float v = strtof(tok, &endp);
    return (endp != tok) ? v : -1.0f;
}

static bool next_bool(const char **pp)
{
    const char *p = *pp;
    if (!p || *p == '\0') return false;
    const char *end = strchr(p, '|');
    char tok[16];
    int  len;
    if (end) { len = (int)(end - p); *pp = end + 1; }
    else      { len = (int)strlen(p); *pp = p + len; }
    if (len <= 0 || len >= (int)sizeof(tok)) return false;
    memcpy(tok, p, len);
    tok[len] = '\0';
    return (strcmp(tok, "on") == 0);
}

esp_err_t ha_ham_fetch(ha_ham_data_t *out)
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

    for (int i = 0; i < HAM_NUM_SWITCHES; i++)
        out->sw[i] = next_bool(&p);

    for (int i = 0; i < HAM_NUM_POWER; i++)
        out->power[i] = next_float(&p);

    out->valid = true;

    ESP_LOGI(TAG, "sw=[%d,%d,%d] pwr=[%.1f,%.1f]",
             out->sw[0], out->sw[1], out->sw[2],
             out->power[0], out->power[1]);
    return ESP_OK;
}

esp_err_t ha_ham_toggle(int switch_idx)
{
    if (switch_idx < 0 || switch_idx >= HAM_NUM_SWITCHES) return ESP_ERR_INVALID_ARG;

    s_resp_len = 0;

    char url[80];
    snprintf(url, sizeof(url), "http://%s:%d/api/services/switch/toggle",
             HA_HOST, HA_PORT);

    char auth[300];
    snprintf(auth, sizeof(auth), "Bearer %s", HA_TOKEN);

    char body[128];
    snprintf(body, sizeof(body), "{\"entity_id\":\"%s\"}", s_sw_entities[switch_idx]);

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

    if (err != ESP_OK || status != 200) {
        ESP_LOGW(TAG, "toggle[%d] failed: err=%d status=%d", switch_idx, err, status);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "toggle[%d] %s OK", switch_idx, s_sw_entities[switch_idx]);
    return ESP_OK;
}

#endif /* HAS_HAM */
