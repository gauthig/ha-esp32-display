#pragma once
#include <stdbool.h>
#include "ha_client.h"

/* Build all LVGL widgets on the active screen. Call once after board_display_init(). */
void ui_init(void);

/* Refresh all data widgets. Must be called under the LVGL port lock. */
void ui_update(const ha_data_t *data);

/* Mark WiFi/HA as connected or disconnected (updates status dot). */
void ui_set_connected(bool connected);
