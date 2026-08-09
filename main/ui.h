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
 * Register callback invoked (under LVGL lock) when the user taps a chart card.
 * The callback should call ui_show_chart() with cached history data immediately.
 * No type arg — both grid and solar cards now show the same combined chart.
 */
void ui_set_chart_request_cb(void (*cb)(void));

/*
 * Replace the current screen with the combined 7-day line chart.
 * Shows both grid (amber) and solar (green) series on a shared axis.
 * Must be called under the LVGL port lock.
 * If hist->valid is false, shows a "loading" message instead.
 */
void ui_show_chart(const ha_history_t *hist);

#endif /* DEVICE_TYPE_ENERGY */
