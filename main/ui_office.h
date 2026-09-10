/*
 * ui_office.h — LVGL UI for the Office Panel 7 (Waveshare 7B, 600x1024 portrait).
 *
 * One HOME screen with:
 *   - Office Fan Lights card (state-aware bulb graphic; tap = toggle both,
 *     long-press = brightness/colour popup)
 *   - three HAM switch cards (Radio PSU / Shelly / Palstar Amp)
 *   - a bottom nav bar that opens the ENERGY screen
 * plus an ENERGY screen (portrait re-layout of the energy dashboard) and its
 * 7-day chart sub-screen.
 *
 * Compiled only for DEVICE_TYPE_OFFICE_PANEL.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "ha_config.h"

#if DEVICE_TYPE == DEVICE_TYPE_OFFICE_PANEL

#include "ha_ham.h"
#include "ha_light.h"
#include "ha_client.h"
#include "ha_history.h"

void ui_office_init(void);

/* State pushes — call under the LVGL port lock. */
void ui_office_set_connected(bool connected);
void ui_office_update_ham(const ha_ham_data_t *d);
void ui_office_update_light(const ha_light_data_t *d);
void ui_office_update_energy(const ha_data_t *d);
void ui_office_show_chart(const ha_history_t *hist);

/* Action callbacks — invoked under the LVGL lock from widget events. The
 * implementations in main.c stash the request and wake the poll task. */
void ui_office_set_ham_toggle_cb(void (*cb)(int switch_idx));
void ui_office_set_light_toggle_cb(void (*cb)(void));
void ui_office_set_light_brightness_cb(void (*cb)(int percent));
void ui_office_set_light_rgb_cb(void (*cb)(uint8_t r, uint8_t g, uint8_t b));
void ui_office_set_chart_request_cb(void (*cb)(void));

#endif /* DEVICE_TYPE_OFFICE_PANEL */
