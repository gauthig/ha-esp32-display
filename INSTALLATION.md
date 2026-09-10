# Installation Guide

## Prerequisites

- **ESP-IDF v5.3+** installed at `C:\esp\esp-idf`
  (tested with v5.5.5; activate with `. C:\esp\esp-idf\export.ps1`)
- **Python** in the ESP-IDF virtualenv (installed automatically with ESP-IDF)
- **esptool** (included with ESP-IDF)
- **Windows PowerShell** (the flash script is `.ps1`)
- **USB driver** for CH343P/CH340 (Waveshare board UART chip)

---

## Hardware: non-B vs B

This project targets the **non-B** board. The two look identical but differ:

| Feature | non-B (this project) | B variant |
|---------|----------------------|-----------|
| Backlight | LEDC PWM on GPIO 2 | CH422G IO expander |
| Reset lines | Hardware POR | Routed through CH422G |
| USB-C | UART (CH343P) + native USB | Same |

Using B-variant firmware on non-B (or vice versa) produces a black screen.

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

Device configs live in `devices/<name>/device_config.h`. For the two current panels:

| Panel | Config file |
|-------|------------|
| Energy Monitor | `devices\energy_4v3_lcd\device_config.h` |
| Ham Controls | `devices\ham_controls\device_config.h` |

### Step 2 — Put device in boot mode

1. Hold the **BOOT** button (GPIO0)
2. Tap the **RESET** (RST/EN) button
3. Release **BOOT**

The device is now waiting for the flash tool.

### Step 3 — Build and flash

```powershell
# Energy Monitor
.\tools\flash-device.ps1 -Device energy_4v3_lcd -Port COM10

# Ham Controls
.\tools\flash-device.ps1 -Device ham_controls -Port COM10
```

Replace `COM10` with whatever port the board enumerated as. To find it:

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
| `No serial data received` (boot mode) | Re-enter boot mode: hold BOOT → tap RESET → release BOOT |
| `No serial data received` (normal reset) | Drop `-before no_reset` — let esptool reset the chip |

---

## Monitoring serial output

```powershell
. C:\esp\esp-idf\export.ps1
idf.py -p COM10 monitor
```

Press **RESET** on the board to see the full boot sequence. Exit with `Ctrl+]`.

Expected boot log — Energy Monitor:

```
I main: startup complete — device: Main House
I wifi: connected with ghome, ...
I ha_client: grid 27.4 kWh | net 434 W | solar 215 W
```

Expected boot log — Ham Controls:

```
I main: startup complete — device: Ham Controls
I wifi: connected with ghome, ...
I ha_ham: sw=[1,0,1] pwr=[145.0,320.0]
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
# For Energy Monitor
copy devices\energy_4v3_lcd\secrets.h.example devices\energy_4v3_lcd\secrets.h

# For Ham Controls
copy devices\ham_controls\secrets.h.example devices\ham_controls\secrets.h
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
# Energy Monitor
.\tools\flash-device.ps1 -Device energy_4v3_lcd -Port COM10

# Ham Controls
.\tools\flash-device.ps1 -Device ham_controls -Port COM10
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

### No solar?

Set these to point at your total consumption sensor:

```c
#define ENT_SOLAR_W   ENT_TOTAL_W
#define ENT_SOLAR_KWH "sensor.dummy_kwh"
#define ENT_NET_W     ENT_TOTAL_W
```

---

## Configuring the Ham Controls panel

Settings live in `devices/ham_controls/device_config.h`.

```c
#define DEVICE_TYPE  DEVICE_TYPE_HAM_CONTROLS   // selects ham UI and HA client
#define DEVICE_NAME  "Ham Controls"

#define HA_HOST      "192.168.1.54"
#define HA_PORT      8123
#define HA_POLL_INTERVAL_MS  15000
#define LOCAL_TZ     "PST8PDT,M3.2.0,M11.1.0"

/* Switch entities — tap a card to toggle */
#define HAM_SW_ENT_0   "switch.radio_power_supply"
#define HAM_SW_ENT_1   "switch.shelly1g4_a085e3c0f2c0"
#define HAM_SW_ENT_2   "switch.palstar_amp"

/* Short labels shown on each card (must be 3 entries) */
#define HAM_SW_NAMES_INIT  "Radio PSU", "Shelly", "Palstar Amp"

/* Power sensor entities (shown on the card when available) */
#define HAM_POWER_ENT_0   "sensor.radio_power_supply_power"   /* → Radio PSU card */
#define HAM_POWER_ENT_1   "sensor.palstar_amp"                /* → Palstar Amp card */
```

To change which switch or sensor a button controls, update the `HAM_SW_ENT_*`
and `HAM_POWER_ENT_*` macros and reflash.

---

## Adding a new device

### Using an existing panel type

If the new device is another Energy Monitor or Ham Controls panel:

1. Copy the matching template:
   ```powershell
   Copy-Item -Recurse devices\energy_4v3_lcd devices\garage
   # or
   Copy-Item -Recurse devices\ham_controls devices\workshop_ham
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

Follow these steps to add a panel type that doesn't exist yet (e.g., a weather
station display, HVAC controller, etc.):

1. **Register the new type** in `main/device_types.h`:
   ```c
   #define DEVICE_TYPE_MY_PANEL  3
   ```

2. **Write the HA client** — `main/ha_mypanel.c` and `main/ha_mypanel.h`:
   - Fetches data from HA REST API (`/api/template` or `/api/states/<entity>`)
   - Exposes a `ha_mypanel_data_t` struct and `ha_mypanel_fetch()` function
   - Wrap the entire implementation in `#if DEVICE_TYPE == DEVICE_TYPE_MY_PANEL`

3. **Write the LVGL UI** — `main/ui_mypanel.c` and `main/ui_mypanel.h`:
   - Builds the screen with `ui_mypanel_init()`
   - Refreshes data with `ui_mypanel_update()`
   - Wrap in `#if DEVICE_TYPE == DEVICE_TYPE_MY_PANEL`

4. **Register the source files** in `main/CMakeLists.txt`:
   ```cmake
   idf_component_register(
       SRCS "main.c" "board.c"
            "ha_client.c" "ha_history.c" "ui.c"
            "ha_ham.c" "ui_ham.c"
            "ha_mypanel.c" "ui_mypanel.c"    # ← add these
       ...
   ```

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
