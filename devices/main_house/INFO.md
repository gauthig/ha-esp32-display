# Device: main_house

| Field | Value |
|-------|-------|
| Location | Main electrical panel / living room |
| Board | Waveshare ESP32-S3-Touch-LCD-4.3 (non-B) |
| COM port | COM9 |
| HA host | 192.168.1.54:8123 |
| Circuits | 13 (Emporia Vue) |
| Timezone | PST8PDT (Pacific) |

## Circuits

| Index | Label | HA Entity |
|-------|-------|-----------|
| 0 | Pool | sensor.pool_power_minute_average |
| 1 | A/C | sensor.ac_power_minute_average |
| 2 | Oven | sensor.oven_power_minute_average |
| 3 | Range | sensor.range_power_minute_average |
| 4 | EV Charger | sensor.evcharger_power_minute_average |
| 5 | Laundry | sensor.laundry_room_power_minute_average |
| 6 | HVAC Fan | sensor.hvac_fan_power_minute_average |
| 7 | Garage | sensor.garage_power_minute_average |
| 8 | Fridge | sensor.fridge_power_minute_average |
| 9 | Computers | sensor.computers_power_minute_average |
| 10 | TV Room | sensor.tv_room_power_minute_average |
| 11 | Master BR | sensor.master_bedroom_power_minute_average |
| 12 | Upstairs BR | sensor.2_upstairs_bedrooms_power_minute_average |

## Setup

1. Copy `secrets.h.example` → `secrets.h` and fill in WiFi and HA token.
2. Flash: `.\tools\flash-device.ps1 -Device main_house -Port COM9`
