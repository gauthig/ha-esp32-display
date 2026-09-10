/*
 * ha_config.h — shim that pulls in the active device configuration.
 *
 * The real config lives in devices/<name>/device_config.h and is copied to
 * main/device_config.h by tools/flash-device.ps1 before each build.
 *
 * device_types.h is included first so that DEVICE_TYPE_* constants are
 * available when device_config.h sets #define DEVICE_TYPE ...
 *
 * After device_config.h has run, DEVICE_TYPE is known and we derive the
 * per-feature compile switches. Source files guard on these
 * (HAS_ENERGY / HAS_HAM / HAS_LIGHT) rather than on DEVICE_TYPE directly,
 * so a composite device (the office panel) can pull in several modules.
 */
#pragma once
#include "device_types.h"
#include "device_config.h"

#if DEVICE_TYPE == DEVICE_TYPE_ENERGY
#  define HAS_ENERGY 1
#elif DEVICE_TYPE == DEVICE_TYPE_OFFICE_PANEL
#  define HAS_ENERGY 1
#  define HAS_HAM 1
#  define HAS_LIGHT 1
#endif
