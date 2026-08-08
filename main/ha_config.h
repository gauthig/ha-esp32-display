/*
 * ha_config.h — shim that pulls in the active device configuration.
 *
 * The real config lives in devices/<name>/device_config.h and is copied to
 * main/device_config.h by tools/flash-device.ps1 before each build.
 *
 * device_types.h is included first so that DEVICE_TYPE_* constants are
 * available when device_config.h sets #define DEVICE_TYPE ...
 */
#pragma once
#include "device_types.h"
#include "device_config.h"
