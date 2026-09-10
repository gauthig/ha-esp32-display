/*
 * ui_office.c — Office Panel 7 UI (Waveshare 7B, 600x1024 portrait), LVGL 9.
 *
 *   HOME   : status bar · Office Fan Lights card · 3 HAM switch cards ·
 *            bottom ENERGY nav bar
 *   POPUP  : long-press the light card → brightness slider + colour swatches,
 *            applied to both light.office_fan_light_1/2 together
 *   ENERGY : portrait re-layout of the energy dashboard (same ha_client /
 *            ha_history data as the energy_4v3_lcd panel)
 *   CHART  : 7-day grid+solar history, tap to return to ENERGY
 *
 * The energy data layer (ha_client.c / ha_history.c) and the HAM data layer
 * (ha_ham.c) are reused unchanged; only the layout here is new.
 *
 * Compiled only for DEVICE_TYPE_OFFICE_PANEL.
 */
#include "ui_office.h"
#include "ha_config.h"

#if DEVICE_TYPE == DEVICE_TYPE_OFFICE_PANEL

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>

#include "esp_log.h"
#include "lvgl.h"
#include "board.h"

static const char *TAG = "ui_office";

/* ------------------------------------------------------------- Colours -- */
#define C_BG        lv_color_hex(0x0d1117)
#define C_CARD      lv_color_hex(0x161b22)
#define C_BORDER    lv_color_hex(0x30363d)
#define C_TXT       lv_color_hex(0xe6edf3)
#define C_TXT2      lv_color_hex(0x7d8590)
#define C_AMBER     lv_color_hex(0xe3a435)
#define C_GREEN     lv_color_hex(0x3fb950)
#define C_RED       lv_color_hex(0xf85149)
#define C_BLUE      lv_color_hex(0x58a6ff)
#define C_BAR_BG    lv_color_hex(0x21262d)
#define C_DOT_OK    lv_color_hex(0x3fb950)
#define C_DOT_ERR   lv_color_hex(0xf85149)
#define C_BTN_ON    lv_color_hex(0x1a3a20)
#define C_BTN_OFF   lv_color_hex(0x2d1a1a)
#define C_BDR_ON    lv_color_hex(0x3fb950)
#define C_BDR_OFF   lv_color_hex(0xf85149)

/* ------------------------------------------------------------ Geometry -- */
#define SCR_W   600
#define SCR_H   1024
#define PAD     10
#define RADIUS  12

#define BAR_H       46            /* status bar */

#define LIGHT_Y     (BAR_H + PAD)
#define LIGHT_H     238
#define LIGHT_X     PAD
#define LIGHT_W     (SCR_W - PAD * 2)

#define HAM_Y0      (LIGHT_Y + LIGHT_H + PAD)
#define HAM_H       196
#define HAM_GAP     PAD
#define HAM_X       PAD
#define HAM_W       (SCR_W - PAD * 2)

#define NAV_H       70
#define NAV_Y       (SCR_H - NAV_H)

/* Idle dim — matches the other panels. */
#define DIM_TIMEOUT_MS  (5UL * 60UL * 1000UL)
#define DIM_PERCENT     10

/* ---------------------------------------------------------- Screen id -- */
typedef enum { SCREEN_HOME, SCREEN_ENERGY, SCREEN_CHART } screen_t;
static screen_t s_screen = SCREEN_HOME;

/* ------------------------------------------------------- Cached state -- */
static ha_ham_data_t   s_ham;    static bool s_has_ham;
static ha_light_data_t s_light;  static bool s_has_light;
static ha_data_t       s_energy; static bool s_has_energy;
static bool            s_connected;

static bool     s_dimmed;
static uint32_t s_last_activity_tick;

/* --------------------------------------------------------- Callbacks --- */
static void (*s_ham_toggle_cb)(int switch_idx);
static void (*s_light_toggle_cb)(void);
static void (*s_light_brightness_cb)(int percent);
static void (*s_light_rgb_cb)(uint8_t r, uint8_t g, uint8_t b);
static void (*s_chart_request_cb)(void);

void ui_office_set_ham_toggle_cb(void (*cb)(int))            { s_ham_toggle_cb = cb; }
void ui_office_set_light_toggle_cb(void (*cb)(void))         { s_light_toggle_cb = cb; }
void ui_office_set_light_brightness_cb(void (*cb)(int))      { s_light_brightness_cb = cb; }
void ui_office_set_light_rgb_cb(void (*cb)(uint8_t,uint8_t,uint8_t)) { s_light_rgb_cb = cb; }
void ui_office_set_chart_request_cb(void (*cb)(void))        { s_chart_request_cb = cb; }

/* ------------------------------------------------- HOME-screen widgets -- */
static lv_obj_t *s_time_label;
static lv_obj_t *s_status_dot;

/* light card */
static lv_obj_t *s_light_card;
static lv_obj_t *s_bulb_glass;
static lv_obj_t *s_bulb_base;
static lv_obj_t *s_light_state_lbl;
static lv_obj_t *s_light_hint_lbl;

/* ham cards */
static struct {
    lv_obj_t *card;
    lv_obj_t *state_lbl;
    lv_obj_t *power_lbl;
} s_ham_btn[HAM_NUM_SWITCHES];
static const char *s_ham_names[HAM_NUM_SWITCHES] = { HAM_SW_NAMES_INIT };
static const int   s_ham_power_idx[HAM_NUM_SWITCHES] = { 0, -1, 1 };

/* light popup */
static lv_obj_t *s_popup;              /* NULL when closed */
static lv_obj_t *s_popup_bri_lbl;

/* ------------------------------------------------- ENERGY-screen widgets */
static lv_obj_t *s_e_net_val;
static lv_obj_t *s_e_gross_val;
static lv_obj_t *s_e_grid_val;
static lv_obj_t *s_e_grid_lbl;
static lv_obj_t *s_e_solar_val;
static lv_obj_t *s_e_solar_sub;
static lv_obj_t *s_e_top_name;
static lv_obj_t *s_e_top_val;
static lv_obj_t *s_e_top_bar;
#define E_ROWS 8
static struct { lv_obj_t *name, *bar, *val; } s_e_rows[E_ROWS];

/* ===================================================== small helpers === */

static lv_obj_t *mk_card(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_set_pos(c, x, y);
    lv_obj_set_size(c, w, h);
    lv_obj_set_style_bg_color(c, C_CARD, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(c, C_BORDER, 0);
    lv_obj_set_style_border_width(c, 1, 0);
    lv_obj_set_style_radius(c, RADIUS, 0);
    lv_obj_set_style_pad_all(c, 12, 0);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    return c;
}

static lv_obj_t *mk_label(lv_obj_t *parent, const char *txt, const lv_font_t *font,
                          lv_color_t col, lv_align_t align, int x, int y)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, col, 0);
    lv_obj_align(l, align, x, y);
    return l;
}

static lv_obj_t *mk_bar(lv_obj_t *parent, int x, int y, int w, int h, lv_color_t fill)
{
    lv_obj_t *bar = lv_bar_create(parent);
    lv_obj_set_pos(bar, x, y);
    lv_obj_set_size(bar, w, h);
    lv_bar_set_range(bar, 0, 1000);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar, C_BAR_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, fill, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 4, LV_PART_INDICATOR);
    lv_obj_set_style_border_width(bar, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(bar, 0, LV_PART_MAIN);
    return bar;
}

static void note_activity(void)
{
    s_last_activity_tick = lv_tick_get();
    if (s_dimmed) { board_backlight_set_percent(100); s_dimmed = false; }
}

/* ===================================================== bulb graphic ==== */
/*
 * State-aware "graphic": a round glass + a base. ON → glass filled with the
 * light's current colour and a bright ring; OFF → dark glass, grey ring.
 */
static void bulb_create(lv_obj_t *parent)
{
    s_bulb_glass = lv_obj_create(parent);
    lv_obj_set_size(s_bulb_glass, 120, 120);
    lv_obj_align(s_bulb_glass, LV_ALIGN_LEFT_MID, 6, -8);
    lv_obj_set_style_radius(s_bulb_glass, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s_bulb_glass, 4, 0);
    lv_obj_clear_flag(s_bulb_glass, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_bulb_glass, LV_OBJ_FLAG_CLICKABLE);

    s_bulb_base = lv_obj_create(parent);
    lv_obj_set_size(s_bulb_base, 44, 26);
    lv_obj_align_to(s_bulb_base, s_bulb_glass, LV_ALIGN_OUT_BOTTOM_MID, 0, -2);
    lv_obj_set_style_radius(s_bulb_base, 4, 0);
    lv_obj_set_style_border_width(s_bulb_base, 0, 0);
    lv_obj_set_style_bg_color(s_bulb_base, C_TXT2, 0);
    lv_obj_set_style_bg_opa(s_bulb_base, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_bulb_base, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_bulb_base, LV_OBJ_FLAG_CLICKABLE);
}

static void bulb_apply(bool on, uint8_t r, uint8_t g, uint8_t b, int pct)
{
    if (!s_bulb_glass) return;
    if (on) {
        lv_color_t col = lv_color_make(r, g, b);
        /* very dark colours read as "off" — floor the glow */
        if ((int)r + g + b < 120) col = C_AMBER;
        lv_obj_set_style_bg_color(s_bulb_glass, col, 0);
        lv_obj_set_style_bg_opa(s_bulb_glass, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(s_bulb_glass, lv_color_white(), 0);
        lv_obj_set_style_shadow_width(s_bulb_glass, 28, 0);
        lv_obj_set_style_shadow_color(s_bulb_glass, col, 0);
        lv_obj_set_style_shadow_opa(s_bulb_glass, LV_OPA_70, 0);
    } else {
        lv_obj_set_style_bg_color(s_bulb_glass, C_CARD, 0);
        lv_obj_set_style_bg_opa(s_bulb_glass, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(s_bulb_glass, C_TXT2, 0);
        lv_obj_set_style_shadow_width(s_bulb_glass, 0, 0);
    }
    if (s_light_state_lbl) {
        char buf[24];
        if (on && pct > 0) snprintf(buf, sizeof(buf), "ON  %d%%", pct);
        else if (on)       snprintf(buf, sizeof(buf), "ON");
        else               snprintf(buf, sizeof(buf), "OFF");
        lv_label_set_text(s_light_state_lbl, buf);
        lv_obj_set_style_text_color(s_light_state_lbl, on ? C_GREEN : C_TXT2, 0);
    }
}

/* ===================================================== light popup ===== */

static const struct { const char *name; uint8_t r, g, b; } s_swatches[] = {
    { "Warm",  255, 214, 170 },
    { "Cool",  255, 255, 255 },
    { "Red",   255,   0,   0 },
    { "Orange",255, 120,   0 },
    { "Yellow",255, 220,   0 },
    { "Green",   0, 200,  60 },
    { "Cyan",    0, 200, 220 },
    { "Blue",   40,  90, 255 },
};
#define N_SWATCH  (int)(sizeof(s_swatches) / sizeof(s_swatches[0]))

static void popup_close(void)
{
    if (s_popup) { lv_obj_delete(s_popup); s_popup = NULL; s_popup_bri_lbl = NULL; }
}

static void popup_close_cb(lv_event_t *e) { (void)e; note_activity(); popup_close(); }

static void popup_bg_cb(lv_event_t *e)
{
    /* tap outside the panel closes */
    if (lv_event_get_target(e) == lv_event_get_current_target(e)) popup_close();
}

static void bri_slider_cb(lv_event_t *e)
{
    lv_obj_t *sl = lv_event_get_target(e);
    int v = lv_slider_get_value(sl);
    if (s_popup_bri_lbl) {
        char buf[16]; snprintf(buf, sizeof(buf), "%d%%", v);
        lv_label_set_text(s_popup_bri_lbl, buf);
    }
    note_activity();
    if (lv_event_get_code(e) == LV_EVENT_RELEASED && s_light_brightness_cb)
        s_light_brightness_cb(v);
}

static void swatch_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    note_activity();
    if (i >= 0 && i < N_SWATCH && s_light_rgb_cb)
        s_light_rgb_cb(s_swatches[i].r, s_swatches[i].g, s_swatches[i].b);
}

static void popup_open(void)
{
    if (s_popup) return;

    s_popup = lv_obj_create(lv_screen_active());
    lv_obj_set_size(s_popup, SCR_W, SCR_H);
    lv_obj_set_pos(s_popup, 0, 0);
    lv_obj_set_style_bg_color(s_popup, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_popup, LV_OPA_60, 0);
    lv_obj_set_style_border_width(s_popup, 0, 0);
    lv_obj_set_style_radius(s_popup, 0, 0);
    lv_obj_clear_flag(s_popup, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_popup, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_popup, popup_bg_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *panel = lv_obj_create(s_popup);
    lv_obj_set_size(panel, 520, 640);
    lv_obj_center(panel);
    lv_obj_set_style_bg_color(panel, C_CARD, 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(panel, C_BORDER, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_radius(panel, RADIUS, 0);
    lv_obj_set_style_pad_all(panel, 18, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    mk_label(panel, "Office Fan Lights", &lv_font_montserrat_24, C_TXT,
             LV_ALIGN_TOP_LEFT, 0, 0);

    mk_label(panel, "Brightness", &lv_font_montserrat_16, C_TXT2,
             LV_ALIGN_TOP_LEFT, 0, 48);
    s_popup_bri_lbl = mk_label(panel, "--%", &lv_font_montserrat_16, C_AMBER,
                               LV_ALIGN_TOP_RIGHT, 0, 48);

    lv_obj_t *sl = lv_slider_create(panel);
    lv_obj_set_width(sl, 484);
    lv_obj_align(sl, LV_ALIGN_TOP_MID, 0, 80);
    lv_slider_set_range(sl, 1, 100);
    lv_slider_set_value(sl, (s_has_light && s_light.brightness_pct > 0)
                             ? s_light.brightness_pct : 50, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(sl, C_BAR_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sl, C_AMBER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sl, C_AMBER, LV_PART_KNOB);
    lv_obj_add_event_cb(sl, bri_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(sl, bri_slider_cb, LV_EVENT_RELEASED, NULL);
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d%%", (int)lv_slider_get_value(sl));
        lv_label_set_text(s_popup_bri_lbl, buf);
    }

    mk_label(panel, "Colour", &lv_font_montserrat_16, C_TXT2,
             LV_ALIGN_TOP_LEFT, 0, 130);

    for (int i = 0; i < N_SWATCH; i++) {
        int col = i % 4, row = i / 4;
        lv_obj_t *sw = lv_button_create(panel);
        lv_obj_set_size(sw, 108, 90);
        lv_obj_set_pos(sw, col * 122, 160 + row * 104);
        lv_obj_set_style_radius(sw, 10, 0);
        lv_obj_set_style_bg_color(sw, lv_color_make(s_swatches[i].r,
                                                    s_swatches[i].g,
                                                    s_swatches[i].b), 0);
        lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(sw, C_BORDER, 0);
        lv_obj_set_style_border_width(sw, 1, 0);
        lv_obj_add_event_cb(sw, swatch_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        lv_obj_t *nl = lv_label_create(sw);
        lv_label_set_text(nl, s_swatches[i].name);
        lv_obj_set_style_text_font(nl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(nl,
            ((int)s_swatches[i].r + s_swatches[i].g + s_swatches[i].b > 420)
            ? lv_color_black() : lv_color_white(), 0);
        lv_obj_center(nl);
    }

    lv_obj_t *close = lv_button_create(panel);
    lv_obj_set_size(close, 484, 64);
    lv_obj_align(close, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(close, C_BORDER, 0);
    lv_obj_set_style_radius(close, 10, 0);
    lv_obj_add_event_cb(close, popup_close_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cl = lv_label_create(close);
    lv_label_set_text(cl, "Close");
    lv_obj_set_style_text_font(cl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(cl, C_TXT, 0);
    lv_obj_center(cl);
}

/* ===================================================== event handlers == */

static void screen_press_cb(lv_event_t *e) { (void)e; note_activity(); }

static void light_card_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    note_activity();
    if (s_dimmed) return;               /* first tap only wakes the screen */
    if (code == LV_EVENT_LONG_PRESSED) {
        popup_open();
    } else if (code == LV_EVENT_CLICKED) {
        if (!s_popup && s_light_toggle_cb) s_light_toggle_cb();
    }
}

static void ham_card_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    note_activity();
    if (s_dimmed) return;
    if (s_ham_toggle_cb) s_ham_toggle_cb(idx);
}

static void nav_energy_cb(lv_event_t *e);   /* fwd */
static void chart_back_cb(lv_event_t *e);   /* fwd */
static void energy_home_cb(lv_event_t *e);  /* fwd */
static void energy_card_cb(lv_event_t *e);  /* fwd */

/* ===================================================== HOME screen ===== */

static void apply_ham_card(int i, bool on)
{
    lv_obj_set_style_bg_color(s_ham_btn[i].card, on ? C_BTN_ON : C_BTN_OFF, 0);
    lv_obj_set_style_border_color(s_ham_btn[i].card, on ? C_BDR_ON : C_BDR_OFF, 0);
    lv_label_set_text(s_ham_btn[i].state_lbl, on ? "ON" : "OFF");
    lv_obj_set_style_text_color(s_ham_btn[i].state_lbl, on ? C_GREEN : C_RED, 0);
}

static void build_home_on(lv_obj_t *scr)
{
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(scr, screen_press_cb, LV_EVENT_PRESSED, NULL);

    /* ---- status bar ---- */
    mk_label(scr, LV_SYMBOL_HOME " OFFICE PANEL", &lv_font_montserrat_20, C_BLUE,
             LV_ALIGN_TOP_LEFT, PAD + 2, 12);
    s_time_label = mk_label(scr, "--:-- --", &lv_font_montserrat_16, C_TXT2,
                            LV_ALIGN_TOP_RIGHT, -(PAD + 20), 14);
    s_status_dot = lv_obj_create(scr);
    lv_obj_set_size(s_status_dot, 12, 12);
    lv_obj_align(s_status_dot, LV_ALIGN_TOP_RIGHT, -PAD, 16);
    lv_obj_set_style_radius(s_status_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_status_dot, s_connected ? C_DOT_OK : C_DOT_ERR, 0);
    lv_obj_set_style_bg_opa(s_status_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_status_dot, 0, 0);

    lv_obj_t *sep = lv_obj_create(scr);
    lv_obj_set_pos(sep, 0, BAR_H - 6);
    lv_obj_set_size(sep, SCR_W, 1);
    lv_obj_set_style_bg_color(sep, C_BORDER, 0);
    lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sep, 0, 0);
    lv_obj_set_style_radius(sep, 0, 0);

    /* ---- Office Fan Lights card ---- */
    s_light_card = mk_card(scr, LIGHT_X, LIGHT_Y, LIGHT_W, LIGHT_H);
    lv_obj_add_flag(s_light_card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_light_card, light_card_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_light_card, light_card_cb, LV_EVENT_LONG_PRESSED, NULL);

    mk_label(s_light_card, "OFFICE FAN LIGHTS", &lv_font_montserrat_16, C_TXT2,
             LV_ALIGN_TOP_LEFT, 0, 0);
    bulb_create(s_light_card);
    s_light_state_lbl = mk_label(s_light_card, "OFF", &lv_font_montserrat_32, C_TXT2,
                                 LV_ALIGN_RIGHT_MID, -10, -16);
    s_light_hint_lbl = mk_label(s_light_card,
                                LV_SYMBOL_SETTINGS "  hold for brightness / colour",
                                &lv_font_montserrat_14, C_TXT2,
                                LV_ALIGN_BOTTOM_MID, 0, 4);

    /* ---- HAM switch cards ---- */
    static const char *hint = LV_SYMBOL_REFRESH "  tap to toggle";
    for (int i = 0; i < HAM_NUM_SWITCHES; i++) {
        int y = HAM_Y0 + i * (HAM_H + HAM_GAP);
        lv_obj_t *card = lv_obj_create(scr);
        lv_obj_set_pos(card, HAM_X, y);
        lv_obj_set_size(card, HAM_W, HAM_H);
        lv_obj_set_style_bg_color(card, C_BTN_OFF, 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(card, C_BDR_OFF, 0);
        lv_obj_set_style_border_width(card, 2, 0);
        lv_obj_set_style_radius(card, RADIUS, 0);
        lv_obj_set_style_pad_all(card, 14, 0);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(card, ham_card_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        s_ham_btn[i].card = card;

        mk_label(card, s_ham_names[i], &lv_font_montserrat_20, C_TXT2,
                 LV_ALIGN_TOP_LEFT, 0, 0);

        lv_obj_t *st = lv_label_create(card);
        lv_label_set_text(st, "---");
        lv_obj_set_style_text_font(st, &lv_font_montserrat_48, 0);
        lv_obj_set_style_text_color(st, C_TXT2, 0);
        lv_obj_align(st, LV_ALIGN_LEFT_MID, 0, (s_ham_power_idx[i] >= 0) ? -6 : 8);
        s_ham_btn[i].state_lbl = st;

        lv_obj_t *pw = lv_label_create(card);
        if (s_ham_power_idx[i] >= 0) {
            lv_label_set_text(pw, "--- W");
            lv_obj_set_style_text_font(pw, &lv_font_montserrat_24, 0);
            lv_obj_set_style_text_color(pw, C_AMBER, 0);
            lv_obj_align(pw, LV_ALIGN_RIGHT_MID, -6, 0);
        } else {
            lv_label_set_text(pw, "");
        }
        s_ham_btn[i].power_lbl = pw;

        mk_label(card, hint, &lv_font_montserrat_14, C_TXT2,
                 LV_ALIGN_BOTTOM_MID, 0, 4);
    }

    /* ---- bottom ENERGY nav bar ---- */
    lv_obj_t *nav = lv_obj_create(scr);
    lv_obj_set_pos(nav, 0, NAV_Y);
    lv_obj_set_size(nav, SCR_W, NAV_H);
    lv_obj_set_style_bg_color(nav, C_CARD, 0);
    lv_obj_set_style_bg_opa(nav, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(nav, C_BORDER, 0);
    lv_obj_set_style_border_width(nav, 1, 0);
    lv_obj_set_style_radius(nav, 0, 0);
    lv_obj_clear_flag(nav, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(nav, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(nav, nav_energy_cb, LV_EVENT_CLICKED, NULL);
    mk_label(nav, LV_SYMBOL_CHARGE "   ENERGY   " LV_SYMBOL_RIGHT,
             &lv_font_montserrat_24, C_BLUE, LV_ALIGN_CENTER, 0, 0);
}

/* ===================================================== ENERGY screen === */

/* geometry */
#define E_PAD     10
#define E_CARD_Y  (BAR_H + E_PAD)
#define E_TODAY_H 150
#define E_GS_Y    (E_CARD_Y + E_TODAY_H + E_PAD)
#define E_GS_H    140
#define E_SOL_Y   (E_GS_Y + E_GS_H + E_PAD)
#define E_TOP_Y   (E_SOL_Y + E_GS_H + E_PAD)
#define E_TOP_H   120
#define E_CIR_Y   (E_TOP_Y + E_TOP_H + E_PAD)
#define E_CIR_H   (SCR_H - E_CIR_Y - E_PAD)
#define E_W       (SCR_W - E_PAD * 2)
#define E_NAME_W  150
#define E_VAL_W   84
#define E_BAR_W   (E_W - 24 - E_NAME_W - E_VAL_W - E_PAD * 2)

static void build_energy_on(lv_obj_t *scr)
{
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(scr, screen_press_cb, LV_EVENT_PRESSED, NULL);

    mk_label(scr, LV_SYMBOL_CHARGE " ENERGY", &lv_font_montserrat_20, C_BLUE,
             LV_ALIGN_TOP_LEFT, E_PAD + 2, 12);

    lv_obj_t *home = lv_button_create(scr);
    lv_obj_set_size(home, 116, 34);
    lv_obj_align(home, LV_ALIGN_TOP_RIGHT, -E_PAD, 8);
    lv_obj_set_style_bg_color(home, C_BORDER, 0);
    lv_obj_set_style_radius(home, 8, 0);
    lv_obj_add_event_cb(home, energy_home_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *hl = lv_label_create(home);
    lv_label_set_text(hl, LV_SYMBOL_LEFT " HOME");
    lv_obj_set_style_text_font(hl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hl, C_TXT, 0);
    lv_obj_center(hl);

    /* ---- TODAY card: NET | GROSS ---- */
    lv_obj_t *c1 = mk_card(scr, E_PAD, E_CARD_Y, E_W, E_TODAY_H);
    mk_label(c1, "TODAY", &lv_font_montserrat_14, C_TXT2, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *dv = lv_obj_create(c1);
    lv_obj_set_pos(dv, E_W / 2 - 12, 20);
    lv_obj_set_size(dv, 1, E_TODAY_H - 52);
    lv_obj_set_style_bg_color(dv, C_BORDER, 0);
    lv_obj_set_style_bg_opa(dv, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(dv, 0, 0);

    mk_label(c1, "NET", &lv_font_montserrat_14, C_TXT2, LV_ALIGN_TOP_LEFT, 2, 24);
    s_e_net_val = mk_label(c1, "---", &lv_font_montserrat_32, C_AMBER,
                           LV_ALIGN_TOP_LEFT, 2, 44);
    mk_label(c1, "kWh from grid", &lv_font_montserrat_14, C_TXT2,
             LV_ALIGN_TOP_LEFT, 2, 90);

    int rx = E_W / 2 - 2;
    mk_label(c1, "GROSS", &lv_font_montserrat_14, C_TXT2, LV_ALIGN_TOP_LEFT, rx, 24);
    s_e_gross_val = mk_label(c1, "---", &lv_font_montserrat_32, C_GREEN,
                             LV_ALIGN_TOP_LEFT, rx, 44);
    mk_label(c1, "kWh total used", &lv_font_montserrat_14, C_TXT2,
             LV_ALIGN_TOP_LEFT, rx, 90);

    /* ---- GRID card (tap → chart) ---- */
    lv_obj_t *c2 = mk_card(scr, E_PAD, E_GS_Y, E_W, E_GS_H);
    lv_obj_add_flag(c2, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(c2, energy_card_cb, LV_EVENT_CLICKED, NULL);
    mk_label(c2, "GRID  " LV_SYMBOL_RIGHT, &lv_font_montserrat_14, C_TXT2,
             LV_ALIGN_TOP_LEFT, 0, 0);
    s_e_grid_val = mk_label(c2, "--- W", &lv_font_montserrat_32, C_AMBER,
                            LV_ALIGN_LEFT_MID, 4, 4);
    s_e_grid_lbl = mk_label(c2, "IMPORTING", &lv_font_montserrat_20, C_AMBER,
                            LV_ALIGN_BOTTOM_LEFT, 4, -2);

    /* ---- SOLAR card (tap → chart) ---- */
    lv_obj_t *c3 = mk_card(scr, E_PAD, E_SOL_Y, E_W, E_GS_H);
    lv_obj_add_flag(c3, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(c3, energy_card_cb, LV_EVENT_CLICKED, NULL);
    mk_label(c3, LV_SYMBOL_LOOP "  SOLAR  " LV_SYMBOL_RIGHT, &lv_font_montserrat_14,
             C_TXT2, LV_ALIGN_TOP_LEFT, 0, 0);
    s_e_solar_val = mk_label(c3, "--- W", &lv_font_montserrat_32, C_GREEN,
                             LV_ALIGN_LEFT_MID, 4, 4);
    s_e_solar_sub = mk_label(c3, "-- kWh today", &lv_font_montserrat_16, C_TXT2,
                             LV_ALIGN_BOTTOM_LEFT, 4, -2);

    /* ---- TOP CONSUMER ---- */
    lv_obj_t *tc = mk_card(scr, E_PAD, E_TOP_Y, E_W, E_TOP_H);
    mk_label(tc, LV_SYMBOL_WARNING "  TOP CONSUMER", &lv_font_montserrat_14, C_TXT2,
             LV_ALIGN_TOP_LEFT, 0, 0);
    s_e_top_name = mk_label(tc, "---", &lv_font_montserrat_20, C_TXT,
                            LV_ALIGN_TOP_LEFT, 0, 26);
    s_e_top_val = mk_label(tc, "---", &lv_font_montserrat_20, C_AMBER,
                           LV_ALIGN_TOP_RIGHT, 0, 26);
    s_e_top_bar = mk_bar(tc, 0, 66, E_W - 24, 14, C_AMBER);

    /* ---- CIRCUITS ---- */
    lv_obj_t *cir = mk_card(scr, E_PAD, E_CIR_Y, E_W, E_CIR_H);
    mk_label(cir, "CIRCUITS", &lv_font_montserrat_14, C_TXT2, LV_ALIGN_TOP_LEFT, 0, 0);
    int row_h = (E_CIR_H - 44) / E_ROWS;
    for (int i = 0; i < E_ROWS; i++) {
        int ry = 26 + i * row_h;
        int bx = E_NAME_W + E_PAD;
        s_e_rows[i].name = lv_label_create(cir);
        lv_label_set_text(s_e_rows[i].name, "---");
        lv_obj_set_style_text_font(s_e_rows[i].name, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(s_e_rows[i].name, C_TXT, 0);
        lv_obj_set_pos(s_e_rows[i].name, 0, ry + 1);
        lv_obj_set_size(s_e_rows[i].name, E_NAME_W, LV_SIZE_CONTENT);

        s_e_rows[i].bar = mk_bar(cir, bx, ry + 3, E_BAR_W, 10, C_BLUE);

        s_e_rows[i].val = lv_label_create(cir);
        lv_label_set_text(s_e_rows[i].val, "---");
        lv_obj_set_style_text_font(s_e_rows[i].val, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(s_e_rows[i].val, C_TXT2, 0);
        lv_obj_set_pos(s_e_rows[i].val, bx + E_BAR_W + E_PAD, ry + 1);
        lv_obj_set_size(s_e_rows[i].val, E_VAL_W, LV_SIZE_CONTENT);
        lv_obj_set_style_text_align(s_e_rows[i].val, LV_TEXT_ALIGN_RIGHT, 0);
    }
}

static void energy_refresh(void)
{
    if (!s_has_energy || s_screen != SCREEN_ENERGY) return;
    const ha_data_t *d = &s_energy;
    if (!d->valid) return;
    char buf[48];

    if (d->grid_kwh_today >= 0) snprintf(buf, sizeof(buf), "%.1f", d->grid_kwh_today);
    else                        snprintf(buf, sizeof(buf), "---");
    lv_label_set_text(s_e_net_val, buf);

    float gross = -1.0f;
    if (d->grid_kwh_today >= 0 && d->solar_kwh_today >= 0) {
        float exp_kwh = (d->export_kwh_today >= 0) ? d->export_kwh_today : 0.0f;
        gross = d->grid_kwh_today + d->solar_kwh_today - exp_kwh;
    }
    if (gross >= 0) snprintf(buf, sizeof(buf), "%.1f", gross);
    else            snprintf(buf, sizeof(buf), "---");
    lv_label_set_text(s_e_gross_val, buf);

    if (d->net_grid_w >= 0) {
        float kw = d->net_grid_w / 1000.0f;
        if (kw >= 1.0f) snprintf(buf, sizeof(buf), "%.2f kW", kw);
        else            snprintf(buf, sizeof(buf), "%.0f W", d->net_grid_w);
        lv_label_set_text(s_e_grid_val, buf);
        lv_obj_set_style_text_color(s_e_grid_val, C_AMBER, 0);
        lv_label_set_text(s_e_grid_lbl, LV_SYMBOL_DOWN " IMPORTING");
        lv_obj_set_style_text_color(s_e_grid_lbl, C_AMBER, 0);
    } else {
        float kw = (-d->net_grid_w) / 1000.0f;
        if (kw >= 1.0f) snprintf(buf, sizeof(buf), "%.2f kW", kw);
        else            snprintf(buf, sizeof(buf), "%.0f W", -d->net_grid_w);
        lv_label_set_text(s_e_grid_val, buf);
        lv_obj_set_style_text_color(s_e_grid_val, C_GREEN, 0);
        lv_label_set_text(s_e_grid_lbl, LV_SYMBOL_UP " EXPORTING");
        lv_obj_set_style_text_color(s_e_grid_lbl, C_GREEN, 0);
    }

    if (d->solar_power_w >= 0) {
        float kw = d->solar_power_w / 1000.0f;
        if (kw >= 1.0f) snprintf(buf, sizeof(buf), "%.2f kW", kw);
        else            snprintf(buf, sizeof(buf), "%.0f W", d->solar_power_w);
    } else {
        snprintf(buf, sizeof(buf), "---");
    }
    lv_label_set_text(s_e_solar_val, buf);
    if (d->solar_kwh_today >= 0) snprintf(buf, sizeof(buf), "%.2f kWh today", d->solar_kwh_today);
    else                         snprintf(buf, sizeof(buf), "-- kWh today");
    lv_label_set_text(s_e_solar_sub, buf);

    const char **names = ha_circuit_names();
    int idx[HA_NUM_CIRCUITS];
    for (int i = 0; i < HA_NUM_CIRCUITS; i++) idx[i] = i;
    for (int i = 1; i < HA_NUM_CIRCUITS; i++) {
        int key = idx[i];
        float kp = d->circuit_power[key];
        int j = i - 1;
        while (j >= 0) {
            float a = (d->circuit_power[idx[j]] < 0) ? 0 : d->circuit_power[idx[j]];
            float b = (kp < 0) ? 0 : kp;
            if (a <= b) { idx[j + 1] = idx[j]; j--; } else break;
        }
        idx[j + 1] = key;
    }

    float max_p = 1.0f;
    if (d->circuit_power[idx[0]] > max_p) max_p = d->circuit_power[idx[0]];

    int top = idx[0];
    lv_label_set_text(s_e_top_name, names[top]);
    if (d->circuit_power[top] >= 0) {
        if (d->total_power_w > 0) {
            float pct = (d->circuit_power[top] / d->total_power_w) * 100.0f;
            snprintf(buf, sizeof(buf), "%.0f W  %.0f%%", d->circuit_power[top], pct);
        } else {
            snprintf(buf, sizeof(buf), "%.0f W", d->circuit_power[top]);
        }
        lv_label_set_text(s_e_top_val, buf);
        lv_bar_set_value(s_e_top_bar, 1000, LV_ANIM_OFF);
    } else {
        lv_label_set_text(s_e_top_val, "---");
        lv_bar_set_value(s_e_top_bar, 0, LV_ANIM_OFF);
    }

    for (int r = 0; r < E_ROWS; r++) {
        int ci = idx[r + 1];
        float p = d->circuit_power[ci];
        lv_label_set_text(s_e_rows[r].name, names[ci]);
        if (p >= 0) {
            snprintf(buf, sizeof(buf), "%.0f W", p);
            lv_label_set_text(s_e_rows[r].val, buf);
            lv_bar_set_value(s_e_rows[r].bar, (int)((p / max_p) * 1000.0f), LV_ANIM_OFF);
        } else {
            lv_label_set_text(s_e_rows[r].val, "---");
            lv_bar_set_value(s_e_rows[r].bar, 0, LV_ANIM_OFF);
        }
    }
}

/* ===================================================== CHART screen ==== */

#define CH_PAD     10
#define CH_YAXIS_W 46
#define CH_X       (CH_PAD + CH_YAXIS_W)
#define CH_Y       64
#define CH_W       (SCR_W - CH_PAD * 2 - CH_YAXIS_W)
#define CH_H       420
#define CH_PT_X(i) (CH_X + CH_PAD + (i) * (CH_W - CH_PAD * 2) / (HISTORY_DAYS - 1))

void ui_office_show_chart(const ha_history_t *hist)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scr, chart_back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(scr, screen_press_cb, LV_EVENT_PRESSED, NULL);

    mk_label(scr, LV_SYMBOL_CHARGE "  7 Day History", &lv_font_montserrat_20, C_BLUE,
             LV_ALIGN_TOP_LEFT, CH_PAD + 2, 12);
    mk_label(scr, "-- Grid", &lv_font_montserrat_14, C_AMBER,
             LV_ALIGN_TOP_RIGHT, -(CH_PAD + 74), 15);
    mk_label(scr, "-- Solar", &lv_font_montserrat_14, C_GREEN,
             LV_ALIGN_TOP_RIGHT, -CH_PAD, 15);
    mk_label(scr, LV_SYMBOL_LEFT "  Tap anywhere to return", &lv_font_montserrat_14,
             C_TXT2, LV_ALIGN_BOTTOM_MID, 0, -8);

    if (!hist || !hist->valid) {
        mk_label(scr, "History loading...\nAvailable ~20 s after boot.",
                 &lv_font_montserrat_16, C_TXT2, LV_ALIGN_CENTER, 0, 0);
        goto load;
    }

    float y_min = 0.0f, y_max = 1.0f;
    for (int i = 0; i < HISTORY_DAYS; i++) {
        if (hist->grid[i]  < y_min) y_min = hist->grid[i];
        if (hist->grid[i]  > y_max) y_max = hist->grid[i];
        if (hist->solar[i] > y_max) y_max = hist->solar[i];
    }
    float lo = (y_min < 0) ? y_min * 1.15f : 0.0f;
    float hi = y_max * 1.15f;
    if (hi < 1.0f) hi = 1.0f;
    int32_t range_lo = (int32_t)(lo * 10.0f);
    int32_t range_hi = (int32_t)(hi * 10.0f);
    if (range_hi <= range_lo) range_hi = range_lo + 10;

    lv_obj_t *chart = lv_chart_create(scr);
    lv_obj_set_pos(chart, CH_X, CH_Y);
    lv_obj_set_size(chart, CH_W, CH_H);
    lv_obj_set_style_bg_color(chart, C_CARD, 0);
    lv_obj_set_style_bg_opa(chart, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(chart, C_BORDER, 0);
    lv_obj_set_style_border_width(chart, 1, 0);
    lv_obj_set_style_radius(chart, RADIUS, 0);
    lv_obj_set_style_pad_all(chart, CH_PAD, 0);
    lv_obj_clear_flag(chart, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(chart, LV_OBJ_FLAG_CLICKABLE);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart, HISTORY_DAYS);
    lv_chart_set_div_line_count(chart, 4, 0);
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, range_lo, range_hi);
    lv_obj_set_style_line_color(chart, C_BORDER, LV_PART_MAIN);
    lv_obj_set_style_size(chart, 0, 0, LV_PART_INDICATOR);
    lv_obj_set_style_line_width(chart, 3, LV_PART_ITEMS);

    lv_chart_series_t *sg = lv_chart_add_series(chart, C_AMBER, LV_CHART_AXIS_PRIMARY_Y);
    for (int i = 0; i < HISTORY_DAYS; i++)
        lv_chart_set_next_value(chart, sg, (lv_value_precise_t)(hist->grid[i] * 10.0f));
    lv_chart_series_t *ss = lv_chart_add_series(chart, C_GREEN, LV_CHART_AXIS_PRIMARY_Y);
    for (int i = 0; i < HISTORY_DAYS; i++)
        lv_chart_set_next_value(chart, ss, (lv_value_precise_t)(hist->solar[i] * 10.0f));

    {
        int n = 6, inner_h = CH_H - 2 * CH_PAD;
        for (int k = 0; k < n; k++) {
            float v10 = (float)range_hi - (float)k * (float)(range_hi - range_lo) / (float)(n - 1);
            int y = CH_Y + CH_PAD + inner_h * k / (n - 1) - 7;
            char tb[10];
            snprintf(tb, sizeof(tb), "%.1f", v10 / 10.0f);
            lv_obj_t *yl = lv_label_create(scr);
            lv_label_set_text(yl, tb);
            lv_obj_set_style_text_font(yl, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(yl, C_TXT2, 0);
            lv_obj_set_pos(yl, CH_PAD, y);
            lv_obj_set_size(yl, CH_YAXIS_W - 6, 16);
            lv_obj_set_style_text_align(yl, LV_TEXT_ALIGN_RIGHT, 0);
        }
        int day_y = CH_Y + CH_H + 6;
        for (int i = 0; i < HISTORY_DAYS; i++) {
            lv_obj_t *dl = lv_label_create(scr);
            lv_label_set_text(dl, hist->day_labels[i]);
            lv_obj_set_style_text_font(dl, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(dl, C_TXT2, 0);
            lv_obj_set_pos(dl, CH_PT_X(i) - 14, day_y);
        }
    }

load:
    {
        lv_obj_t *old = lv_screen_active();
        lv_screen_load(scr);
        lv_obj_delete_async(old);
    }
    s_screen = SCREEN_CHART;
    s_last_activity_tick = lv_tick_get();
}

/* ===================================================== screen swaps ==== */

static void load_home(void)
{
    lv_obj_t *old = lv_screen_active();
    lv_obj_t *scr = lv_obj_create(NULL);
    build_home_on(scr);
    lv_screen_load(scr);
    lv_obj_delete_async(old);
    s_screen = SCREEN_HOME;
    note_activity();
    if (s_has_ham)   ui_office_update_ham(&s_ham);
    if (s_has_light) ui_office_update_light(&s_light);
}

static void load_energy(void)
{
    lv_obj_t *old = lv_screen_active();
    lv_obj_t *scr = lv_obj_create(NULL);
    build_energy_on(scr);
    lv_screen_load(scr);
    lv_obj_delete_async(old);
    s_screen = SCREEN_ENERGY;
    note_activity();
    energy_refresh();
}

static void nav_energy_cb(lv_event_t *e)   { (void)e; note_activity(); if (!s_dimmed) load_energy(); }
static void energy_home_cb(lv_event_t *e)  { (void)e; note_activity(); load_home(); }
static void chart_back_cb(lv_event_t *e)   { (void)e; load_energy(); }

static void energy_card_cb(lv_event_t *e)
{
    (void)e;
    note_activity();
    if (s_dimmed) return;
    if (s_chart_request_cb) s_chart_request_cb();
}

/* ===================================================== timers ========== */

static void clock_tick_cb(lv_timer_t *t)
{
    (void)t;
    if (s_screen != SCREEN_HOME || !s_time_label) return;
    time_t now; time(&now);
    if (now < 1000000UL) { lv_label_set_text(s_time_label, "-- : --"); return; }
    struct tm tm; localtime_r(&now, &tm);
    char buf[32];
    strftime(buf, sizeof(buf), "%I:%M %p  %a %b %d", &tm);
    lv_label_set_text(s_time_label, buf);
}

static void dimmer_cb(lv_timer_t *t)
{
    (void)t;
    uint32_t elapsed = lv_tick_get() - s_last_activity_tick;
    if (!s_dimmed && elapsed >= DIM_TIMEOUT_MS) {
        board_backlight_set_percent(DIM_PERCENT); s_dimmed = true;
    } else if (s_dimmed && elapsed < DIM_TIMEOUT_MS) {
        board_backlight_set_percent(100); s_dimmed = false;
    }
}

/* ===================================================== public API ===== */

void ui_office_init(void)
{
    s_last_activity_tick = lv_tick_get();
    s_dimmed = false;
    s_screen = SCREEN_HOME;

    build_home_on(lv_screen_active());

    lv_timer_create(clock_tick_cb, 1000, NULL);
    lv_timer_create(dimmer_cb, 10000, NULL);

    ESP_LOGI(TAG, "Office Panel UI ready (600x1024 portrait)");
}

void ui_office_set_connected(bool connected)
{
    s_connected = connected;
    if (s_screen == SCREEN_HOME && s_status_dot)
        lv_obj_set_style_bg_color(s_status_dot, connected ? C_DOT_OK : C_DOT_ERR, 0);
}

void ui_office_update_ham(const ha_ham_data_t *d)
{
    if (!d->valid) return;
    s_ham = *d; s_has_ham = true;
    if (s_screen != SCREEN_HOME) return;

    char buf[32];
    for (int i = 0; i < HAM_NUM_SWITCHES; i++) {
        apply_ham_card(i, d->sw[i]);
        int pi = s_ham_power_idx[i];
        if (pi >= 0 && d->power[pi] >= 0.0f) {
            if (d->power[pi] >= 1000.0f)
                snprintf(buf, sizeof(buf), "%.2f kW", d->power[pi] / 1000.0f);
            else
                snprintf(buf, sizeof(buf), "%.0f W", d->power[pi]);
            lv_label_set_text(s_ham_btn[i].power_lbl, buf);
        }
    }
    if (s_status_dot)
        lv_obj_set_style_bg_color(s_status_dot, s_connected ? C_DOT_OK : C_DOT_ERR, 0);
}

void ui_office_update_light(const ha_light_data_t *d)
{
    if (!d->valid) return;
    s_light = *d; s_has_light = true;
    if (s_screen != SCREEN_HOME) return;
    bulb_apply(d->on, d->r, d->g, d->b, d->brightness_pct);
}

void ui_office_update_energy(const ha_data_t *d)
{
    if (!d->valid) return;
    s_energy = *d; s_has_energy = true;
    energy_refresh();
}

#endif /* DEVICE_TYPE_OFFICE_PANEL */
