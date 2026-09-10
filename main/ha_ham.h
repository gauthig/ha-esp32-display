/*
 * ha_ham.h — Home Assistant client for the Ham Radio control panel.
 *
 * Fetches the state of three HA switches and two power sensors via the
 * /api/template endpoint, and can toggle any switch via /api/services.
 *
 * Compiled only for DEVICE_TYPE_HAM_CONTROLS builds.
 */
#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "ha_config.h"   /* device_types.h + device_config.h */

#if defined(HAS_HAM)

#define HAM_NUM_SWITCHES 3
#define HAM_NUM_POWER    2

typedef struct {
    bool  sw[HAM_NUM_SWITCHES];   /* true = on, false = off              */
    float power[HAM_NUM_POWER];   /* W readings; -1.0 = unavailable      */
    bool  valid;
} ha_ham_data_t;

/*
 * Fetch current switch states and power sensor values from HA.
 * Entity IDs come from HAM_SW_ENT_* and HAM_POWER_ENT_* in device_config.h.
 */
esp_err_t ha_ham_fetch(ha_ham_data_t *out);

/*
 * Toggle switch[switch_idx] (0=Radio PSU, 1=Shelly, 2=Palstar Amp).
 * Posts to /api/services/switch/toggle.
 */
esp_err_t ha_ham_toggle(int switch_idx);

#endif /* HAS_HAM */
