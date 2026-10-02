# Installation Guide

## Prerequisites

- **ESP-IDF v5.3+** installed at `C:\esp\esp-idf`
  (tested with v5.5.5; activate with `. C:\esp\esp-idf\export.ps1`)
- **Python** in the ESP-IDF virtualenv (installed automatically with ESP-IDF)
- **esptool** (included with ESP-IDF)
- **Windows PowerShell** (the flash script is `.ps1`)
- **USB driver** for CH343P/CH340 (Waveshare board UART chip)

---

## Hardware: which board per device

| Device | Board | Notes |
|--------|-------|-------|
| `energy_4v3_lcd` | Waveshare ESP32-S3-Touch-LCD-4.3 **non-B** | `board.c` |
| `office_panel_7` | Waveshare ESP32-S3-Touch-LCD-7B | `board_7b.c` + `ws_io_expander.c`, portrait |

### 4.3" non-B vs B

The 4.3" builds target the **non-B** board. The two look identical but differ:

| Feature | non-B (this project) | B variant |
|---------|----------------------|-----------|
| Backlight | LEDC PWM on GPIO 2 | CH422G IO expander |
| Reset lines | Hardware POR | Routed through CH422G |
| USB-C | UART (CH343P) + native USB | Same |

Using B-variant firmware on non-B (or vice versa) produces a black screen.

### 7B (`office_panel_7`)

Different panel (1024×600), a **Waveshare IO_EXTENSION @ I2C 0x24** (not a
CH422G), and an extra `EXIO6 = LCD_VDD_EN` that must be HIGH before RGB init.
Mounted **portrait** — the firmware runs a logical 600×1024 via esp_lvgl_port
software rotation. All handled in `board_7b.c`; its top-of-file tuning knobs
(`PCLK_HZ`, `BOUNCE_LINES`, `DRAW_LINES`) are the bench dials if the portrait
frame flickers. `BOARD_7B_ROTATION` flips 90°↔270° for the mount direction.

## USB ports on the board

The board has **two USB-C ports**:

| Port | Chip | Use |
|------|------|-----|
| UART USB-C (marked "UART") | CH343P → COM port | Flash and serial monitor |
| Native USB-C (marked "USB") | ESP32-S3 internal | Not used in this project |

**Always use the UART port for flashing and monitoring.**

---

## Updating an existing panel

This is the everyday workflow — you already have a device configured and just
want to reflash it (new firmware, changed entities, etc.).

### Step 1 — Edit config if needed

Device configs live in `devices/<name>/device_config.h`:

| Panel | Config file |
|-------|------------|
| Weather + Energy Monitor | `devices\energy_4v3_lcd\device_config.h` |
| Office Panel 7 | `devices\office_panel_7\device_config.h` |

### Step 2 — Boot mode (usually not needed)

esptool resets the ESP32-S3 into download mode over USB by itself
(`--before default_reset`), so normally you can skip this step. Only if
flashing fails with `No serial data received`, enter boot mode manually:

1. Hold the **BOOT** button (GPIO0)
2. Tap the **RESET** (RST/EN) button
3. Release **BOOT**

### Step 3 — Build and flash

```powershell
# Weather + Energy Monitor
.\tools\flash-device.ps1 -Device energy_4v3_lcd -Port COM8

# Office Panel 7
.\tools\flash-device.ps1 -Device office_panel_7 -Port COM25
```

Replace the port with whatever the board enumerated as. To find it:

```powershell
[System.IO.Ports.SerialPort]::GetPortNames()
```

### Step 4 — Press RESET

After `esptool` says `Hard resetting via RTS pin...` the board boots
automatically. If not, press **RESET** once.

### Troubleshooting flash failures

| Error | Fix |
|-------|-----|
| `Could not open COMx` | Port not yet assigned — check Device Manager |
| `PermissionError 13` | Another process owns the port — run `Stop-Process -Name python -Force -ErrorAction SilentlyContinue` |
| `No serial data received` (auto-reset) | Enter boot mode manually: hold BOOT → tap RESET → release BOOT, then flash with `--before no_reset` |
| `No serial data received` (`--before no_reset`) | The board isn't in boot mode. Use `--before default_reset` to let esptool reset the chip, or re-enter boot mode |
| `idf.py` not recognized / `export.ps1` fails with "expression after '.'" | ESP-IDF's Python venv lost its base Python (e.g. after a Python upgrade moved it). Fix `home`/`executable` in `%USERPROFILE%\.espressif\python_env\idf5.5_py3.14_env\pyvenv.cfg`, or re-run the ESP-IDF installer |
| Build output identical after switching devices | `Copy-Item` kept an old timestamp on `main\device_config.h`. Touch it (`(Get-Item main\device_config.h).LastWriteTime = Get-Date`) and rebuild |

---

## Monitoring serial output

```powershell
. C:\esp\esp-idf\export.ps1
idf.py -p COM8 monitor
```

Press **RESET** on the board to see the full boot sequence. Exit with `Ctrl+]`.

Expected boot log — Weather + Energy Monitor:

```
I ui: UI ready
I main: startup complete — device: Main House
I ha_client: grid 27.4 kWh | net 434 W | solar 215 W
I ha_weather: 61 F Clear | 12 hourly / 5 daily (cur=1 hr=1 day=1)
```

Expected boot log — Office Panel 7:

```
I board_7b: up: 1024x600 RGB565 → 600x1024 portrait, GT911, LVGL core 1
I ui_office: Office Panel UI ready (600x1024 portrait)
I main: startup complete — device: Office Panel 7
I ha_ham:   sw=[1,0,1] pwr=[145.0,320.0]
I ha_light: light: on bri=72% rgb=255,214,170
I ha_client: grid 27.4 kWh | net 434 W | solar 215 W
```

---

## Setting up a device for the first time

### 1. Clone the repo

```powershell
git clone https://github.com/youruser/ha-esp32-display
cd ha-esp32-display
```

### 2. Create secrets.h

`secrets.h` holds your WiFi credentials and Home Assistant token.
**It is gitignored — never commit it.**

```powershell
# e.g. for the Energy Monitor (repeat per device dir you build)
copy devices\energy_4v3_lcd\secrets.h.example devices\energy_4v3_lcd\secrets.h
```

Edit each `secrets.h`:

```c
#pragma once

#define WIFI_SSID     "your_network_name"
#define WIFI_PASSWORD "your_wifi_password"

// Home Assistant → Profile → Long-Lived Access Tokens → Create Token
#define HA_TOKEN      "eyJhbGci..."
```

### 3. Build and flash

```powershell
# Weather + Energy Monitor
.\tools\flash-device.ps1 -Device energy_4v3_lcd -Port COM8

# Office Panel 7
.\tools\flash-device.ps1 -Device office_panel_7 -Port COM25
```

The script:
1. Copies `devices/<name>/device_config.h` → `main/device_config.h`
2. Copies `devices/<name>/secrets.h` → `main/secrets.h`
3. Activates ESP-IDF and runs `idf.py build`
4. Flashes with `esptool`

---

## Configuring the Energy Monitor

All settings live in `devices/energy_4v3_lcd/device_config.h`.

```c
#define DEVICE_TYPE  DEVICE_TYPE_ENERGY   // selects energy UI and HA client
#define DEVICE_NAME  "Main House"

#define HA_HOST      "192.168.1.54"
#define HA_PORT      8123
#define HA_POLL_INTERVAL_MS  10000
#define LOCAL_TZ     "PST8PDT,M3.2.0,M11.1.0"

// Main energy sensor entity IDs (find in HA Developer Tools → States)
#define ENT_GRID_KWH    "sensor.mainsfromgrid_energy_today"
#define ENT_SOLAR_W     "sensor.solar_line_1_power_minute_average"
#define ENT_SOLAR_KWH   "sensor.solar_energy_today"
#define ENT_TOTAL_W     "sensor.total_home_power"
#define ENT_NET_W       "sensor.net_grid_power"

// Circuit list (shown in bottom panel)
#define HA_NUM_CIRCUITS  13
#define CIRCUIT_NAMES_INIT  "Pool", "A/C", "Oven", ...
#define CIRCUIT_ENTITIES_TEMPLATE \
    "{{ states('sensor.pool_power') }}|" \
    ...
```

### Weather screen (optional, boot default)

Defining `ENT_WEATHER` turns on `HAS_WEATHER`: the device boots to a weather
screen (now / next 12 hours / 5-day) with an **ENERGY ▸** button, and the
energy screen gets a **◂ WEATHER** button.

```c
#define ENT_WEATHER       "weather.home_nws"   // HA weather entity
#define WEATHER_LOCATION  "Apple Valley"       // header label
#define WEATHER_STATION   "KVCV"               // footer label (optional)
```

The entity must support **hourly** and **twice-daily** forecasts. The NWS
integration does (Settings → Devices & services → Add integration → National
Weather Service, then enter your lat/long and an observation station). Pick a
full **ASOS** station: some small-airport **AWOS** stations, such as KAPV, report
no sky or weather text, and HA then shows the current condition as "Unknown".
Remove the three lines to build the energy-only firmware.

### No solar?

Set these to point at your total consumption sensor:

```c
#define ENT_SOLAR_W   ENT_TOTAL_W
#define ENT_SOLAR_KWH "sensor.dummy_kwh"
#define ENT_NET_W     ENT_TOTAL_W
```

---

## Configuring the Office Panel 7

Settings live in `devices/office_panel_7/device_config.h`. This device is a
composite — `DEVICE_TYPE_OFFICE_PANEL` compiles the light, ham, **and** energy
modules — so its config carries all three sets of macros.

```c
#define DEVICE_TYPE       DEVICE_TYPE_OFFICE_PANEL  // office UI + all data modules
#define DEVICE_NAME       "Office Panel 7"
#define BOARD_VARIANT_7B  1                          // selects board_7b.c

#define HA_HOST           "192.168.1.54"
#define HA_PORT           8123
#define HA_POLL_INTERVAL_MS 15000
#define LOCAL_TZ          "PST8PDT,M3.2.0,M11.1.0"

/* Grouped light — both entities are driven together */
#define LIGHT_ENT_0  "light.office_fan_light_1"
#define LIGHT_ENT_1  "light.office_fan_light_2"

/* HAM switches — the ham radio station switches (tap a card to toggle) */
#define HAM_SW_ENT_0 "switch.radio_power_supply"
#define HAM_SW_ENT_1 "switch.shelly1g4_a085e3c0f2c0"
#define HAM_SW_ENT_2 "switch.palstar_amp"
#define HAM_SW_NAMES_INIT "Radio PSU", "Shelly", "Palstar Amp"
#define HAM_POWER_ENT_0 "sensor.radio_power_supply_power"
#define HAM_POWER_ENT_1 "sensor.palstar_amp_power"

/* Energy screen — ENT_* + HA_NUM_CIRCUITS + CIRCUIT_* (see Energy Monitor) */
```

The two light entities are toggled/dimmed/coloured as one: tapping the card
calls `light.toggle` on both; the long-press popup calls `light.turn_on` with
`brightness_pct` or `rgb_color` on both.

---

## Adding a new device

### Using an existing panel type

If the new device reuses an existing panel type (e.g. another Energy Monitor):

1. Copy the matching template:
   ```powershell
   Copy-Item -Recurse devices\energy_4v3_lcd devices\garage
   ```

2. Edit `devices\<name>\device_config.h` — update `DEVICE_NAME`, `HA_HOST`,
   entity IDs, etc. Keep `DEVICE_TYPE` the same.

3. Create secrets:
   ```powershell
   copy devices\<name>\secrets.h.example devices\<name>\secrets.h
   # Edit with WiFi and HA token
   ```

4. Update `INFO.md` with location and COM port.

5. Flash:
   ```powershell
   .\tools\flash-device.ps1 -Device <name> -Port COM<X>
   ```

### Adding a brand-new panel type

Follow these steps to add a panel type that doesn't exist yet (e.g., an HVAC
controller). For an optional screen on an existing type, prefer the
`HAS_WEATHER` pattern instead: an opt-in macro in `device_config.h` gates a new
`HAS_*` feature in `ha_config.h`.

1. **Register the new type** in `main/device_types.h`:
   ```c
   #define DEVICE_TYPE_MY_PANEL  4   // 1-3 are taken
   ```
   If it reuses existing data modules, add it to the feature-macro block in
   `main/ha_config.h` (e.g. `#define HAS_ENERGY 1`) instead of duplicating a
   fetcher. A new board needs its own `DEVICE_TYPE`-guarded `board_*.c`
   implementing the `board.h` API.

2. **Write the HA client** — `main/ha_mypanel.c` and `main/ha_mypanel.h`:
   - Fetches data from HA REST API (`/api/template` or `/api/states/<entity>`)
   - Exposes a `ha_mypanel_data_t` struct and `ha_mypanel_fetch()` function
   - Wrap the entire implementation in `#if DEVICE_TYPE == DEVICE_TYPE_MY_PANEL`

3. **Write the LVGL UI** — `main/ui_mypanel.c` and `main/ui_mypanel.h`:
   - Builds the screen with `ui_mypanel_init()`
   - Refreshes data with `ui_mypanel_update()`
   - Wrap in `#if DEVICE_TYPE == DEVICE_TYPE_MY_PANEL`

4. **Register the source files** in `main/CMakeLists.txt` — append your
   `ha_mypanel.c` / `ui_mypanel.c` (and any `board_*.c`) to the `SRCS` list.
   Every `.c` is always compiled; the `#if` guards make it empty for other
   device types.

5. **Wire up `main.c`** — add an `#elif` block for the new type:
   ```c
   #elif DEVICE_TYPE == DEVICE_TYPE_MY_PANEL
   #include "ha_mypanel.h"
   #include "ui_mypanel.h"
   ```
   Then add matching blocks in `app_main()` (UI init) and `ha_poll_task()` (data fetch).

6. **Create the device folder**:
   ```powershell
   Copy-Item -Recurse devices\NEW_DEVICE_TEMPLATE devices\my_panel
   ```
   Set `DEVICE_TYPE DEVICE_TYPE_MY_PANEL` in `device_config.h` and fill in
   any macros your new HA client expects.

7. **Flash and test**:
   ```powershell
   .\tools\flash-device.ps1 -Device my_panel -Port COM<X>
   idf.py -p COM<X> monitor
   ```

---

## Reference: timezone strings

| Location | TZ string |
|----------|-----------|
| US Pacific | `PST8PDT,M3.2.0,M11.1.0` |
| US Mountain | `MST7MDT,M3.2.0,M11.1.0` |
| US Central | `CST6CDT,M3.2.0,M11.1.0` |
| US Eastern | `EST5EDT,M3.2.0,M11.1.0` |

## Reference: finding HA entity IDs

In Home Assistant: **Developer Tools → States** → search for your sensor name.
Emporia Vue circuits end in `_power_minute_average`. Switch entities appear
under the `switch.*` domain. Power sensor entities appear under `sensor.*`.

## Reference: partition table

The firmware is ~1.5 MB. The project uses a custom partition table
(`partitions.csv`) with a 2 MB factory slot. The default ESP-IDF 1 MB
partition **will not work** — do not delete `partitions.csv` or remove
`CONFIG_PARTITION_TABLE_CUSTOM=y` from `sdkconfig.defaults`.
