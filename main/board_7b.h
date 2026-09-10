/*
 * board_7b.h — Waveshare ESP32-S3-Touch-LCD-7B (1024x600), PORTRAIT.
 *
 * Ported from firefly-touch/components/board/board_lcd7b.{c,h}, which is
 * bench-verified on real 7B hardware. Differences here:
 *
 *   - Display half only. No TWAI/CAN, no USB/CAN mux — this is a WiFi/HA
 *     wall panel, so EXIO5 is left alone and the native USB port stays live.
 *   - PORTRAIT. The panel is wall-mounted rotated 90°, so the UI runs at a
 *     logical 600x1024. We use esp_lvgl_port software rotation
 *     (sw_rotate + bb_mode, NOT avoid_tearing/full_refresh) — the same
 *     recipe firefly-touch's board_4_3b.c uses for its portrait panels.
 *     avoid_tearing hands LVGL the physical framebuffers and cannot be
 *     combined with sw_rotate; see board_7b.c for the full rationale.
 *
 * 7B hardware facts carried over from firefly-touch (do not "simplify"):
 *
 *   - IO expander is Waveshare's register-addressed "IO_EXTENSION" at I2C
 *     0x24 (ws_io_expander.c), NOT a CH422G.
 *   - EXIO6 = LCD_VDD_EN (VCOM supply). Must be driven HIGH before the RGB
 *     panel comes up or the screen stays black with the backlight lit —
 *     the "double enable pin" trap. board_display_init() handles it.
 *   - RGB porch/pulse block + pclk are ESPHome's WAVESHARE-5-1024X600
 *     numbers, NOT the "7B demo" fork's (still 800x480 → scrambled panel).
 */
#pragma once

#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Physical LCD geometry (landscape scanout) --------------------- */
#define BOARD_LCD_H_RES   1024
#define BOARD_LCD_V_RES   600

/* Logical geometry after the 90° portrait rotation. UI code uses these. */
#define BOARD_UI_H_RES    600
#define BOARD_UI_V_RES    1024

/* ---- RGB LCD pins (identical to the 4.3B and the non-B 7") --------- */
#define BOARD_LCD_GPIO_DE      5
#define BOARD_LCD_GPIO_VSYNC   3
#define BOARD_LCD_GPIO_HSYNC   46
#define BOARD_LCD_GPIO_PCLK    7
/* Data bus, LSB first: B3..B7, G2..G7, R3..R7 */
#define BOARD_LCD_GPIO_DATA0   14  /* B3 */
#define BOARD_LCD_GPIO_DATA1   38  /* B4 */
#define BOARD_LCD_GPIO_DATA2   18  /* B5 */
#define BOARD_LCD_GPIO_DATA3   17  /* B6 */
#define BOARD_LCD_GPIO_DATA4   10  /* B7 */
#define BOARD_LCD_GPIO_DATA5   39  /* G2 */
#define BOARD_LCD_GPIO_DATA6   0   /* G3 */
#define BOARD_LCD_GPIO_DATA7   45  /* G4 */
#define BOARD_LCD_GPIO_DATA8   48  /* G5 */
#define BOARD_LCD_GPIO_DATA9   47  /* G6 */
#define BOARD_LCD_GPIO_DATA10  21  /* G7 */
#define BOARD_LCD_GPIO_DATA11  1   /* R3 */
#define BOARD_LCD_GPIO_DATA12  2   /* R4 */
#define BOARD_LCD_GPIO_DATA13  42  /* R5 */
#define BOARD_LCD_GPIO_DATA14  41  /* R6 */
#define BOARD_LCD_GPIO_DATA15  40  /* R7 */

/* ---- I2C (GT911 touch + IO_EXTENSION expander share the bus) ------- */
#define BOARD_I2C_GPIO_SDA     8
#define BOARD_I2C_GPIO_SCL     9
#define BOARD_I2C_FREQ_HZ      400000
#define BOARD_TOUCH_GPIO_INT   4

/* ---- IO_EXTENSION expander pins (EXIOn) --------------------------- */
#define BOARD_EXIO_TP_RST      1   /* GT911 reset            */
#define BOARD_EXIO_DISP        2   /* LCD backlight enable   */
#define BOARD_EXIO_LCD_RST     3
#define BOARD_EXIO_LCD_VDD_EN  6   /* 7B-only VCOM supply enable — HIGH first */

/* ---- Portrait rotation -------------------------------------------- */
/* 90 = USB port on the left when mounted; flip to 270 and reflash if the
 * panel ends up upside-down on the wall. */
#ifndef BOARD_7B_ROTATION
#define BOARD_7B_ROTATION  LV_DISPLAY_ROTATION_90
#endif

#ifdef __cplusplus
}
#endif
