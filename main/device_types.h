/*
 * device_types.h — DEVICE_TYPE constants shared by all device configs.
 *
 * Include this before device_config.h (done automatically via ha_config.h).
 * Each devices/<name>/device_config.h sets:
 *   #define DEVICE_TYPE  DEVICE_TYPE_ENERGY          (energy monitor)
 *   #define DEVICE_TYPE  DEVICE_TYPE_OFFICE_PANEL    (7B combo control panel)
 *
 * The HAS_ENERGY / HAS_HAM / HAS_LIGHT feature macros derived from DEVICE_TYPE
 * are defined in ha_config.h, after device_config.h has run.
 *
 * Value 2 (DEVICE_TYPE_HAM_CONTROLS) was retired when the dedicated ham panel
 * was folded into the office panel; the gap is intentional.
 */
#pragma once

#define DEVICE_TYPE_ENERGY       1
#define DEVICE_TYPE_OFFICE_PANEL 3
