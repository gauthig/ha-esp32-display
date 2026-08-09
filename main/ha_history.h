#pragma once
#include <stdbool.h>
#include "esp_err.h"

#define HISTORY_DAYS 7

/*
 * Combined 7-day history for both grid and solar.
 *   grid[i]  = net kWh from grid on day i (import − export).
 *              Negative values mean the house was a net exporter that day.
 *   solar[i] = solar kWh generated on day i.
 *   index 0 = oldest day, index 6 = today.
 */
typedef struct {
    float grid[HISTORY_DAYS];              /* net grid kWh per day */
    float solar[HISTORY_DAYS];             /* solar kWh per day    */
    char  day_labels[HISTORY_DAYS][4];     /* "Mon", "Tue", ...    */
    bool  valid;
} ha_history_t;

/*
 * Fetch combined 7-day history (grid net + solar) from HA long-term statistics.
 * Makes 3 sequential HTTP requests × 7 windows each = 21 calls (~20 s total).
 * Call from a background task; results are cached and shown instantly on tap.
 * Requires SNTP to be synced (time() > 1 000 000).
 */
esp_err_t ha_history_fetch_combined(ha_history_t *out);
