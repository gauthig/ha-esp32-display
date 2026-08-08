#pragma once
#include <stdbool.h>
#include "esp_err.h"

#define HISTORY_DAYS 7

typedef struct {
    float values[HISTORY_DAYS];          /* daily kWh, index 0 = oldest day  */
    char  day_labels[HISTORY_DAYS][4];   /* "Mon", "Tue", …                   */
    bool  valid;
} ha_history_t;

typedef enum { HIST_GRID = 0, HIST_SOLAR = 1 } ha_history_type_t;

/*
 * Fetch 7-day daily kWh history from HA long-term statistics.
 * HIST_GRID: net kWh per day (import − export; negative = net export day).
 * HIST_SOLAR: solar kWh generated per day.
 * Requires SNTP to be synced (time() > 1 000 000).
 */
esp_err_t ha_history_fetch(ha_history_type_t type, ha_history_t *out);
