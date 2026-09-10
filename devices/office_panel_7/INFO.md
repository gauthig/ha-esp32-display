# Device: office_panel_7

| Field | Value |
|-------|-------|
| Location | Office wall (PORTRAIT mount) |
| Board | Waveshare ESP32-S3-Touch-LCD-7B |
| Panel | 1024×600 RGB565, rotated 90° → 600×1024 portrait |
| COM port | COM25 |
| HA host | 192.168.1.54:8123 |
| Device type | `DEVICE_TYPE_OFFICE_PANEL` (ham + light + energy) |
| Timezone | PST8PDT (Pacific) |

## Screens

- **HOME** — Office Fan Lights card (state-aware bulb graphic; tap = toggle
  both, long-press = brightness/colour popup), three HAM switch cards, and a
  bottom **ENERGY** nav bar.
- **ENERGY** — portrait re-layout of the `energy_4v3_lcd` dashboard (same
  `ha_client` / `ha_history` data). Tap a card → 7-day grid+solar chart.

## Controls

| Control | HA entities | Behaviour |
|---------|-------------|-----------|
| Office Fan Lights | `light.office_fan_light_1`, `light.office_fan_light_2` | Grouped. Tap toggles both. Long-press → popup: brightness slider + 8 colour swatches, applied to both. |
| Radio PSU | `switch.radio_power_supply` (+ `sensor.radio_power_supply_power`) | Tap toggles |
| Shelly | `switch.shelly1g4_a085e3c0f2c0` | Tap toggles |
| Palstar Amp | `switch.palstar_amp` (+ `sensor.palstar_amp_power`) | Tap toggles |

## Board notes (Waveshare 7B)

- IO expander is Waveshare's **IO_EXTENSION @ 0x24** (`ws_io_expander.c`), NOT
  a CH422G.
- **EXIO6 = LCD_VDD_EN must go HIGH before RGB init** or the panel stays black
  with the backlight lit. Handled in `board_7b.c`.
- Portrait uses esp_lvgl_port software rotation (`sw_rotate` + `bb_mode`).
  If the frame flickers, see the tuning knobs at the top of `board_7b.c`
  (`PCLK_HZ`, `BOUNCE_LINES`, `DRAW_LINES`).
- Flip `BOARD_7B_ROTATION` to `LV_DISPLAY_ROTATION_270` if the wall mount
  comes out upside-down.

## Setup

1. `main/secrets.h` (shared, `ghome` WiFi + HA token) is reused — or copy
   `secrets.h.example` → `secrets.h` here for a per-device override.
2. Flash: `.\tools\flash-device.ps1 -Device office_panel_7 -Port COM25`
