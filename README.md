# HA ESP32 Display

ESP32-S3 touch-panel firmware for Home Assistant. Supports multiple panel
types — currently an **Energy Monitor**, a **Ham Radio Control Panel**, and a
combined **Office Panel 7** — across two Waveshare board families. Adding a
new panel type requires only a device config file; the core WiFi/display/LVGL
code is shared across all devices.

**Boards**:
- Waveshare ESP32-S3-Touch-LCD-4.3 — **non-B variant only** — `energy_4v3_lcd`,
  `ham_controls` (see [INSTALLATION.md](INSTALLATION.md) for non-B vs B).
- Waveshare ESP32-S3-Touch-LCD-7B — 1024×600, mounted portrait — `office_panel_7`.

---

## Current panels

### Energy Monitor (`devices/energy_4v3_lcd/`)

| Area | Data |
|------|------|
| Top row | Grid kWh today · Net grid watts (import or solar export) · Solar kWh + watts |
| Middle | Highest-draw circuit name and wattage |
| Bottom | Top-5 circuits by current draw with relative power bars |
| Header | Clock, connection status dot |
| Tap a stat card | 7-day line chart (grid or solar) |

### Ham Radio Control Panel (`devices/ham_controls/`)

| Area | Data |
|------|------|
| Three tap-to-toggle cards | Radio PSU · Shelly · Palstar Amp |
| Radio PSU card | Switch state (ON/OFF) + live power draw (W) |
| Palstar Amp card | Switch state (ON/OFF) + live power draw (W) |
| Header | Clock, connection status dot |
| First tap (after dim) | Wakes display only — no accidental toggle |

### Office Panel 7 (`devices/office_panel_7/`)

Waveshare **7B** (1024×600) mounted **portrait** (600×1024). A combo panel:

| Area | Data |
|------|------|
| Office Fan Lights card | Grouped `light.office_fan_light_1` + `_2`. State-aware bulb graphic. Tap = toggle both. Long-press = popup with brightness slider + 8 colour swatches, applied to both. |
| Three HAM switch cards | Radio PSU · Shelly · Palstar Amp (same entities/behaviour as Ham Controls) |
| Bottom nav bar | Opens the **ENERGY** screen — portrait re-layout of the Energy Monitor dashboard (tap a card → 7-day chart) |
| Header | Clock, connection status dot |

---

## Quick start — flash an existing panel

```powershell
# Energy Monitor
.\tools\flash-device.ps1 -Device energy_4v3_lcd -Port COM10

# Ham Controls
.\tools\flash-device.ps1 -Device ham_controls -Port COM10

# Office Panel 7
.\tools\flash-device.ps1 -Device office_panel_7 -Port COM25
```

The script copies the device config, builds, and flashes in one step.
See [INSTALLATION.md](INSTALLATION.md) for prerequisites and boot-mode steps.

---

## Monitor serial output (live logs)

```powershell
. C:\esp\esp-idf\export.ps1

# Energy Monitor
idf.py -p COM10 monitor

# Ham Controls
idf.py -p COM10 monitor
```

Press **RESET** on the board to see the full boot log. Exit with `Ctrl+]`.

> **Tip — port busy?** Kill any stale monitor session first:
> `Stop-Process -Name python -Force -ErrorAction SilentlyContinue`

---

## Switching between panels on the same device

The two device configs use the same hardware. To swap which firmware is on
the board, simply flash a different device:

```powershell
# Load Energy Monitor onto the device
.\tools\flash-device.ps1 -Device energy_4v3_lcd -Port COM10

# Load Ham Controls onto the device
.\tools\flash-device.ps1 -Device ham_controls -Port COM10
```

---

## How the multi-device system works

Each device folder contains a `device_config.h` that sets `DEVICE_TYPE`.
That constant gates which UI and HA client compile into the binary:

`ha_config.h` then derives feature macros (`HAS_ENERGY` / `HAS_HAM` /
`HAS_LIGHT`); source files guard on those so a composite device compiles
several modules at once.

| `DEVICE_TYPE` constant | UI compiled | Data modules compiled |
|------------------------|-------------|-----------------------|
| `DEVICE_TYPE_ENERGY` | `ui.c` (energy dashboard) | `ha_client.c` + `ha_history.c` |
| `DEVICE_TYPE_HAM_CONTROLS` | `ui_ham.c` (toggle panel) | `ha_ham.c` |
| `DEVICE_TYPE_OFFICE_PANEL` | `ui_office.c` (7B portrait combo) | `ha_ham.c` + `ha_light.c` + `ha_client.c` + `ha_history.c` |

Core code — WiFi, SNTP, LVGL port — is **always compiled** and shared. The
board layer is split: `board.c` (4.3" non-B) and `board_7b.c` (7B) are each
`DEVICE_TYPE`-guarded so exactly one `board_display_init()` links.

```
devices/
  energy_4v3_lcd/       ← DEVICE_TYPE_ENERGY        (Energy Monitor, 4.3" non-B)
  ham_controls/         ← DEVICE_TYPE_HAM_CONTROLS  (Ham Radio Panel, 4.3" non-B)
  office_panel_7/       ← DEVICE_TYPE_OFFICE_PANEL  (Office Panel 7, 7B portrait)
  NEW_DEVICE_TEMPLATE/  ← copy this to add a new panel
```

---

## Adding a new panel

See [INSTALLATION.md — Adding a new device](INSTALLATION.md#adding-a-new-device)
for the full step-by-step. The short version:

1. Copy `devices\NEW_DEVICE_TEMPLATE\` → `devices\<your_name>\`
2. Set `#define DEVICE_TYPE` in `device_config.h` (pick an existing type or add a new one)
3. Copy `secrets.h.example` → `secrets.h` and fill in credentials
4. Flash: `.\tools\flash-device.ps1 -Device <your_name> -Port COM<X>`

To add a **brand-new panel type** (new UI + new HA client):
- Add the constant to `main/device_types.h`
- Write `main/ha_<type>.c/.h` (HA REST fetch)
- Write `main/ui_<type>.c/.h` (LVGL screen)
- Add both `.c` files to `main/CMakeLists.txt`
- Add `#if DEVICE_TYPE == ...` guards in `main/main.c`

---

## Project layout

```
devices/
  energy_4v3_lcd/
    device_config.h      ← HA host, timezone, circuit entities, DEVICE_TYPE_ENERGY
    secrets.h            ← GITIGNORED — WiFi + HA token
    secrets.h.example    ← committed template
    INFO.md              ← location, COM port, circuit table
  ham_controls/
    device_config.h      ← HA host, timezone, switch + power entities, DEVICE_TYPE_HAM_CONTROLS
    secrets.h            ← GITIGNORED — WiFi + HA token
    secrets.h.example    ← committed template
    INFO.md              ← location, entity table
  office_panel_7/
    device_config.h      ← 7B board, light + ham + energy entities, DEVICE_TYPE_OFFICE_PANEL
    secrets.h.example    ← committed template
    INFO.md              ← location, controls, 7B board notes
  NEW_DEVICE_TEMPLATE/
    device_config.h      ← starter template with TODOs

main/
  device_types.h         ← DEVICE_TYPE_* constants
  ha_config.h            ← shim: device_types.h + device_config.h, then HAS_* feature macros
  device_config.h        ← GITIGNORED — copied from devices/<name>/ by flash script
  secrets.h              ← GITIGNORED — copied from devices/<name>/ by flash script

  board.h/.c             ← 4.3" non-B board init (RGB LCD, GT911, LEDC backlight)
  board_7b.h/.c          ← 7B board init (IO_EXTENSION, RGB LCD, GT911, portrait sw_rotate)
  ws_io_expander.h/.c    ← Waveshare IO_EXTENSION @0x24 driver (7B)
  main.c                 ← WiFi, SNTP, poll task (branches on DEVICE_TYPE)

  ha_client.h/.c         ← HA template API (energy data)     — HAS_ENERGY
  ha_history.h/.c        ← HA 7-day kWh history              — HAS_ENERGY
  ui.h/.c                ← LVGL energy dashboard (landscape) — DEVICE_TYPE_ENERGY

  ha_ham.h/.c            ← HA switch toggle + power fetch    — HAS_HAM
  ui_ham.h/.c            ← LVGL ham radio toggle panel       — DEVICE_TYPE_HAM_CONTROLS

  ha_light.h/.c          ← HA grouped-light state + brightness/colour — HAS_LIGHT
  ui_office.h/.c         ← LVGL office panel: home + light popup + portrait energy — DEVICE_TYPE_OFFICE_PANEL

tools/
  flash-device.ps1       ← select device, build, flash

partitions.csv           ← 2 MB factory slot (binaries are ~1.5 MB)
sdkconfig.defaults       ← ESP32-S3, 16 MB flash, octal PSRAM, LVGL fonts (shared by all devices)
```
