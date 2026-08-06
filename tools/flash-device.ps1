<#
.SYNOPSIS
    Select, build, and flash a named device configuration.

.DESCRIPTION
    Copies devices/<Device>/device_config.h (and secrets.h if present) to
    main/, builds with idf.py, then flashes via esptool.

    The device must be in download mode before flashing:
    Hold BOOT, tap RESET, release BOOT — then press Enter when prompted.

.PARAMETER Device
    Device name — must match a subdirectory under devices/.

.PARAMETER Port
    COM port to flash to (e.g. COM9). Required unless -BuildOnly is set.

.PARAMETER BuildOnly
    Build without flashing.

.PARAMETER FlashOnly
    Skip rebuild and flash existing build/ artifacts.

.EXAMPLE
    # Build and flash the main house display
    .\tools\flash-device.ps1 -Device main_house -Port COM9

    # Build only (no device connected)
    .\tools\flash-device.ps1 -Device garage -BuildOnly

    # Flash a pre-built binary (device already selected and built)
    .\tools\flash-device.ps1 -Device main_house -Port COM9 -FlashOnly
#>
param(
    [Parameter(Mandatory)]
    [string]$Device,

    [string]$Port = "",

    [switch]$BuildOnly,
    [switch]$FlashOnly
)

$ErrorActionPreference = "Stop"
$root    = Split-Path $PSScriptRoot -Parent
$devDir  = Join-Path $root "devices\$Device"
$mainDir = Join-Path $root "main"
$build   = Join-Path $root "build"

# ── Validate ─────────────────────────────────────────────────────────────────

if (-not (Test-Path $devDir)) {
    Write-Error "Device '$Device' not found at $devDir"
    Write-Host ""
    Write-Host "Available devices:"
    Get-ChildItem (Join-Path $root "devices") -Directory |
        Where-Object { $_.Name -ne "NEW_DEVICE_TEMPLATE" } |
        ForEach-Object { Write-Host "  $($_.Name)" }
    exit 1
}

$configSrc = Join-Path $devDir "device_config.h"
if (-not (Test-Path $configSrc)) {
    Write-Error "Missing $devDir\device_config.h"
    exit 1
}

if (-not $BuildOnly -and -not $Port) {
    Write-Error "-Port is required unless -BuildOnly is set."
    exit 1
}

# ── Select device ─────────────────────────────────────────────────────────────

if (-not $FlashOnly) {
    Write-Host ""
    Write-Host "==> Selecting device: $Device" -ForegroundColor Cyan

    Copy-Item $configSrc (Join-Path $mainDir "device_config.h") -Force
    Write-Host "    Copied device_config.h"

    $secretsSrc = Join-Path $devDir "secrets.h"
    if (Test-Path $secretsSrc) {
        Copy-Item $secretsSrc (Join-Path $mainDir "secrets.h") -Force
        Write-Host "    Copied secrets.h"
    } elseif (Test-Path (Join-Path $mainDir "secrets.h")) {
        Write-Host "    Using existing main/secrets.h"
    } else {
        Write-Error "No secrets.h found. Copy $devDir\secrets.h.example to $devDir\secrets.h and fill in credentials."
        exit 1
    }

    # ── Build ─────────────────────────────────────────────────────────────────

    Write-Host ""
    Write-Host "==> Activating ESP-IDF..." -ForegroundColor Cyan
    . C:\esp\esp-idf\export.ps1 2>&1 | Out-Null

    Write-Host "==> Building $Device..." -ForegroundColor Cyan
    Set-Location $root
    idf.py build
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Build failed — check output above."
        exit 1
    }
    Write-Host "==> Build succeeded." -ForegroundColor Green
}

if ($BuildOnly) {
    Write-Host ""
    Write-Host "Build-only mode — not flashing." -ForegroundColor Yellow
    exit 0
}

# ── Flash ─────────────────────────────────────────────────────────────────────

Write-Host ""
Write-Host "==> Ready to flash $Device to $Port" -ForegroundColor Cyan
Write-Host ""
Write-Host "    Put the device in boot mode:"
Write-Host "      1. Hold BOOT button"
Write-Host "      2. Tap RESET button"
Write-Host "      3. Release BOOT button"
Write-Host ""
Write-Host "    Press Enter when the device is in boot mode..." -NoNewline
Read-Host

. C:\esp\esp-idf\export.ps1 2>&1 | Out-Null

python -m esptool --chip esp32s3 -p $Port -b 460800 --before no_reset `
    write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m `
    0x0    "$build\bootloader\bootloader.bin" `
    0x8000 "$build\partition_table\partition-table.bin" `
    0x10000 "$build\ha_esp32_display.bin"

if ($LASTEXITCODE -eq 0) {
    Write-Host ""
    Write-Host "==> Flash complete!" -ForegroundColor Green
    Write-Host "    Press RESET on the device to boot normally."
    Write-Host ""
    Write-Host "    To monitor serial output:"
    Write-Host "      idf.py -p $Port monitor"
} else {
    Write-Error "Flash failed. Is the device in boot mode on $Port?"
}
