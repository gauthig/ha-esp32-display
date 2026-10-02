#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#define WX_HOURS 12
#define WX_DAYS  5

/* Condition buckets — HA weather condition strings collapse into these. */
typedef enum {
    WX_UNKNOWN = 0,
    WX_SUNNY,          /* sunny, exceptional            */
    WX_CLEAR_NIGHT,    /* clear-night                   */
    WX_PARTLY,         /* partlycloudy                  */
    WX_CLOUDY,         /* cloudy, fog                   */
    WX_RAIN,           /* rainy, pouring, hail          */
    WX_STORM,          /* lightning, lightning-rainy    */
    WX_SNOW,           /* snowy, snowy-rainy            */
    WX_WIND,           /* windy, windy-variant          */
} wx_cond_t;

typedef struct {
    int8_t    hour;    /* local hour 0-23 */
    int16_t   temp;    /* °F */
    int8_t    pop;     /* precipitation probability %, -1 = n/a */
    wx_cond_t cond;
} wx_hour_t;

typedef struct {
    char      dow[4];  /* "Mon" */
    int16_t   hi;      /* °F, INT16_MIN = n/a */
    int16_t   lo;      /* °F, INT16_MIN = n/a */
    int8_t    pop;     /* max precipitation probability % of day + night */
    wx_cond_t cond;    /* daytime condition (night if no daytime entry) */
} wx_day_t;

/*
 * Weather snapshot from Home Assistant (ENT_WEATHER in device_config.h).
 * Temperatures are rounded to whole degrees in the entity's unit (°F here).
 */
typedef struct {
    int16_t   temp;           /* current, INT16_MIN = n/a */
    int8_t    humidity;       /* %, -1 = n/a */
    int16_t   wind_mph;       /* -1 = n/a */
    int16_t   wind_bearing;   /* degrees, -1 = n/a / calm */
    wx_cond_t cond;           /* current; falls back to first hourly slot */
    int16_t   today_hi;       /* INT16_MIN = n/a */
    int16_t   today_lo;       /* INT16_MIN = n/a */
    wx_hour_t hourly[WX_HOURS];
    int       n_hourly;
    wx_day_t  daily[WX_DAYS];
    int       n_daily;
    bool      valid;
} ha_weather_t;

/*
 * Fetch current conditions + hourly + twice-daily forecasts (3 HTTP calls).
 * Call from a background task. Requires SNTP to be synced so past hourly
 * slots can be skipped.
 */
esp_err_t ha_weather_fetch(ha_weather_t *out);

/* Short display label for a condition ("Sunny", "Partly cloudy", ...). */
const char *ha_weather_cond_label(wx_cond_t c);
