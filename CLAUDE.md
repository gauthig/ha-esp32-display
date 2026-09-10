# ha-energy-display

ESP32-S3 energy dashboard pulling live data from Home Assistant via the
REST template API, displayed on a Waveshare **ESP32-S3-Touch-LCD-4.3**
(**non-B** variant — no CH422G, LEDC backlight on GPIO 2).

## Hardware

Two board families, selected per-device (see Multi-device support):

- **Waveshare ESP32-S3-Touch-LCD-4.3 (non-B)** — 800×480 RGB565, landscape.
  `main/board.c` / `board.h`. No CH422G IO expander; backlight = LEDC PWM on
  GPIO 2 (real dimming); touch/LCD RST = GPIO_NUM_NC (hardware POR).
  `direct_mode=true` is mandatory with `avoid_tearing` for the RGB LCD.
- **Waveshare ESP32-S3-Touch-LCD-7B** — 1024×600 RGB565, mounted **portrait**
  (logical 600×1024). `main/board_7b.c` / `board_7b.h` +
  `main/ws_io_expander.c`. Used by `office_panel_7`. Ported from the
  bench-verified `firefly-touch` project. Key facts: IO expander is
  Waveshare's **IO_EXTENSION @ I2C 0x24** (NOT a CH422G); **EXIO6 =
  LCD_VDD_EN must be driven HIGH before RGB init** or the panel stays black;
  portrait uses esp_lvgl_port `sw_rotate` + `bb_mode` (NOT avoid_tearing —
  incompatible with software rotation).

See `main/board.h` / `main/board_7b.h` for pin assignments.

## Build

```powershell
. C:\esp\esp-idf\export.ps1
cd C:\Users\garre\OneDrive\Documents\GitHub\ha-energy-display
idf.py build
```

First build fetches managed components (LVGL 9.5.0, esp_lvgl_port 2.8.0,
esp_lcd_touch_gt911 1.2.0). Subsequent builds are incremental.

## Flash procedure (Windows — use esptool directly)

`idf.py flash` is unreliable on Windows without manual boot mode. Always use
the esptool command directly or `tools/flash-device.ps1`.

**Step 1 — enter boot mode on the device:**
1. Hold BOOT button
2. Tap RESET button
3. Release BOOT button

**Step 2 — flash (run immediately after step 1):**

```powershell
. C:\esp\esp-idf\export.ps1
python -m esptool --chip esp32s3 -p COM9 -b 460800 --before no_reset `
    write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m `
    0x0    build\bootloader\bootloader.bin `
    0x8000 build\partition_table\partition-table.bin `
    0x10000 build\ha_esp32_display.bin
```

**Step 3 — press RESET to boot normally.**

Or use the helper script (handles boot-mode prompt, build, and flash):

```powershell
.\tools\flash-device.ps1 -Device energy_4v3_lcd -Port COM9
```

## Monitor

```powershell
. C:\esp\esp-idf\export.ps1
idf.py -p COM9 monitor   # press RESET on device to see boot log; exit Ctrl+]
```

If you get `PermissionError 13` on the COM port, kill stale Python monitor processes:

```powershell
Stop-Process -Name python -Force -ErrorAction SilentlyContinue
```

## Multi-device support

Each board has a config directory under `devices/`. The build tool selects one
at a time. See [README.md](README.md) and [INSTALLATION.md](INSTALLATION.md).

```
devices/
  energy_4v3_lcd/      ← Energy Monitor  (DEVICE_TYPE_ENERGY,        4.3" non-B, COM9,  192.168.1.54)
  office_panel_7/      ← Office Panel 7  (DEVICE_TYPE_OFFICE_PANEL,   7B portrait, COM25, 192.168.1.54)
  NEW_DEVICE_TEMPLATE/ ← copy this to add a board
```

The dedicated `ham_controls` 4.3" panel (`DEVICE_TYPE_HAM_CONTROLS`) was
retired — its 4.3" board died and its three switches moved onto the office
panel. `ha_ham.c` lives on there via `HAS_HAM`.

### Device types

`device_config.h` sets `DEVICE_TYPE` to one of the constants in `device_types.h`.
`ha_config.h` then derives feature macros (`HAS_ENERGY` / `HAS_HAM` / `HAS_LIGHT`)
and source files guard on those, so a composite device can pull in several
modules:

| Constant | UI | Modules compiled |
|----------|----|------------------|
| `DEVICE_TYPE_ENERGY`       | Energy dashboard (`ui.c`)       | `ha_client.c` + `ha_history.c` |
| `DEVICE_TYPE_OFFICE_PANEL` | Office panel     (`ui_office.c`) | `ha_ham.c` + `ha_light.c` + `ha_client.c` + `ha_history.c`; board = `board_7b.c` |

Switching devices changes which UI and data-fetchers compile in. Core code
(WiFi, SNTP) stays static; `board.c` (4.3") and `board_7b.c` (7B) are each
guarded by `DEVICE_TYPE` so only one `board_display_init()` is linked.

### Office Panel 7 device

7B portrait combo panel. HOME screen: grouped **Office Fan Lights** card
(state-aware bulb graphic; tap = toggle both `light.office_fan_light_1/2`,
long-press = brightness slider + colour swatch popup applied to both), three
HAM switch cards (`switch.radio_power_supply`, `switch.shelly1g4_a085e3c0f2c0`,
`switch.palstar_amp` + their power sensors), and a bottom nav bar to the
**ENERGY** screen (portrait re-layout of the `energy_4v3_lcd` dashboard,
reusing `ha_client.c` / `ha_history.c` unchanged).

Flash:
```powershell
.\tools\flash-device.ps1 -Device office_panel_7 -Port COM25
```

## Credentials (never commit)

| File | Contents | Gitignored |
|------|----------|------------|
| `main/secrets.h` | Active WiFi + HA token | ✓ |
| `main/device_config.h` | Active device config | ✓ |
| `devices/*/secrets.h` | Per-device secrets | ✓ |
| `devices/*/device_config.h` | Per-device config | committed |

## Architecture

All device-specific config (HA host, entity IDs, circuit list) lives in
`devices/<name>/device_config.h`. The source files use macros from that file:

- `CIRCUIT_NAMES_INIT` → `s_names[]` in ha_client.c
- `CIRCUIT_ENTITIES_TEMPLATE` → HA template POST body in ha_client.c
- `ENT_*` macros → individual sensor entity IDs
- `HA_NUM_CIRCUITS` → array sizes in ha_client.h and ha_data_t

`main/ha_config.h` is a one-line shim: `#include "device_config.h"`.

## Key files

| File | Purpose |
|------|---------|
| `main/board.h/.c` | 4.3" non-B board init (I2C, RGB LCD, GT911, LEDC backlight) |
| `main/board_7b.h/.c` | 7B board init (IO_EXTENSION, RGB LCD, GT911, portrait sw_rotate) |
| `main/ws_io_expander.h/.c` | Waveshare IO_EXTENSION @0x24 driver (7B) |
| `main/ha_client.h/.c` | HA template API fetch and parse (energy) |
| `main/ha_ham.h/.c` | HA switch state + toggle (ham + office) |
| `main/ha_light.h/.c` | HA grouped-light state + brightness/colour service calls (office) |
| `main/ui.h/.c` | LVGL 9 energy dashboard (4.3" landscape) |
| `main/ui_office.h/.c` | LVGL office panel: home + light popup + portrait energy screen |
| `main/main.c` | WiFi, SNTP, poll task (branches on DEVICE_TYPE) |
| `partitions.csv` | 2 MB factory slot (binaries ~1.5 MB; default 1 MB fails) |
| `sdkconfig.defaults` | ESP32-S3, 16 MB flash, octal PSRAM, LVGL fonts (shared by all devices) |

## Gotchas

- **GPIO 2 backlight warning**: `lcd_panel.rgb: GPIO 2 is not usable` appears
  on every boot — benign. LEDC claims GPIO 2; RGB driver flags it. Backlight
  stays at full brightness (fine for wall-mounted display).
- **GT911 address**: if touch does not respond, change `dev_addr` to
  `ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP` (0x14) in `board.c`.
- **Partition**: custom 2 MB factory; `partitions.csv` + `CONFIG_PARTITION_TABLE_CUSTOM=y`
  in `sdkconfig.defaults` must not be removed.
- **sdkconfig edits**: if you need to change partition config without a full
  clean, edit the `CONFIG_PARTITION_TABLE_*` lines in `sdkconfig` directly.
- **4.3" firmware is non-B only**: the `energy_4v3_lcd` build must not go on a
  4.3B (CH422G) board.
- **7B migration** (`office_panel_7`): the 7B is NOT a CH422G board — it uses
  Waveshare's IO_EXTENSION @0x24 (`ws_io_expander.c`). EXIO6 = LCD_VDD_EN
  must be HIGH before RGB init. Portrait is `sw_rotate` + `bb_mode` in
  `board_7b.c`; `avoid_tearing` / `full_refresh` cannot be used with software
  rotation. If the portrait frame flickers, tune `PCLK_HZ` / `BOUNCE_LINES` /
  `DRAW_LINES` at the top of `board_7b.c` (bench-iterate). Flip
  `BOARD_7B_ROTATION` to `_270` if the wall mount is upside-down.
- **shared `sdkconfig.defaults`**: all three devices build from it. The 7B
  build currently reuses the 4.3" font/PSRAM/partition config unchanged; keep
  any 7B-specific tuning additive or gated so the 4.3" builds don't regress.
