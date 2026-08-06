/*
 * board.h — Waveshare ESP32-S3-Touch-LCD-4.3 (non-B variant).
 *
 * Key differences from the 4.3B:
 *   - No CH422G IO expander — backlight, LCD RST, touch RST are direct GPIOs.
 *   - Backlight on GPIO 2 via LEDC PWM (real dimming, unlike on/off of the B).
 *   - No wide DC input; 5 V USB-C only.
 *   - GPIO 2 = backlight; GPIO_NUM_NC for LCD RST and touch RST.
 *
 * RGB pin assignments are the same panel as the 4.3B.
 * Source: Waveshare ESP32-S3-Touch-LCD-4.3 demo code.
 */
#pragma once

#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- LCD geometry ------------------------------------------------------ */
#define BOARD_LCD_H_RES   800
#define BOARD_LCD_V_RES   480

/* ---- RGB LCD GPIO (same panel as 4.3B) --------------------------------- */
#define BOARD_LCD_GPIO_DE      5
#define BOARD_LCD_GPIO_VSYNC   3
#define BOARD_LCD_GPIO_HSYNC   46
#define BOARD_LCD_GPIO_PCLK    7
/* Data bus LSB first: B3-B7, G2-G7, R3-R7 */
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

/* ---- I2C (GT911 touch) ------------------------------------------------- */
#define BOARD_I2C_GPIO_SDA     8
#define BOARD_I2C_GPIO_SCL     9
#define BOARD_I2C_FREQ_HZ      400000
#define BOARD_TOUCH_GPIO_INT   4

/* ---- Backlight (non-B: LEDC PWM direct, real dimming available) --------- */
#define BOARD_BACKLIGHT_GPIO         2
#define BOARD_BACKLIGHT_LEDC_TIMER   LEDC_TIMER_0
#define BOARD_BACKLIGHT_LEDC_CHANNEL LEDC_CHANNEL_0

/* ---- API --------------------------------------------------------------- */

/*
 * Bring up I2C, RGB LCD, GT911 touch, esp_lvgl_port (LVGL on core 1),
 * and LEDC backlight at 100 %. Call once before any UI code.
 */
esp_err_t board_display_init(void);

lv_display_t *board_get_display(void);
lv_indev_t   *board_get_touch_indev(void);

/* percent 0-100; LEDC PWM, so intermediate values work. */
esp_err_t board_backlight_set_percent(uint8_t percent);

#ifdef __cplusplus
}
#endif
