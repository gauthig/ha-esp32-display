/*
 * ha_weather.c — current conditions + forecasts from a Home Assistant weather
 * entity (ENT_WEATHER in device_config.h, e.g. NWS station KAPV).
 * Compiled only when HAS_WEATHER is set (ha_config.h).
 *
 * Three requests per refresh:
 *   GET  /api/states/<ENT_WEATHER>                                 current
 *   POST /api/services/weather/get_forecasts?return_response hourly     (~28 KB)
 *   POST /api/services/weather/get_forecasts?return_response twice_daily (~3 KB)
 *
 * The forecast datetimes come back in HA's local time with an offset
 * ("2026-10-02T06:00:00-07:00"), so the local hour and date are read straight
 * from the string; the offset is only used to skip hourly slots already past.
 */
#include "ha_weather.h"
#include "ha_config.h"

#if defined(HAS_WEATHER)

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <time.h>

#include "esp_http_client.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "cJSON.h"

static const char *TAG = "ha_weather";

#define RESP_CAP (64 * 1024)   /* hourly forecast is ~28 KB for 156 slots */

static char *s_resp;           /* PSRAM, allocated on first fetch */
static int   s_resp_len;
static bool  s_overflow;

/* ------------------------------------------------------------- HTTP ---- */

static esp_err_t on_data(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        int room = RESP_CAP - 1 - s_resp_len;
        int copy = evt->data_len < room ? evt->data_len : room;
        if (copy < evt->data_len) s_overflow = true;
        if (copy > 0) {
            memcpy(s_resp + s_resp_len, evt->data, copy);
            s_resp_len += copy;
        }
    }
    return ESP_OK;
}

/* GET when body is NULL, POST otherwise. Result is NUL-terminated in s_resp. */
static esp_err_t ha_request(const char *path, const char *body)
{
    s_resp_len = 0;
    s_overflow = false;

    char url[160];
    snprintf(url, sizeof(url), "http://%s:%d%s", HA_HOST, HA_PORT, path);

    char auth[300];
    snprintf(auth, sizeof(auth), "Bearer %s", HA_TOKEN);

    esp_http_client_config_t cfg = {
        .url           = url,
        .event_handler = on_data,
        .timeout_ms    = 15000,
        .buffer_size   = 1024,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    esp_http_client_set_header(c, "Authorization", auth);
    if (body) {
        esp_http_client_set_method(c, HTTP_METHOD_POST);
        esp_http_client_set_header(c, "Content-Type", "application/json");
        esp_http_client_set_post_field(c, body, strlen(body));
    }

    esp_err_t err    = esp_http_client_perform(c);
    int       status = esp_http_client_get_status_code(c);
    esp_http_client_cleanup(c);

    s_resp[s_resp_len] = '\0';
    if (err != ESP_OK || status != 200) {
        ESP_LOGW(TAG, "%s failed: err=%d status=%d", path, err, status);
        return ESP_FAIL;
    }
    if (s_overflow) {
        ESP_LOGW(TAG, "%s response exceeded %d bytes", path, RESP_CAP);
        return ESP_FAIL;
    }
    return ESP_OK;
}

/* ---------------------------------------------------------- Helpers ---- */

static wx_cond_t map_cond(const char *s)
{
    if (!s) return WX_UNKNOWN;
    if (!strcmp(s, "sunny") || !strcmp(s, "exceptional")) return WX_SUNNY;
    if (!strcmp(s, "clear-night"))                         return WX_CLEAR_NIGHT;
    if (!strcmp(s, "partlycloudy"))                        return WX_PARTLY;
    if (!strcmp(s, "cloudy") || !strcmp(s, "fog"))        return WX_CLOUDY;
    if (!strcmp(s, "rainy") || !strcmp(s, "pouring") ||
        !strcmp(s, "hail"))                                return WX_RAIN;
    if (!strncmp(s, "lightning", 9))                       return WX_STORM;
    if (!strncmp(s, "snowy", 5))                           return WX_SNOW;
    if (!strncmp(s, "windy", 5))                           return WX_WIND;
    return WX_UNKNOWN;
}

const char *ha_weather_cond_label(wx_cond_t c)
{
    switch (c) {
    case WX_SUNNY:       return "Sunny";
    case WX_CLEAR_NIGHT: return "Clear";
    case WX_PARTLY:      return "Partly cloudy";
    case WX_CLOUDY:      return "Cloudy";
    case WX_RAIN:        return "Rain";
    case WX_STORM:       return "Thunderstorms";
    case WX_SNOW:        return "Snow";
    case WX_WIND:        return "Windy";
    default:             return "--";
    }
}

static int16_t num_or(const cJSON *obj, const char *key, int16_t dflt)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsNumber(v) ? (int16_t)lround(v->valuedouble) : dflt;
}

/* Days since 1970-01-01 for a proleptic Gregorian date (H. Hinnant). */
static long days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    long yoe = y - era * 400;
    long doy = (153L * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

/*
 * Parse "YYYY-MM-DDTHH:MM:SS±HH:MM". Returns false on bad input.
 * *epoch is UTC seconds; *hour and *wday (0=Sun) are local to the string.
 */
static bool parse_dt(const char *s, time_t *epoch, int *hour, int *wday)
{
    int y, mo, d, h, mi, sec, oh = 0, om = 0;
    char sign = '+';
    if (!s || sscanf(s, "%4d-%2d-%2dT%2d:%2d:%2d%c%2d:%2d",
                     &y, &mo, &d, &h, &mi, &sec, &sign, &oh, &om) < 6)
        return false;
    long days = days_from_civil(y, mo, d);
    long off  = (oh * 3600L + om * 60L) * (sign == '-' ? -1 : 1);
    if (epoch) *epoch = (time_t)(days * 86400L + h * 3600L + mi * 60L + sec - off);
    if (hour)  *hour  = h;
    if (wday)  *wday  = (int)((days % 7 + 11) % 7);   /* 1970-01-01 = Thu */
    return true;
}

static cJSON *forecast_array(cJSON *root)
{
    cJSON *sr = cJSON_GetObjectItemCaseSensitive(root, "service_response");
    cJSON *en = cJSON_GetObjectItemCaseSensitive(sr, ENT_WEATHER);
    cJSON *fc = cJSON_GetObjectItemCaseSensitive(en, "forecast");
    return cJSON_IsArray(fc) ? fc : NULL;
}

/* ---------------------------------------------------------- Parsers ---- */

static esp_err_t fetch_current(ha_weather_t *out)
{
    if (ha_request("/api/states/" ENT_WEATHER, NULL) != ESP_OK) return ESP_FAIL;

    cJSON *root = cJSON_Parse(s_resp);
    if (!root) return ESP_FAIL;

    const cJSON *st = cJSON_GetObjectItemCaseSensitive(root, "state");
    const cJSON *at = cJSON_GetObjectItemCaseSensitive(root, "attributes");
    out->cond         = map_cond(cJSON_IsString(st) ? st->valuestring : NULL);
    out->temp         = num_or(at, "temperature",  INT16_MIN);
    out->humidity     = (int8_t)num_or(at, "humidity", -1);
    out->wind_mph     = num_or(at, "wind_speed",   -1);
    out->wind_bearing = num_or(at, "wind_bearing", -1);

    cJSON_Delete(root);
    return ESP_OK;
}

static const char k_hourly_body[] =
    "{\"entity_id\":\"" ENT_WEATHER "\",\"type\":\"hourly\"}";
static const char k_daily_body[] =
    "{\"entity_id\":\"" ENT_WEATHER "\",\"type\":\"twice_daily\"}";
#define FORECAST_PATH "/api/services/weather/get_forecasts?return_response"

static esp_err_t fetch_hourly(ha_weather_t *out)
{
    if (ha_request(FORECAST_PATH, k_hourly_body) != ESP_OK) return ESP_FAIL;

    cJSON *root = cJSON_Parse(s_resp);
    cJSON *fc   = forecast_array(root);
    if (!fc) { cJSON_Delete(root); return ESP_FAIL; }

    time_t now = time(NULL);
    bool   synced = now > 1000000000;
    out->n_hourly = 0;

    const cJSON *e;
    cJSON_ArrayForEach(e, fc) {
        if (out->n_hourly >= WX_HOURS) break;
        const cJSON *dt = cJSON_GetObjectItemCaseSensitive(e, "datetime");
        time_t t; int h;
        if (!cJSON_IsString(dt) || !parse_dt(dt->valuestring, &t, &h, NULL))
            continue;
        if (synced && t + 3600 <= now) continue;   /* slot already over */

        const cJSON *cd = cJSON_GetObjectItemCaseSensitive(e, "condition");
        wx_hour_t *s = &out->hourly[out->n_hourly++];
        s->hour = (int8_t)h;
        s->temp = num_or(e, "temperature", INT16_MIN);
        s->pop  = (int8_t)num_or(e, "precipitation_probability", -1);
        s->cond = map_cond(cJSON_IsString(cd) ? cd->valuestring : NULL);
    }

    cJSON_Delete(root);
    return out->n_hourly > 0 ? ESP_OK : ESP_FAIL;
}

static const char *k_dow[7] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };

static esp_err_t fetch_daily(ha_weather_t *out)
{
    if (ha_request(FORECAST_PATH, k_daily_body) != ESP_OK) return ESP_FAIL;

    cJSON *root = cJSON_Parse(s_resp);
    cJSON *fc   = forecast_array(root);
    if (!fc) { cJSON_Delete(root); return ESP_FAIL; }

    /* Group day/night entries by their local date ("YYYY-MM-DD"). */
    char last_date[11] = "";
    out->n_daily = 0;

    const cJSON *e;
    cJSON_ArrayForEach(e, fc) {
        const cJSON *dt = cJSON_GetObjectItemCaseSensitive(e, "datetime");
        int wday;
        if (!cJSON_IsString(dt) || strlen(dt->valuestring) < 10 ||
            !parse_dt(dt->valuestring, NULL, NULL, &wday))
            continue;

        if (strncmp(dt->valuestring, last_date, 10) != 0) {
            if (out->n_daily >= WX_DAYS) break;
            memcpy(last_date, dt->valuestring, 10);
            last_date[10] = '\0';
            wx_day_t *d = &out->daily[out->n_daily++];
            strcpy(d->dow, k_dow[wday]);
            d->hi = d->lo = INT16_MIN;
            d->pop  = -1;
            d->cond = WX_UNKNOWN;
        }

        wx_day_t *d = &out->daily[out->n_daily - 1];
        const cJSON *day = cJSON_GetObjectItemCaseSensitive(e, "is_daytime");
        const cJSON *cd  = cJSON_GetObjectItemCaseSensitive(e, "condition");
        int16_t temp = num_or(e, "temperature", INT16_MIN);
        int8_t  pop  = (int8_t)num_or(e, "precipitation_probability", -1);
        wx_cond_t c  = map_cond(cJSON_IsString(cd) ? cd->valuestring : NULL);

        if (cJSON_IsTrue(day)) {
            d->hi   = temp;
            d->cond = c;
        } else {
            d->lo = temp;
            if (d->cond == WX_UNKNOWN) d->cond = c;
        }
        if (pop > d->pop) d->pop = pop;
    }

    cJSON_Delete(root);
    return out->n_daily > 0 ? ESP_OK : ESP_FAIL;
}

/* ---------------------------------------------------------------- API --- */

esp_err_t ha_weather_fetch(ha_weather_t *out)
{
    if (!s_resp) {
        s_resp = heap_caps_malloc(RESP_CAP, MALLOC_CAP_SPIRAM);
        if (!s_resp) {
            ESP_LOGE(TAG, "no PSRAM for %d-byte response buffer", RESP_CAP);
            return ESP_ERR_NO_MEM;
        }
    }

    memset(out, 0, sizeof(*out));
    out->temp = out->today_hi = out->today_lo = INT16_MIN;
    out->humidity = -1;
    out->wind_mph = out->wind_bearing = -1;

    bool ok_cur = fetch_current(out) == ESP_OK;
    bool ok_hr  = fetch_hourly(out)  == ESP_OK;
    bool ok_day = fetch_daily(out)   == ESP_OK;

    /* NWS stations often report no condition text — use the current hour's. */
    if (out->cond == WX_UNKNOWN && out->n_hourly > 0)
        out->cond = out->hourly[0].cond;
    if (out->n_daily > 0) {
        out->today_hi = out->daily[0].hi;
        out->today_lo = out->daily[0].lo;
    }

    out->valid = ok_cur || ok_hr || ok_day;
    ESP_LOGI(TAG, "%d F %s | %d hourly / %d daily (cur=%d hr=%d day=%d)",
             out->temp, ha_weather_cond_label(out->cond),
             out->n_hourly, out->n_daily, ok_cur, ok_hr, ok_day);
    return out->valid ? ESP_OK : ESP_FAIL;
}

#endif /* HAS_WEATHER */
