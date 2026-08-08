/*
 * ui_ham.c — Ham Radio control panel UI for 800x480, LVGL 9.
 *
 * Layout:
 *   Status bar (0-40px): "HAM CONTROLS", clock, connection dot.
 *   Three large tap-to-toggle cards:
 *     [0] Radio PSU    switch.radio_power_supply       + sensor.radio_power_supply_power
 *     [1] Shelly       switch.shelly1g4_a085e3c0f2c0
 *     [2] Palstar Amp  switch.palstar_amp               + sensor.palstar_amp
 *
 * Compiled only for DEVICE_TYPE_HAM_CONTROLS builds.
 */
#include "ui_ham.h"
#include "ha_config.h"

#if DEVICE_TYPE == DEVICE_TYPE_HAM_CONTROLS

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "lvgl.h"
#include "board.h"

static const char *TAG = "ui_ham";

#define C_BG         lv_color_hex(0x0d1117)
#define C_CARD       lv_color_hex(0x161b22)
#define C_BORDER     lv_color_hex(0x30363d)
#define C_TXT        lv_color_hex(0xe6edf3)
#define C_TXT2       lv_color_hex(0x7d8590)
#define C_AMBER      lv_color_hex(0xe3a435)
#define C_GREEN      lv_color_hex(0x3fb950)
#define C_RED        lv_color_hex(0xf85149)
#define C_BLUE       lv_color_hex(0x58a6ff)
#define C_DOT_OK     lv_color_hex(0x3fb950)
#define C_DOT_ERR    lv_color_hex(0xf85149)
#define C_BTN_ON     lv_color_hex(0x1a3a20)
#define C_BTN_OFF    lv_color_hex(0x2d1a1a)
#define C_BDR_ON     lv_color_hex(0x3fb950)
#define C_BDR_OFF    lv_color_hex(0xf85149)

#define SCR_W   800
#define SCR_H   480
#define PAD     8
#define RADIUS  12

#define BTN_Y   50
#define BTN_H   (SCR_H - BTN_Y - PAD)
#define BTN_W   ((SCR_W - PAD * 4) / 3)
#define BTN_1X  PAD
#define BTN_2X  (BTN_1X + BTN_W + PAD)
#define BTN_3X  (BTN_2X + BTN_W + PAD)

#define DIM_TIMEOUT_MS  (5UL * 60UL * 1000UL)
#define DIM_PERCENT     10

static bool     s_dimmed;
static uint32_t s_last_activity_tick;

static bool          s_connected;

static lv_obj_t *s_time_label;
static lv_obj_t *s_status_dot;

static struct {
    lv_obj_t *card;
    lv_obj_t *state_lbl;
    lv_obj_t *power_lbl;
} s_btns[HAM_NUM_SWITCHES];

static const char *s_btn_names[HAM_NUM_SWITCHES] = { HAM_SW_NAMES_INIT };

/* Which power[] index each button shows; -1 = none */
static const int s_btn_power_idx[HAM_NUM_SWITCHES] = { 0, -1, 1 };

static void (*s_toggle_cb)(int switch_idx);

void ui_ham_set_toggle_cb(void (*cb)(int switch_idx))
{
    s_toggle_cb = cb;
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *txt,
                             const lv_font_t *font, lv_color_t col,
                             lv_align_t align, int x, int y)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, txt);
    lv_obj_set_style_text_font(lbl, font, 0);
    lv_obj_set_style_text_color(lbl, col, 0);
    lv_obj_align(lbl, align, x, y);
    return lbl;
}

static void dimmer_cb(lv_timer_t *t)
{
    (void)t;
    uint32_t elapsed = lv_tick_get() - s_last_activity_tick;
    if (!s_dimmed && elapsed >= DIM_TIMEOUT_MS) {
        board_backlight_set_percent(DIM_PERCENT);
        s_dimmed = true;
    } else if (s_dimmed && elapsed < DIM_TIMEOUT_MS) {
        board_backlight_set_percent(100);
        s_dimmed = false;
    }
}

static void clock_tick_cb(lv_timer_t *t)
{
    (void)t;
    if (!s_time_label) return;
    time_t now;
    time(&now);
    if (now < 1000000UL) {
        lv_label_set_text(s_time_label, "-- : -- --");
        return;
    }
    struct tm tm;
    localtime_r(&now, &tm);
    char buf[32];
    strftime(buf, sizeof(buf), "%I:%M %p  %a %b %d", &tm);
    lv_label_set_text(s_time_label, buf);
}

static void screen_press_cb(lv_event_t *e)
{
    (void)e;
    s_last_activity_tick = lv_tick_get();
    if (s_dimmed) {
        board_backlight_set_percent(100);
        s_dimmed = false;
    }
}

static void btn_click_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    s_last_activity_tick = lv_tick_get();
    if (s_dimmed) {
        board_backlight_set_percent(100);
        s_dimmed = false;
        return;
    }
    if (s_toggle_cb) s_toggle_cb(idx);
}

static void apply_btn_state(int i, bool on)
{
    lv_obj_set_style_bg_color(s_btns[i].card,    on ? C_BTN_ON : C_BTN_OFF, 0);
    lv_obj_set_style_border_color(s_btns[i].card, on ? C_BDR_ON : C_BDR_OFF, 0);
    lv_label_set_text(s_btns[i].state_lbl, on ? "ON" : "OFF");
    lv_obj_set_style_text_color(s_btns[i].state_lbl, on ? C_GREEN : C_RED, 0);
}

void ui_ham_init(void)
{
    s_last_activity_tick = lv_tick_get();
    s_dimmed = false;

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(scr, screen_press_cb, LV_EVENT_PRESSED, NULL);

    make_label(scr, LV_SYMBOL_AUDIO " HAM CONTROLS",
               &lv_font_montserrat_20, C_BLUE,
               LV_ALIGN_TOP_LEFT, PAD + 2, 10);

    s_time_label = make_label(scr, "--:-- --  --- --- --",
                              &lv_font_montserrat_16, C_TXT2,
                              LV_ALIGN_TOP_RIGHT, -(PAD + 18), 12);

    s_status_dot = lv_obj_create(scr);
    lv_obj_set_size(s_status_dot, 12, 12);
    lv_obj_align(s_status_dot, LV_ALIGN_TOP_RIGHT, -PAD, 14);
    lv_obj_set_style_radius(s_status_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_status_dot, s_connected ? C_DOT_OK : C_DOT_ERR, 0);
    lv_obj_set_style_bg_opa(s_status_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_status_dot, 0, 0);

    lv_obj_t *sep = lv_obj_create(scr);
    lv_obj_set_pos(sep, 0, 40);
    lv_obj_set_size(sep, SCR_W, 1);
    lv_obj_set_style_bg_color(sep, C_BORDER, 0);
    lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sep, 0, 0);
    lv_obj_set_style_radius(sep, 0, 0);

    static const int btn_x[HAM_NUM_SWITCHES] = { BTN_1X, BTN_2X, BTN_3X };

    for (int i = 0; i < HAM_NUM_SWITCHES; i++) {
        lv_obj_t *card = lv_obj_create(scr);
        lv_obj_set_pos(card, btn_x[i], BTN_Y);
        lv_obj_set_size(card, BTN_W, BTN_H);
        lv_obj_set_style_bg_color(card, C_BTN_OFF, 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(card, C_BDR_OFF, 0);
        lv_obj_set_style_border_width(card, 2, 0);
        lv_obj_set_style_radius(card, RADIUS, 0);
        lv_obj_set_style_pad_all(card, 14, 0);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(card, btn_click_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);

        s_btns[i].card = card;

        make_label(card, s_btn_names[i],
                   &lv_font_montserrat_16, C_TXT2,
                   LV_ALIGN_TOP_LEFT, 0, 0);

        lv_obj_t *state = lv_label_create(card);
        lv_label_set_text(state, "---");
        lv_obj_set_style_text_font(state, &lv_font_montserrat_48, 0);
        lv_obj_set_style_text_color(state, C_TXT2, 0);
        lv_obj_align(state, LV_ALIGN_CENTER, 0,
                     (s_btn_power_idx[i] >= 0) ? -30 : 0);
        s_btns[i].state_lbl = state;

        lv_obj_t *power_lbl = lv_label_create(card);
        if (s_btn_power_idx[i] >= 0) {
            lv_label_set_text(power_lbl, "--- W");
            lv_obj_set_style_text_font(power_lbl, &lv_font_montserrat_24, 0);
            lv_obj_set_style_text_color(power_lbl, C_AMBER, 0);
            lv_obj_align(power_lbl, LV_ALIGN_CENTER, 0, 40);
        } else {
            lv_label_set_text(power_lbl, "");
        }
        s_btns[i].power_lbl = power_lbl;

        make_label(card, LV_SYMBOL_REFRESH "  tap to toggle",
                   &lv_font_montserrat_14, C_TXT2,
                   LV_ALIGN_BOTTOM_MID, 0, -4);
    }

    lv_timer_create(clock_tick_cb, 1000, NULL);
    lv_timer_create(dimmer_cb, 10000, NULL);

    ESP_LOGI(TAG, "Ham Controls UI ready");
}

void ui_ham_update(const ha_ham_data_t *d)
{
    if (!d->valid) return;

    char buf[32];

    for (int i = 0; i < HAM_NUM_SWITCHES; i++) {
        apply_btn_state(i, d->sw[i]);

        int pi = s_btn_power_idx[i];
        if (pi >= 0 && d->power[pi] >= 0.0f) {
            if (d->power[pi] >= 1000.0f)
                snprintf(buf, sizeof(buf), "%.2f kW", d->power[pi] / 1000.0f);
            else
                snprintf(buf, sizeof(buf), "%.0f W", d->power[pi]);
            lv_label_set_text(s_btns[i].power_lbl, buf);
        }
    }

    lv_obj_set_style_bg_color(s_status_dot,
                              s_connected ? C_DOT_OK : C_DOT_ERR, 0);
}

void ui_ham_set_connected(bool connected)
{
    s_connected = connected;
    if (s_status_dot)
        lv_obj_set_style_bg_color(s_status_dot,
                                  connected ? C_DOT_OK : C_DOT_ERR, 0);
}

#endif /* DEVICE_TYPE_HAM_CONTROLS */
