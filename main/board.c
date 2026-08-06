/*
 * board.c — Waveshare ESP32-S3-Touch-LCD-4.3 (non-B) bring-up.
 *
 * No CH422G: backlight = LEDC on GPIO 2, touch/LCD reset = GPIO_NUM_NC.
 *
 * direct_mode=true is mandatory with avoid_tearing — without it LVGL runs in
 * partial mode and each framebuffer swap can present a buffer holding only the
 * last dirty region, seen on hardware as the UI alternating with a white frame
 * at the cadence of any periodic LVGL timer. (Same invariant as firefly-touch.)
 */
#include "board.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_lcd_panel_io.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"

static const char *TAG = "board";

static i2c_master_bus_handle_t s_i2c_bus;
static esp_lcd_panel_handle_t  s_lcd_panel;
static esp_lcd_touch_handle_t  s_touch;
static lv_display_t           *s_lv_display;
static lv_indev_t             *s_lv_touch_indev;

/* ---------------------------------------------------------------- I2C -- */

static esp_err_t i2c_init(void)
{
    const i2c_master_bus_config_t cfg = {
        .clk_source            = I2C_CLK_SRC_DEFAULT,
        .i2c_port              = I2C_NUM_0,
        .sda_io_num            = BOARD_I2C_GPIO_SDA,
        .scl_io_num            = BOARD_I2C_GPIO_SCL,
        .glitch_ignore_cnt     = 7,
        .flags.enable_internal_pullup = true,
    };
    return i2c_new_master_bus(&cfg, &s_i2c_bus);
}

/* ------------------------------------------------------------ RGB LCD -- */

static esp_err_t rgb_panel_init(void)
{
    const esp_lcd_rgb_panel_config_t cfg = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        /* ST7262-class 800x480 panel timings — matches Waveshare 4.3 demo. */
        .timings = {
            .pclk_hz            = 16 * 1000 * 1000,
            .h_res              = BOARD_LCD_H_RES,
            .v_res              = BOARD_LCD_V_RES,
            .hsync_pulse_width  = 4,
            .hsync_back_porch   = 8,
            .hsync_front_porch  = 8,
            .vsync_pulse_width  = 4,
            .vsync_back_porch   = 16,
            .vsync_front_porch  = 16,
            .flags.pclk_active_neg = true,
        },
        .data_width            = 16,
        .bits_per_pixel        = 16,
        .num_fbs               = 2,
        .bounce_buffer_size_px = BOARD_LCD_H_RES * 10,
        .psram_trans_align     = 64,
        .hsync_gpio_num        = BOARD_LCD_GPIO_HSYNC,
        .vsync_gpio_num        = BOARD_LCD_GPIO_VSYNC,
        .de_gpio_num           = BOARD_LCD_GPIO_DE,
        .pclk_gpio_num         = BOARD_LCD_GPIO_PCLK,
        .disp_gpio_num         = -1,  /* backlight handled via LEDC */
        .data_gpio_nums = {
            BOARD_LCD_GPIO_DATA0,  BOARD_LCD_GPIO_DATA1,  BOARD_LCD_GPIO_DATA2,
            BOARD_LCD_GPIO_DATA3,  BOARD_LCD_GPIO_DATA4,  BOARD_LCD_GPIO_DATA5,
            BOARD_LCD_GPIO_DATA6,  BOARD_LCD_GPIO_DATA7,  BOARD_LCD_GPIO_DATA8,
            BOARD_LCD_GPIO_DATA9,  BOARD_LCD_GPIO_DATA10, BOARD_LCD_GPIO_DATA11,
            BOARD_LCD_GPIO_DATA12, BOARD_LCD_GPIO_DATA13, BOARD_LCD_GPIO_DATA14,
            BOARD_LCD_GPIO_DATA15,
        },
        .flags.fb_in_psram = true,
    };

    ESP_RETURN_ON_ERROR(esp_lcd_new_rgb_panel(&cfg, &s_lcd_panel), TAG, "new rgb panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_lcd_panel), TAG, "panel reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_lcd_panel),  TAG, "panel init");
    return ESP_OK;
}

/* -------------------------------------------------------------- Touch -- */

static esp_err_t touch_init(void)
{
    /* No CH422G on the non-B: skip explicit RST; GT911 uses internal POR.
     * Wait for touch controller to settle before probing. */
    vTaskDelay(pdMS_TO_TICKS(100));

    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    io_cfg.scl_speed_hz = BOARD_I2C_FREQ_HZ;
    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_io_i2c(s_i2c_bus, &io_cfg, &io), TAG, "gt911 io");

    /* If GT911 doesn't ACK at default 0x5D, try backup addr 0x14:
     *   io_cfg.dev_addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP; */
    const esp_lcd_touch_config_t tp_cfg = {
        .x_max        = BOARD_LCD_H_RES,
        .y_max        = BOARD_LCD_V_RES,
        .rst_gpio_num = GPIO_NUM_NC,
        .int_gpio_num = BOARD_TOUCH_GPIO_INT,
        .levels       = { .reset = 0, .interrupt = 0 },
        .flags        = { .swap_xy = 0, .mirror_x = 0, .mirror_y = 0 },
    };
    return esp_lcd_touch_new_i2c_gt911(io, &tp_cfg, &s_touch);
}

/* ------------------------------------------------------ Backlight LEDC -- */

static esp_err_t backlight_init(void)
{
    const ledc_timer_config_t timer = {
        .duty_resolution = LEDC_TIMER_8_BIT,
        .freq_hz         = 5000,
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .timer_num       = BOARD_BACKLIGHT_LEDC_TIMER,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "ledc timer");

    const ledc_channel_config_t ch = {
        .channel    = BOARD_BACKLIGHT_LEDC_CHANNEL,
        .duty       = 0,
        .gpio_num   = BOARD_BACKLIGHT_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_sel  = BOARD_BACKLIGHT_LEDC_TIMER,
        .intr_type  = LEDC_INTR_DISABLE,
        .hpoint     = 0,
    };
    return ledc_channel_config(&ch);
}

/* --------------------------------------------------------- LVGL glue -- */

static esp_err_t lvgl_init(void)
{
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_priority = 4;
    port_cfg.task_stack    = 8192;
    port_cfg.task_affinity = 1;   /* UI on core 1 */
    ESP_RETURN_ON_ERROR(lvgl_port_init(&port_cfg), TAG, "lvgl_port_init");

    const lvgl_port_display_cfg_t disp_cfg = {
        .panel_handle = s_lcd_panel,
        .buffer_size  = BOARD_LCD_H_RES * BOARD_LCD_V_RES,
        .double_buffer = true,
        .hres          = BOARD_LCD_H_RES,
        .vres          = BOARD_LCD_V_RES,
        .monochrome    = false,
        .rotation      = { .swap_xy = false, .mirror_x = false, .mirror_y = false },
        .color_format  = LV_COLOR_FORMAT_RGB565,
        .flags = {
            .buff_dma    = false,
            .buff_spiram = true,
            .swap_bytes  = false,
            /* direct_mode required with avoid_tearing — see file header. */
            .direct_mode = true,
        },
    };
    const lvgl_port_display_rgb_cfg_t rgb_cfg = {
        .flags = {
            .bb_mode      = true,
            .avoid_tearing = true,
        },
    };
    s_lv_display = lvgl_port_add_disp_rgb(&disp_cfg, &rgb_cfg);
    ESP_RETURN_ON_FALSE(s_lv_display != NULL, ESP_FAIL, TAG, "add display");

    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp   = s_lv_display,
        .handle = s_touch,
    };
    s_lv_touch_indev = lvgl_port_add_touch(&touch_cfg);
    ESP_RETURN_ON_FALSE(s_lv_touch_indev != NULL, ESP_FAIL, TAG, "add touch");
    return ESP_OK;
}

/* ---------------------------------------------------------------- API -- */

esp_err_t board_display_init(void)
{
    ESP_RETURN_ON_ERROR(i2c_init(),       TAG, "i2c");
    ESP_RETURN_ON_ERROR(backlight_init(), TAG, "ledc");

    /* Let panel power rails settle before starting RGB output. */
    vTaskDelay(pdMS_TO_TICKS(50));

    ESP_RETURN_ON_ERROR(rgb_panel_init(), TAG, "rgb");
    ESP_RETURN_ON_ERROR(touch_init(),     TAG, "touch");
    ESP_RETURN_ON_ERROR(lvgl_init(),      TAG, "lvgl");
    ESP_RETURN_ON_ERROR(board_backlight_set_percent(100), TAG, "backlight on");

    ESP_LOGI(TAG, "up: 800x480 RGB565, GT911, LVGL core 1, LEDC backlight GPIO %d",
             BOARD_BACKLIGHT_GPIO);
    return ESP_OK;
}

lv_display_t *board_get_display(void)      { return s_lv_display; }
lv_indev_t   *board_get_touch_indev(void)  { return s_lv_touch_indev; }

esp_err_t board_backlight_set_percent(uint8_t percent)
{
    uint32_t duty = ((uint32_t)percent * 255) / 100;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, BOARD_BACKLIGHT_LEDC_CHANNEL, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, BOARD_BACKLIGHT_LEDC_CHANNEL);
    return ESP_OK;
}
