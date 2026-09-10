/*
 * board_7b.c — Waveshare ESP32-S3-Touch-LCD-7B (1024x600) bring-up, PORTRAIT.
 *
 * Display half of firefly-touch/components/board/board_lcd7b.c (bench-verified
 * on real 7B hardware), minus the TWAI/CAN + USB mux code this HA wall panel
 * does not need. Rotated 90° to a logical 600x1024 for a portrait wall mount.
 *
 * ─── Buffer / rotation mode ──────────────────────────────────────────────
 * The panel is wall-mounted in portrait, so LVGL renders a 600x1024 frame
 * that has to be transposed to the 1024x600 physical scan-line layout. That
 * is esp_lvgl_port's sw_rotate path:
 *
 *   sw_rotate = true, bb_mode = true, avoid_tearing = false, no full_refresh
 *
 * — the same recipe board_4_3b.c uses for its portrait panels. Why not the
 * 7B's landscape recipe (avoid_tearing + full_refresh):
 *
 *   - avoid_tearing hands LVGL the RGB panel's two physical PSRAM
 *     framebuffers as its draw buffers, leaving no room for a rotation
 *     transpose. esp_lvgl_port therefore forbids sw_rotate + avoid_tearing
 *     on RGB panels.
 *   - full_refresh / direct_mode both wait on disp_ctx->trans_sem, which is
 *     only created when avoid_tearing is on — so they assert on the first
 *     flush without it.
 *
 * Partial-render + bounce-buffer is what is left. board_lcd7b.c notes that
 * partial mode "blinked" on this 1024x600 panel in landscape; in portrait we
 * have no alternative, so the tuning knobs (raise BOUNCE_LINES, drop
 * PCLK_HZ to 16 MHz, enlarge DRAW_LINES) are called out inline. Bench-iterate.
 */
#include "board.h"
#include "board_7b.h"
#include "ha_config.h"

#if DEVICE_TYPE == DEVICE_TYPE_OFFICE_PANEL

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_lcd_panel_io.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"

#include "ws_io_expander.h"

static const char *TAG = "board_7b";

/* ── Tuning knobs (bench-iterate if the portrait frame flickers) ──────── */
#define PCLK_HZ       (21 * 1000 * 1000)   /* drop to 16 MHz if it shakes  */
#define BOUNCE_LINES  20                   /* internal-RAM bounce, in rows */
#define DRAW_LINES    120                  /* LVGL partial draw buf, rows  */

static i2c_master_bus_handle_t s_i2c_bus;
static esp_lcd_panel_handle_t  s_lcd_panel;
static esp_lcd_touch_handle_t  s_touch;
static lv_display_t           *s_lv_display;
static lv_indev_t             *s_lv_touch_indev;

/* ---------------------------------------------------------------- I2C -- */

static esp_err_t i2c_bus_init(void)
{
    const i2c_master_bus_config_t cfg = {
        .clk_source                   = I2C_CLK_SRC_DEFAULT,
        .i2c_port                     = I2C_NUM_0,
        .sda_io_num                   = BOARD_I2C_GPIO_SDA,
        .scl_io_num                   = BOARD_I2C_GPIO_SCL,
        .glitch_ignore_cnt            = 7,
        .flags.enable_internal_pullup = true,
    };
    return i2c_new_master_bus(&cfg, &s_i2c_bus);
}

/* ------------------------------------------------------------ RGB LCD -- */

static esp_err_t rgb_panel_init(void)
{
    const esp_lcd_rgb_panel_config_t cfg = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        /* ESPHome WAVESHARE-5-1024X600 timings — bench-verified on a real 7B
         * (xtux77/waveshare-esp32s3-lcd7b-esphome). Do NOT swap in the
         * "7B demo" fork numbers: still 800x480 → scrambled panel. */
        .timings = {
            .pclk_hz           = PCLK_HZ,
            .h_res             = BOARD_LCD_H_RES,
            .v_res             = BOARD_LCD_V_RES,
            .hsync_pulse_width = 30,
            .hsync_back_porch  = 145,
            .hsync_front_porch = 170,
            .vsync_pulse_width = 2,
            .vsync_back_porch  = 23,
            .vsync_front_porch = 12,
            .flags.pclk_active_neg = true,
        },
        .data_width            = 16,
        .bits_per_pixel        = 16,
        .num_fbs               = 2,
        .bounce_buffer_size_px = BOARD_LCD_H_RES * BOUNCE_LINES,
        .psram_trans_align     = 64,
        .hsync_gpio_num        = BOARD_LCD_GPIO_HSYNC,
        .vsync_gpio_num        = BOARD_LCD_GPIO_VSYNC,
        .de_gpio_num           = BOARD_LCD_GPIO_DE,
        .pclk_gpio_num         = BOARD_LCD_GPIO_PCLK,
        .disp_gpio_num         = -1,   /* DISP driven via IO_EXTENSION EXIO2 */
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
    /* GT911 reset is wired through the IO expander. INT level during reset
     * picks 0x5D vs 0x14; INT left as input lands on the default 0x5D.
     * If the GT911 doesn't ACK at 0x5D, set io_cfg.dev_addr =
     * ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP (0x14). */
    ESP_RETURN_ON_ERROR(ws_io_expander_set_pin(BOARD_EXIO_TP_RST, false), TAG, "tp rst low");
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_RETURN_ON_ERROR(ws_io_expander_set_pin(BOARD_EXIO_TP_RST, true), TAG, "tp rst high");
    vTaskDelay(pdMS_TO_TICKS(100));

    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    io_cfg.scl_speed_hz = BOARD_I2C_FREQ_HZ;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(s_i2c_bus, &io_cfg, &io), TAG, "gt911 io");

    /* x_max/y_max are the PHYSICAL panel extents; LVGL 9 applies the inverse
     * of the display rotation to indev coordinates automatically. */
    const esp_lcd_touch_config_t tp_cfg = {
        .x_max        = BOARD_LCD_H_RES,
        .y_max        = BOARD_LCD_V_RES,
        .rst_gpio_num = GPIO_NUM_NC,           /* handled via IO expander above */
        .int_gpio_num = BOARD_TOUCH_GPIO_INT,
        .levels       = { .reset = 0, .interrupt = 0 },
        .flags        = { .swap_xy = 0, .mirror_x = 0, .mirror_y = 0 },
    };
    return esp_lcd_touch_new_i2c_gt911(io, &tp_cfg, &s_touch);
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
        .panel_handle  = s_lcd_panel,
        .buffer_size   = BOARD_LCD_H_RES * DRAW_LINES,
        .double_buffer = true,
        .hres          = BOARD_LCD_H_RES,   /* physical, pre-rotation */
        .vres          = BOARD_LCD_V_RES,
        .monochrome    = false,
        .rotation      = { .swap_xy = false, .mirror_x = false, .mirror_y = false },
        .color_format  = LV_COLOR_FORMAT_RGB565,
        .flags = {
            .buff_dma    = false,
            .buff_spiram = true,
            .swap_bytes  = false,
            /* Portrait: CPU rotation of each dirty region at partial-flush
             * time. See the file header for why avoid_tearing / full_refresh
             * cannot be used here. */
            .sw_rotate   = true,
        },
    };
    const lvgl_port_display_rgb_cfg_t rgb_cfg = {
        .flags = {
            .bb_mode       = true,
            .avoid_tearing = false,
        },
    };
    s_lv_display = lvgl_port_add_disp_rgb(&disp_cfg, &rgb_cfg);
    ESP_RETURN_ON_FALSE(s_lv_display != NULL, ESP_FAIL, TAG, "add display");

    /* 90° CW → logical 600x1024. LVGL 9 rotates touch-indev coordinates to
     * match, so no GT911 flag changes are needed. Flip to _270 (or set
     * BOARD_7B_ROTATION) and reflash if the wall mount comes out inverted. */
    lvgl_port_lock(0);
    lv_display_set_rotation(s_lv_display, BOARD_7B_ROTATION);
    lvgl_port_unlock();

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
    ESP_RETURN_ON_ERROR(i2c_bus_init(), TAG, "i2c");
    ESP_RETURN_ON_ERROR(ws_io_expander_init(s_i2c_bus), TAG, "io expander");

    /* ⚠️ 7B-only: bring up the panel VCOM supply (EXIO6 = LCD_VDD_EN)
     * BEFORE reset / RGB init, or the backlight lights but no image shows. */
    ESP_RETURN_ON_ERROR(ws_io_expander_set_pin(BOARD_EXIO_LCD_VDD_EN, true), TAG, "lcd vdd en");
    vTaskDelay(pdMS_TO_TICKS(20));

    /* Panel reset. */
    ESP_RETURN_ON_ERROR(ws_io_expander_set_pin(BOARD_EXIO_LCD_RST, false), TAG, "lcd rst low");
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_RETURN_ON_ERROR(ws_io_expander_set_pin(BOARD_EXIO_LCD_RST, true), TAG, "lcd rst high");
    vTaskDelay(pdMS_TO_TICKS(20));

    ESP_RETURN_ON_ERROR(rgb_panel_init(), TAG, "rgb panel");
    ESP_RETURN_ON_ERROR(touch_init(),     TAG, "touch");
    ESP_RETURN_ON_ERROR(lvgl_init(),      TAG, "lvgl");

    ESP_RETURN_ON_ERROR(board_backlight_set_percent(100), TAG, "backlight");

    ESP_LOGI(TAG, "heap after display init: internal %u B, PSRAM %u B",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    ESP_LOGI(TAG, "up: 1024x600 RGB565 → 600x1024 portrait, GT911, LVGL core 1");
    return ESP_OK;
}

lv_display_t *board_get_display(void)     { return s_lv_display; }
lv_indev_t   *board_get_touch_indev(void) { return s_lv_touch_indev; }

esp_err_t board_backlight_set_percent(uint8_t percent)
{
    /* Match firefly-touch board_lcd7b.c exactly: EXIO2 is a plain on/off
     * output bit and that is the backlight enable on this board (it also
     * gates DISP, so percent==0 = panel standby). The chip's PWM output
     * (reg 0x05, ws_io_expander_set_backlight_pct) is a future hardware-dim
     * improvement — NOT wired in here: writing it on the bench unit left the
     * panel dark, so on/off only for parity with the proven path. */
    return ws_io_expander_set_pin(BOARD_EXIO_DISP, percent > 0);
}

#endif /* DEVICE_TYPE_OFFICE_PANEL */
