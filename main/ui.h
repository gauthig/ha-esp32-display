#pragma once
#include <stdbool.h>
#include "ha_client.h"
#include "ha_history.h"
#include "ha_config.h"

#if DEVICE_TYPE == DEVICE_TYPE_ENERGY

/* Build all LVGL widgets on the active screen. Call once after board_display_init(). */
void ui_init(void);

/* Refresh all data widgets. Must be called under the LVGL port lock. */
void ui_update(const ha_data_t *data);

/* Mark WiFi/HA as connected or disconnected (updates status dot). */
void ui_set_connected(bool connected);

/*
 * Register callback invoked (under LVGL lock) when the user taps a stat card.
 * The callback should trigger a history fetch and then call ui_show_chart().
 * type: 0 = HIST_GRID, 1 = HIST_SOLAR.
 */
void ui_set_chart_request_cb(void (*cb)(int type));

/*
 * Replace the current screen with a 7-day line chart.
 * Must be called under the LVGL port lock.
 */
void ui_show_chart(ha_history_type_t type, const ha_history_t *hist);

#endif /* DEVICE_TYPE_ENERGY */
