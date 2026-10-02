# Device: energy_4v3_lcd

| Field | Value |
|-------|-------|
| Location | Main electrical panel / living room |
| Board | Waveshare ESP32-S3-Touch-LCD-4.3 (non-B) |
| COM port | COM8 |
| HA host | 192.168.1.54:8123 |
| Circuits | 13 (Emporia Vue) |
| Timezone | PST8PDT (Pacific) |
| Weather | `weather.home_nws` (HA NWS integration, obs. station KVCV) |

## Screens

- **Weather** (boot default): current conditions, today's high/low, humidity,
  wind; next 12 hours; 5-day outlook. Refreshed every 15 min.
  **ENERGY ▸** (top right) opens the energy dashboard.
- **Energy**: the original dashboard. **◂ WEATHER** in the status bar
  returns; tap GRID or SOLAR for the 7-day chart.

Observation station is **KVCV** (Victorville, full ASOS). KAPV (Apple Valley,
closer) was tried first, but it is an AWOS that reports no sky/weather text, so
HA showed the condition as "Unknown". The forecast itself is the NWS grid
for the home location and doesn't depend on the station.

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
2. Flash: `.\tools\flash-device.ps1 -Device energy_4v3_lcd -Port COM8`
   (no boot-mode buttons needed; esptool auto-resets the board).
