# HA ESP32 Display

ESP32-S3 touch-panel firmware for Home Assistant. Supports multiple panel
types — currently an **Energy Monitor** and a **Ham Radio Control Panel** —
on a Waveshare 4.3" touchscreen. Adding a new panel type requires only a
device config file; the core WiFi/display/LVGL code is shared across all devices.

**Board**: Waveshare ESP32-S3-Touch-LCD-4.3 — **non-B variant only** (see
[INSTALLATION.md](INSTALLATION.md) for non-B vs B differences).

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

---

## Quick start — flash an existing panel

```powershell
# Energy Monitor
.\tools\flash-device.ps1 -Device energy_4v3_lcd -Port COM10

# Ham Controls
.\tools\flash-device.ps1 -Device ham_controls -Port COM10
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

| `DEVICE_TYPE` constant | UI compiled | HA client compiled |
|------------------------|-------------|-------------------|
| `DEVICE_TYPE_ENERGY` | `ui.c` (energy dashboard) | `ha_client.c` + `ha_history.c` |
| `DEVICE_TYPE_HAM_CONTROLS` | `ui_ham.c` (toggle panel) | `ha_ham.c` |

Core code — WiFi, SNTP, board init, LVGL port — is **always compiled**
and shared by all device types.

```
devices/
  energy_4v3_lcd/           ← DEVICE_TYPE_ENERGY    (Energy Monitor)
  ham_controls/         ← DEVICE_TYPE_HAM_CONTROLS (Ham Radio Panel)
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
  NEW_DEVICE_TEMPLATE/
    device_config.h      ← starter template with TODOs

main/
  device_types.h         ← DEVICE_TYPE_ENERGY / DEVICE_TYPE_HAM_CONTROLS constants
  ha_config.h            ← shim: includes device_types.h then device_config.h
  device_config.h        ← GITIGNORED — copied from devices/<name>/ by flash script
  secrets.h              ← GITIGNORED — copied from devices/<name>/ by flash script

  board.h/.c             ← non-B board init (RGB LCD, GT911 touch, LEDC backlight)
  main.c                 ← WiFi, SNTP, poll task (conditional on DEVICE_TYPE)

  ha_client.h/.c         ← HA template API (energy data) — DEVICE_TYPE_ENERGY only
  ha_history.h/.c        ← HA 7-day kWh history          — DEVICE_TYPE_ENERGY only
  ui.h/.c                ← LVGL energy dashboard          — DEVICE_TYPE_ENERGY only

  ha_ham.h/.c            ← HA switch toggle + power fetch — DEVICE_TYPE_HAM_CONTROLS only
  ui_ham.h/.c            ← LVGL ham radio toggle panel    — DEVICE_TYPE_HAM_CONTROLS only

tools/
  flash-device.ps1       ← select device, build, flash

partitions.csv           ← 2 MB factory slot (binary is ~1.5 MB)
sdkconfig.defaults       ← ESP32-S3, 16 MB flash, octal PSRAM, LVGL fonts
```
