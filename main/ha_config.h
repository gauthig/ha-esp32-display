/*
 * ha_config.h — shim that pulls in the active device configuration.
 *
 * The real config lives in devices/<name>/device_config.h and is copied to
 * main/device_config.h by tools/flash-device.ps1 before each build.
 */
#pragma once
#include "device_config.h"
