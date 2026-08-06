# Device: NEW_DEVICE_TEMPLATE

| Field | Value |
|-------|-------|
| Location | TODO |
| Board | Waveshare ESP32-S3-Touch-LCD-4.3 (non-B) |
| COM port | TODO |
| HA host | TODO |
| Circuits | TODO |
| Timezone | TODO |

## Circuits

| Index | Label | HA Entity |
|-------|-------|-----------|
| 0 | Circuit 1 | sensor.circuit1_power_minute_average |
| 1 | Circuit 2 | sensor.circuit2_power_minute_average |
| 2 | Circuit 3 | sensor.circuit3_power_minute_average |

## Setup

1. Copy `secrets.h.example` → `secrets.h` and fill in WiFi and HA token.
2. Flash: `.\tools\flash-device.ps1 -Device <name> -Port COM<X>`
