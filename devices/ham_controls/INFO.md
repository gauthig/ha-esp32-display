# Ham Controls

| Field     | Value                                |
|-----------|--------------------------------------|
| Device    | Waveshare ESP32-S3-Touch-LCD-4.3 (non-B) |
| COM port  | COM9                                 |
| HA host   | 192.168.1.54:8123                    |
| Poll rate | 15 s                                 |
| Type      | DEVICE_TYPE_HAM_CONTROLS             |

## UI

Three large tap-to-toggle switch cards covering the 800×480 display:

| Card | Switch entity | Power sensor |
|------|--------------|--------------|
| Radio PSU  | `switch.radio_power_supply` | `sensor.radio_power_supply_power` |
| Shelly     | `switch.shelly1g4_a085e3c0f2c0` | — |
| Palstar Amp | `switch.palstar_amp` | `sensor.palstar_amp` |

- Green card / "ON" = switch is on.
- Red card / "OFF" = switch is off.
- First tap after 5-min dim wakes the display only (no toggle).
- Display auto-dims after 5 minutes of inactivity.

## Flash

```powershell
.\tools\flash-device.ps1 -Device ham_controls -Port COM9
```
