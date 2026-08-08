/*
 * ui_ham.h — LVGL UI for the Ham Radio control panel.
 * Compiled only for DEVICE_TYPE_HAM_CONTROLS builds.
 */
#pragma once
#include <stdbool.h>
#include "ha_ham.h"
#include "ha_config.h"

#if DEVICE_TYPE == DEVICE_TYPE_HAM_CONTROLS

void ui_ham_init(void);
void ui_ham_update(const ha_ham_data_t *data);
void ui_ham_set_connected(bool connected);
void ui_ham_set_toggle_cb(void (*cb)(int switch_idx));

#endif /* DEVICE_TYPE_HAM_CONTROLS */
