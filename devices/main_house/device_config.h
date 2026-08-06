/*
 * device_config.h — Main House energy display
 *
 * Location : Main electrical panel / living room
 * Board    : Waveshare ESP32-S3-Touch-LCD-4.3 (non-B)
 * COM port : COM9
 * HA host  : 192.168.1.54:8123
 *
 * Flash command:
 *   .\tools\flash-device.ps1 -Device main_house -Port COM9
 */
#pragma once
#include "secrets.h"   /* WIFI_SSID, WIFI_PASSWORD, HA_TOKEN */

#define DEVICE_NAME         "Main House"

#define HA_HOST             "192.168.1.54"
#define HA_PORT             8123
#define HA_POLL_INTERVAL_MS 30000
#define LOCAL_TZ            "PST8PDT,M3.2.0,M11.1.0"

/* ── Main energy sensors ────────────────────────────────────────────────── */
#define ENT_GRID_KWH    "sensor.mainsfromgrid_energy_today"
#define ENT_EXPORT_KWH  "sensor.mainstogrid_energy_today"
#define ENT_SOLAR_W     "sensor.solar_line_1_power_minute_average"
#define ENT_TOTAL_W     "sensor.totalusage_power_minute_average"
#define ENT_NET_W       "sensor.energy_grid_totalusage_power_minute_average" \
                        "_solar_line_1_power_minute_average_net_power"
#define ENT_SOLAR_KWH   "sensor.solar_line_1_energy_today"

/* ── Circuits ───────────────────────────────────────────────────────────── */
/* Keep HA_NUM_CIRCUITS, CIRCUIT_NAMES_INIT, and CIRCUIT_ENTITIES_TEMPLATE  */
/* all the same length and in the same order.                                */
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
