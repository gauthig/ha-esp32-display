/*
 * device_config.h — NEW DEVICE TEMPLATE
 *
 * Copy this directory to devices/<your_device_name>/ and edit every
 * field marked with TODO. Then run:
 *   .\tools\flash-device.ps1 -Device <your_device_name> -Port COM<X>
 *
 * Steps:
 *   1. Set DEVICE_NAME, HA_HOST, HA_PORT, LOCAL_TZ
 *   2. Replace ENT_* with the correct HA entity IDs for your installation
 *   3. Set HA_NUM_CIRCUITS to match your circuit count (1–13)
 *   4. Fill CIRCUIT_NAMES_INIT and CIRCUIT_ENTITIES_TEMPLATE with your circuits
 *   5. Copy secrets.h.example → secrets.h and fill in credentials
 *   6. Update INFO.md with location and port
 *
 * Finding entity IDs in HA: Developer Tools → States, search for your sensor.
 * Emporia Vue entities typically end in _power_minute_average.
 * No solar? Set ENT_SOLAR_W, ENT_SOLAR_KWH, ENT_NET_W to ENT_TOTAL_W.
 */
#pragma once
#include "secrets.h"   /* WIFI_SSID, WIFI_PASSWORD, HA_TOKEN */

/* TODO: set display label */
#define DEVICE_NAME         "My Display"

/* TODO: set HA connection */
#define HA_HOST             "192.168.1.54"
#define HA_PORT             8123
#define HA_POLL_INTERVAL_MS 30000

/* TODO: set your timezone (POSIX TZ string) */
/* US Pacific:  PST8PDT,M3.2.0,M11.1.0  */
/* US Mountain: MST7MDT,M3.2.0,M11.1.0  */
/* US Central:  CST6CDT,M3.2.0,M11.1.0  */
/* US Eastern:  EST5EDT,M3.2.0,M11.1.0  */
#define LOCAL_TZ            "PST8PDT,M3.2.0,M11.1.0"

/* ── Main energy sensors — TODO: replace with your entity IDs ──────────── */
#define ENT_GRID_KWH    "sensor.mainsfromgrid_energy_today"
#define ENT_EXPORT_KWH  "sensor.mainstogrid_energy_today"
#define ENT_SOLAR_W     "sensor.solar_line_1_power_minute_average"
#define ENT_TOTAL_W     "sensor.totalusage_power_minute_average"
/* Net power entity (>0 = importing, <0 = exporting). If no solar exists,   */
/* set ENT_NET_W to the same value as ENT_TOTAL_W.                          */
#define ENT_NET_W       "sensor.energy_grid_totalusage_power_minute_average" \
                        "_solar_line_1_power_minute_average_net_power"
#define ENT_SOLAR_KWH   "sensor.solar_line_1_energy_today"

/* ── Circuits — TODO: set count, names, and entity IDs ─────────────────── */
/* Min 1, max 13. All three definitions must have exactly HA_NUM_CIRCUITS    */
/* entries and be in the same order.                                         */
#define HA_NUM_CIRCUITS 3   /* TODO: change to your actual count */

#define CIRCUIT_NAMES_INIT \
    "Circuit 1",   \
    "Circuit 2",   \
    "Circuit 3"

/* Last entry has NO trailing | */
#define CIRCUIT_ENTITIES_TEMPLATE \
    "{{ states('sensor.circuit1_power_minute_average') }}|"  \
    "{{ states('sensor.circuit2_power_minute_average') }}|"  \
    "{{ states('sensor.circuit3_power_minute_average') }}"
