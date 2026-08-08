/*
 * device_types.h — DEVICE_TYPE constants shared by all device configs.
 *
 * Include this before device_config.h (done automatically via ha_config.h).
 * Each devices/<name>/device_config.h sets:
 *   #define DEVICE_TYPE  DEVICE_TYPE_ENERGY          (energy monitor)
 *   #define DEVICE_TYPE  DEVICE_TYPE_HAM_CONTROLS    (ham radio control panel)
 */
#pragma once

#define DEVICE_TYPE_ENERGY       1
#define DEVICE_TYPE_HAM_CONTROLS 2
