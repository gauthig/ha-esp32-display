# ha-energy-display

ESP32-S3 energy dashboard pulling live data from Home Assistant via the
REST template API, displayed on a Waveshare **ESP32-S3-Touch-LCD-4.3**
(**non-B** variant — no CH422G, LEDC backlight on GPIO 2).

## Hardware

- **Board**: Waveshare ESP32-S3-Touch-LCD-4.3 (non-B), 800×480 RGB565
- **Non-B vs B**: no CH422G IO expander; backlight = LEDC PWM on GPIO 2
  (real dimming); touch/LCD RST = GPIO_NUM_NC (hardware POR)
- `direct_mode=true` is mandatory with `avoid_tearing` for the RGB LCD
- See `main/board.h` for all pin assignments

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
    0x10000 build\ha_energy_display.bin
```

**Step 3 — press RESET to boot normally.**

Or use the helper script (handles boot-mode prompt, build, and flash):

```powershell
.\tools\flash-device.ps1 -Device main_house -Port COM9
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
  main_house/          ← currently deployed (COM9, 192.168.1.54)
  NEW_DEVICE_TEMPLATE/ ← copy this to add a board
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
| `main/board.h/.c` | Non-B board init (I2C, RGB LCD, GT911, LEDC backlight) |
| `main/ha_client.h/.c` | HA template API fetch and parse |
| `main/ui.h/.c` | LVGL 9 dashboard |
| `main/main.c` | WiFi, SNTP, poll task |
| `partitions.csv` | 2 MB factory slot (binary ~1.5 MB; default 1 MB fails) |
| `sdkconfig.defaults` | ESP32-S3, 16 MB flash, octal PSRAM, LVGL fonts |

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
- **non-B board only**: do not flash this firmware on the B variant (CH422G
  board). See firefly-touch project for the B variant.
