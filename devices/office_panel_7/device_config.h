/*
 * device_config.h — Office Panel 7
 *
 * Location : Office wall (PORTRAIT mount)
 * Board    : Waveshare ESP32-S3-Touch-LCD-7B (1024x600 → 600x1024 portrait)
 * COM port : COM25
 * HA host  : 192.168.1.54:8123
 *
 * Flash command:
 *   .\tools\flash-device.ps1 -Device office_panel_7 -Port COM25
 *
 * Combo panel: DEVICE_TYPE_OFFICE_PANEL pulls in ha_ham.c (3 switches),
 * ha_light.c (grouped Office Fan Lights), and ha_client.c + ha_history.c
 * (the energy screen, reached from the bottom nav bar). UI is ui_office.c.
 */
#pragma once
#include "secrets.h"   /* WIFI_SSID, WIFI_PASSWORD, HA_TOKEN */

#define DEVICE_TYPE         DEVICE_TYPE_OFFICE_PANEL
#define DEVICE_NAME         "Office Panel 7"

/* Select the 7B board bring-up (board_7b.c). Absence of this = 4.3" non-B. */
#define BOARD_VARIANT_7B    1

#define HA_HOST             "192.168.1.54"
#define HA_PORT             8123
#define HA_POLL_INTERVAL_MS 15000
#define LOCAL_TZ            "PST8PDT,M3.2.0,M11.1.0"

/* ── Grouped light (Office Fan Lights) ──────────────────────────────────── */
/* Both entities are driven together: tap toggles both, the long-press popup
 * applies brightness / colour to both. */
#define LIGHT_ENT_0    "light.office_fan_light_1"
#define LIGHT_ENT_1    "light.office_fan_light_2"

/* ── HAM switch entities (same three as the ham_controls panel) ─────────── */
#define HAM_SW_ENT_0   "switch.radio_power_supply"
#define HAM_SW_ENT_1   "switch.shelly1g4_a085e3c0f2c0"
#define HAM_SW_ENT_2   "switch.palstar_amp"
#define HAM_SW_NAMES_INIT  "Radio PSU", "Shelly", "Palstar Amp"
#define HAM_POWER_ENT_0   "sensor.radio_power_supply_power"
#define HAM_POWER_ENT_1   "sensor.palstar_amp_power"

/* ── Energy screen sensors (same install as energy_4v3_lcd) ─────────────── */
#define ENT_GRID_KWH    "sensor.mainsfromgrid_energy_today"
#define ENT_EXPORT_KWH  "sensor.mainstogrid_energy_today"
#define ENT_SOLAR_W     "sensor.solar_line_1_power_minute_average"
#define ENT_TOTAL_W     "sensor.totalusage_power_minute_average"
#define ENT_NET_W       "sensor.energy_grid_totalusage_power_minute_average" \
                        "_solar_line_1_power_minute_average_net_power"
#define ENT_SOLAR_KWH   "sensor.solar_line_1_energy_today"

/* ── Circuits ──────────────────────────────────────────────────────────── */
#define HA_NUM_CIRCUITS 13

#define CIRCUIT_NAMES_INIT \
    "Pool",        \
    "A/C",         \
    "Oven",        \
    "Range",       \
    "EV Charger",  \
    "Laundry",     \
    "HVAC Fan",    \
    "Garage",      \
    "Fridge",      \
    "Computers",   \
    "TV Room",     \
    "Master BR",   \
    "Upstairs BR"

#define CIRCUIT_ENTITIES_TEMPLATE \
    "{{ states('sensor.pool_power_minute_average') }}|"            \
    "{{ states('sensor.ac_power_minute_average') }}|"              \
    "{{ states('sensor.oven_power_minute_average') }}|"            \
    "{{ states('sensor.range_power_minute_average') }}|"           \
    "{{ states('sensor.evcharger_power_minute_average') }}|"       \
    "{{ states('sensor.laundry_room_power_minute_average') }}|"    \
    "{{ states('sensor.hvac_fan_power_minute_average') }}|"        \
    "{{ states('sensor.garage_power_minute_average') }}|"          \
    "{{ states('sensor.fridge_power_minute_average') }}|"          \
    "{{ states('sensor.computers_power_minute_average') }}|"       \
    "{{ states('sensor.tv_room_power_minute_average') }}|"         \
    "{{ states('sensor.master_bedroom_power_minute_average') }}|"  \
    "{{ states('sensor.2_upstairs_bedrooms_power_minute_average') }}"
