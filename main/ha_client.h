#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "ha_config.h"   /* device_types.h + device_config.h */

#if DEVICE_TYPE == DEVICE_TYPE_ENERGY

typedef struct {
    float grid_kwh_today;      /* energy imported from grid today (kWh) */
    float export_kwh_today;    /* energy exported to grid today (kWh) */
    float solar_kwh_today;     /* solar energy generated today (kWh) */
    float solar_power_w;       /* current solar generation (W) */
    float total_power_w;       /* total home consumption (W) */
    float net_grid_w;          /* net from grid: >0 importing, <0 exporting */
    float circuit_power[HA_NUM_CIRCUITS];
    bool  valid;               /* true once at least one fetch succeeded */
} ha_data_t;

const char **ha_circuit_names(void);
esp_err_t ha_client_fetch(ha_data_t *out);

#endif /* DEVICE_TYPE_ENERGY */
