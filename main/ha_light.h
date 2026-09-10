/*
 * ha_light.h — Home Assistant client for a grouped light control.
 *
 * Reads the state of one representative light entity (LIGHT_ENT_0) via the
 * /api/template endpoint and drives BOTH configured entities together via
 * the light.turn_on / light.toggle services. The two Office Fan Lights move
 * as one control.
 *
 * Compiled only when HAS_LIGHT is defined (DEVICE_TYPE_OFFICE_PANEL).
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "ha_config.h"

#if defined(HAS_LIGHT)

typedef struct {
    bool    on;               /* true = at least the lead entity is on      */
    int     brightness_pct;   /* 1..100, or -1 if unknown/off               */
    uint8_t r, g, b;          /* last known rgb_color; 255,255,255 if none  */
    bool    valid;            /* true once at least one fetch succeeded     */
} ha_light_data_t;

/* Fetch on/off + brightness + rgb for the grouped light. */
esp_err_t ha_light_fetch(ha_light_data_t *out);

/* Toggle both entities together (light/toggle). */
esp_err_t ha_light_toggle(void);

/* Set brightness on both entities, percent 1..100 (light/turn_on). */
esp_err_t ha_light_set_brightness(int percent);

/* Set rgb_color on both entities (light/turn_on). */
esp_err_t ha_light_set_rgb(uint8_t r, uint8_t g, uint8_t b);

#endif /* HAS_LIGHT */
