/*
 * device_config.h — Ham Radio Control Panel
 *
 * Location : Ham radio station
 * Board    : Waveshare ESP32-S3-Touch-LCD-4.3 (non-B)
 * COM port : COM9
 * HA host  : 192.168.1.54:8123
 *
 * Flash command:
 *   .\tools\flash-device.ps1 -Device ham_controls -Port COM9
 *
 * This device shows three tap-to-toggle switch buttons and live power readings.
 * DEVICE_TYPE = DEVICE_TYPE_HAM_CONTROLS selects the ham UI and HA client.
 */
#pragma once
#include "secrets.h"   /* WIFI_SSID, WIFI_PASSWORD, HA_TOKEN */

#define DEVICE_TYPE         DEVICE_TYPE_HAM_CONTROLS
#define DEVICE_NAME         "Ham Controls"

#define HA_HOST             "192.168.1.54"
#define HA_PORT             8123
#define HA_POLL_INTERVAL_MS 15000
#define LOCAL_TZ            "PST8PDT,M3.2.0,M11.1.0"

/* ── Switch entities (tap-to-toggle buttons) ────────────────────────────── */
/* Order must match HAM_SW_NAMES_INIT */
#define HAM_SW_ENT_0   "switch.radio_power_supply"
#define HAM_SW_ENT_1   "switch.shelly1g4_a085e3c0f2c0"
#define HAM_SW_ENT_2   "switch.palstar_amp"

/* Short display labels for each switch button (must be 3 entries) */
#define HAM_SW_NAMES_INIT  "Radio PSU", "Shelly", "Palstar Amp"

/* ── Power sensor entities ──────────────────────────────────────────────── */
/* power[0] → shown on Radio PSU button;  power[1] → shown on Palstar Amp button */
#define HAM_POWER_ENT_0   "sensor.radio_power_supply_power"
#define HAM_POWER_ENT_1   "sensor.palstar_amp_power"
