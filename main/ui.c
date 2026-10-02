/*
 * ui.c -- Energy dashboard for 800x480, LVGL 9.
 *
 * Screens:
 *   SCREEN_MAIN  -- status bar, three stat cards, top-consumer, circuit list.
 *   SCREEN_CHART -- 7-day dual-series chart (grid amber + solar green); tap anywhere -> main.
 *   SCREEN_WEATHER -- (HAS_WEATHER only, boot default) current conditions,
 *                   next 12 hours, 5-day outlook. "ENERGY >" in its status
 *                   bar opens SCREEN_MAIN; "< WEATHER" on SCREEN_MAIN returns.
 *
 * TODAY card shows two values side-by-side:
 *   NET   = energy imported from grid today (kWh)     [amber]
 *   GROSS = total house consumption today (kWh)        [green]
 *           = grid_imported + solar_generated - grid_exported
 *
 * Chart appears instantly because main.c pre-fetches history in ha_hist_task
 * (background, every 10 min) and caches the result.  Tapping either the GRID
 * or SOLAR card calls ui_set_chart_request_cb() with no type arg, and
 * on_chart_requested() in main.c calls ui_show_chart(&s_hist_cache) directly.
 *
 * Compiled only for DEVICE_TYPE_ENERGY builds.
 */
#include "ui.h"
#include "ha_config.h"

#if DEVICE_TYPE == DEVICE_TYPE_ENERGY

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>

#include "esp_log.h"
#include "lvgl.h"
#include "board.h"
#include "ha_history.h"

static const char *TAG = "ui";

/* ---------------------------------------------------------------- Colors */
#define C_BG         lv_color_hex(0x0d1117)
#define C_CARD       lv_color_hex(0x161b22)
#define C_BORDER     lv_color_hex(0x30363d)
#define C_TXT        lv_color_hex(0xe6edf3)
#define C_TXT2       lv_color_hex(0x7d8590)
#define C_AMBER      lv_color_hex(0xe3a435)
#define C_GREEN      lv_color_hex(0x3fb950)
#define C_BLUE       lv_color_hex(0x58a6ff)
#define C_BAR_BG     lv_color_hex(0x21262d)
#define C_DOT_OK     lv_color_hex(0x3fb950)
#define C_DOT_ERR    lv_color_hex(0xf85149)

/* ------------------------------------------------------------ Geometry */
#define SCR_W  800
#define SCR_H  480
#define PAD    6
#define RADIUS 8

/* Stat card positions */
#define CARD_Y   48
#define CARD_H   168
#define CARD_W   ((SCR_W - PAD*4) / 3)
#define CARD1_X  PAD
#define CARD2_X  (CARD1_X + CARD_W + PAD)
#define CARD3_X  (CARD2_X + CARD_W + PAD)

/* Inner half-width of card 1 for the NET/GROSS columns */
#define C1_HALF  ((CARD_W - 20) / 2)   /* content half-point (after pad_all=10) */

/* Top-consumer card */
#define TOP_Y    (CARD_Y + CARD_H + PAD)
#define TOP_H    108
#define TOP_X    PAD
#define TOP_W    (SCR_W - PAD*2)

/* Circuit list */
#define CIR_Y    (TOP_Y + TOP_H + PAD)
#define CIR_H    (SCR_H - CIR_Y - PAD)
#define CIR_X    PAD
#define CIR_W    (SCR_W - PAD*2)
#define CIR_ROWS 5
#define ROW_H    ((CIR_H - 28) / CIR_ROWS)

/* Bar geometry inside circuit list */
#define NAME_W   148
#define VAL_W    80
#define BAR_W    (CIR_W - 20 - NAME_W - VAL_W - PAD*2)

/* Chart screen geometry */
#define Y_AXIS_W      50
#define CHART_X       (PAD + Y_AXIS_W)
#define CHART_Y       48
#define CHART_W       (SCR_W - PAD*2 - Y_AXIS_W)
#define CHART_H       360
#define CHART_PAD     10
#define CHART_PT_X(i) (CHART_X + CHART_PAD + \
                       (i) * (CHART_W - CHART_PAD*2) / (HISTORY_DAYS - 1))

/* -------------------------------------------------------- Idle / dim ---- */
#define DIM_TIMEOUT_MS  (5UL * 60UL * 1000UL)
#define DIM_PERCENT     10

static bool     s_dimmed;
static uint32_t s_last_activity_tick;

/* -------------------------------------------------------- Screen state -- */
typedef enum { SCREEN_MAIN, SCREEN_CHART, SCREEN_WEATHER } screen_t;
static screen_t s_screen = SCREEN_MAIN;

#if defined(HAS_WEATHER)
static ha_weather_t s_last_wx;
static bool         s_has_last_wx;
static time_t       s_wx_fetched_at;
static void build_weather_on(lv_obj_t *scr);
#endif

/* -------------------------------------------------------- Cached data --- */
static ha_data_t s_last_data;
static bool      s_has_last_data;
static bool      s_connected;

/* ----------------------------------------------------- Main-screen widgets */
static lv_obj_t *s_time_label;
static lv_obj_t *s_status_dot;
/* Card 1: TODAY - NET / GROSS */
static lv_obj_t *s_net_val;
static lv_obj_t *s_gross_val;
/* Card 2: GRID */
static lv_obj_t *s_grid_val;
static lv_obj_t *s_grid_lbl;
/* Card 3: SOLAR */
static lv_obj_t *s_solar_val;
static lv_obj_t *s_solar_sub;
/* Top consumer */
static lv_obj_t *s_top_name;
static lv_obj_t *s_top_val;
static lv_obj_t *s_top_bar;
static struct {
    lv_obj_t *name;
    lv_obj_t *bar;
    lv_obj_t *val;
} s_rows[CIR_ROWS];

/* -------------------------------------------------- Chart request callback */
static void (*s_chart_request_cb)(void);

void ui_set_chart_request_cb(void (*cb)(void))
{
    s_chart_request_cb = cb;
}

/* ======================================================= Helpers ========= */

static lv_obj_t *make_card(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, w, h);
    lv_obj_set_style_bg_color(card, C_CARD, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card, C_BORDER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, RADIUS, 0);
    lv_obj_set_style_pad_all(card, 10, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
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

static lv_obj_t *make_bar(lv_obj_t *parent, int x, int y, int w, int h,
                           lv_color_t fill_col)
{
    lv_obj_t *bar = lv_bar_create(parent);
    lv_obj_set_pos(bar, x, y);
    lv_obj_set_size(bar, w, h);
    lv_bar_set_range(bar, 0, 1000);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar, C_BAR_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, fill_col, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 4, LV_PART_INDICATOR);
    lv_obj_set_style_border_width(bar, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(bar, 0, LV_PART_MAIN);
    return bar;
}

/* ======================================================= Timers ========== */

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
    if ((s_screen != SCREEN_MAIN && s_screen != SCREEN_WEATHER) || !s_time_label)
        return;
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

/* ======================================================= Navigation ====== */

static void build_main_on(lv_obj_t *scr);

/* Swap in a freshly built screen and free the old one (same pattern as the
 * chart screen). Counts as user activity, so the backlight comes back up. */
static void load_screen(lv_obj_t *scr, screen_t which)
{
    lv_obj_t *old = lv_screen_active();
    lv_screen_load(scr);
    lv_obj_delete_async(old);

    s_screen = which;
    s_last_activity_tick = lv_tick_get();
    s_dimmed = false;
    board_backlight_set_percent(100);
}

#if defined(HAS_WEATHER)
/* Status-bar navigation button: outlined pill, blue label. */
static lv_obj_t *make_nav_btn(lv_obj_t *parent, const char *txt,
                              int w, lv_align_t align, int x, int y)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, w, 32);
    lv_obj_align(btn, align, x, y);
    lv_obj_set_style_bg_color(btn, C_CARD, 0);
    lv_obj_set_style_bg_color(btn, C_BAR_BG, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(btn, C_BLUE, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_radius(btn, 16, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, txt);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl, C_BLUE, 0);
    lv_obj_center(lbl);
    return btn;
}

static void goto_energy_cb(lv_event_t *e)
{
    (void)e;
    lv_obj_t *scr = lv_obj_create(NULL);
    build_main_on(scr);
    load_screen(scr, SCREEN_MAIN);
    if (s_has_last_data) ui_update(&s_last_data);
}

static void goto_weather_cb(lv_event_t *e)
{
    (void)e;
    lv_obj_t *scr = lv_obj_create(NULL);
    build_weather_on(scr);
    load_screen(scr, SCREEN_WEATHER);
    if (s_has_last_wx) ui_weather_update(&s_last_wx);
}
#endif /* HAS_WEATHER */

/* ======================================================= Main screen ===== */

static void screen_press_cb(lv_event_t *e)
{
    (void)e;
    s_last_activity_tick = lv_tick_get();
    if (s_dimmed) {
        board_backlight_set_percent(100);
        s_dimmed = false;
    }
}

static void chart_card_clicked_cb(lv_event_t *e)
{
    (void)e;
    /* Both GRID and SOLAR cards call the same combined chart callback */
    if (s_chart_request_cb) s_chart_request_cb();
}

static void build_main_on(lv_obj_t *scr)
{
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(scr, screen_press_cb, LV_EVENT_PRESSED, NULL);

    /* ---- Status bar ------------------------------------------- */
    make_label(scr, LV_SYMBOL_CHARGE " ENERGY MONITOR",
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

#if defined(HAS_WEATHER)
    /* Between the title and the clock; clock text is ~190 px wide. */
    lv_obj_t *wx_btn = make_nav_btn(scr, LV_SYMBOL_LEFT " WEATHER", 140,
                                    LV_ALIGN_TOP_RIGHT, -(PAD + 18 + 200), 4);
    lv_obj_add_event_cb(wx_btn, goto_weather_cb, LV_EVENT_CLICKED, NULL);
#endif

    lv_obj_t *sep = lv_obj_create(scr);
    lv_obj_set_pos(sep, 0, 40);
    lv_obj_set_size(sep, SCR_W, 1);
    lv_obj_set_style_bg_color(sep, C_BORDER, 0);
    lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sep, 0, 0);
    lv_obj_set_style_radius(sep, 0, 0);

    /* ---- Card 1: TODAY  (NET | GROSS two-column layout) -------- */
    lv_obj_t *c1 = make_card(scr, CARD1_X, CARD_Y, CARD_W, CARD_H);

    make_label(c1, "TODAY", &lv_font_montserrat_14, C_TXT2,
               LV_ALIGN_TOP_LEFT, 0, 0);

    /* Vertical divider between the two columns */
    lv_obj_t *divider = lv_obj_create(c1);
    lv_obj_set_pos(divider, C1_HALF, 16);
    lv_obj_set_size(divider, 1, CARD_H - 34);
    lv_obj_set_style_bg_color(divider, C_BORDER, 0);
    lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(divider, 0, 0);
    lv_obj_set_style_radius(divider, 0, 0);

    /* --- Left column: NET (grid import kWh) --- */
    make_label(c1, "NET", &lv_font_montserrat_14, C_TXT2,
               LV_ALIGN_TOP_LEFT, 2, 20);

    s_net_val = lv_label_create(c1);
    lv_label_set_text(s_net_val, "---");
    lv_obj_set_style_text_font(s_net_val, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(s_net_val, C_AMBER, 0);
    lv_obj_align(s_net_val, LV_ALIGN_TOP_LEFT, 2, 38);

    make_label(c1, "kWh", &lv_font_montserrat_14, C_TXT2,
               LV_ALIGN_TOP_LEFT, 2, 78);
    make_label(c1, "from grid", &lv_font_montserrat_14, C_TXT2,
               LV_ALIGN_TOP_LEFT, 2, 96);

    /* --- Right column: GROSS (total consumption kWh) --- */
    int rx = C1_HALF + 6;   /* right column x offset in content coords */

    make_label(c1, "GROSS", &lv_font_montserrat_14, C_TXT2,
               LV_ALIGN_TOP_LEFT, rx, 20);

    s_gross_val = lv_label_create(c1);
    lv_label_set_text(s_gross_val, "---");
    lv_obj_set_style_text_font(s_gross_val, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(s_gross_val, C_GREEN, 0);
    lv_obj_align(s_gross_val, LV_ALIGN_TOP_LEFT, rx, 38);

    make_label(c1, "kWh", &lv_font_montserrat_14, C_TXT2,
               LV_ALIGN_TOP_LEFT, rx, 78);
    make_label(c1, "total used", &lv_font_montserrat_14, C_TXT2,
               LV_ALIGN_TOP_LEFT, rx, 96);

    /* ---- Card 2: GRID -- tappable, shows combined chart -------- */
    lv_obj_t *c2 = make_card(scr, CARD2_X, CARD_Y, CARD_W, CARD_H);
    make_label(c2, "GRID  " LV_SYMBOL_RIGHT, &lv_font_montserrat_14, C_TXT2,
               LV_ALIGN_TOP_LEFT, 0, 0);

    s_grid_val = lv_label_create(c2);
    lv_label_set_text(s_grid_val, "--- W");
    lv_obj_set_style_text_font(s_grid_val, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(s_grid_val, C_AMBER, 0);
    lv_obj_align(s_grid_val, LV_ALIGN_CENTER, 0, -12);

    s_grid_lbl = lv_label_create(c2);
    lv_label_set_text(s_grid_lbl, "IMPORTING");
    lv_obj_set_style_text_font(s_grid_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_grid_lbl, C_AMBER, 0);
    lv_obj_align(s_grid_lbl, LV_ALIGN_BOTTOM_MID, 0, -2);

    lv_obj_add_flag(c2, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(c2, chart_card_clicked_cb, LV_EVENT_CLICKED, NULL);

    /* ---- Card 3: SOLAR -- tappable, shows combined chart ------- */
    lv_obj_t *c3 = make_card(scr, CARD3_X, CARD_Y, CARD_W, CARD_H);
    make_label(c3, LV_SYMBOL_LOOP "  SOLAR  " LV_SYMBOL_RIGHT,
               &lv_font_montserrat_14, C_TXT2,
               LV_ALIGN_TOP_LEFT, 0, 0);

    s_solar_val = lv_label_create(c3);
    lv_label_set_text(s_solar_val, "--- W");
    lv_obj_set_style_text_font(s_solar_val, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(s_solar_val, C_GREEN, 0);
    lv_obj_align(s_solar_val, LV_ALIGN_CENTER, 0, -12);

    s_solar_sub = lv_label_create(c3);
    lv_label_set_text(s_solar_sub, "-- kWh today");
    lv_obj_set_style_text_font(s_solar_sub, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_solar_sub, C_TXT2, 0);
    lv_obj_align(s_solar_sub, LV_ALIGN_BOTTOM_MID, 0, -2);

    lv_obj_add_flag(c3, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(c3, chart_card_clicked_cb, LV_EVENT_CLICKED, NULL);

    /* ---- Top consumer card ------------------------------------ */
    lv_obj_t *top_card = make_card(scr, TOP_X, TOP_Y, TOP_W, TOP_H);
    make_label(top_card, LV_SYMBOL_WARNING "  TOP CONSUMER",
               &lv_font_montserrat_14, C_TXT2,
               LV_ALIGN_TOP_LEFT, 0, 0);

    s_top_name = lv_label_create(top_card);
    lv_label_set_text(s_top_name, "---");
    lv_obj_set_style_text_font(s_top_name, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_top_name, C_TXT, 0);
    lv_obj_align(s_top_name, LV_ALIGN_TOP_LEFT, 0, 22);

    s_top_val = lv_label_create(top_card);
    lv_label_set_text(s_top_val, "---");
    lv_obj_set_style_text_font(s_top_val, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_top_val, C_AMBER, 0);
    lv_obj_align(s_top_val, LV_ALIGN_TOP_RIGHT, 0, 22);

    s_top_bar = make_bar(top_card, 0, 54, TOP_W - 20, 14, C_AMBER);

    /* ---- Circuit list ---------------------------------------- */
    lv_obj_t *cir_card = make_card(scr, CIR_X, CIR_Y, CIR_W, CIR_H);
    make_label(cir_card, "CIRCUITS", &lv_font_montserrat_14, C_TXT2,
               LV_ALIGN_TOP_LEFT, 0, 0);

    for (int i = 0; i < CIR_ROWS; i++) {
        int ry = 22 + i * ROW_H;
        int bar_x = NAME_W + PAD;

        s_rows[i].name = lv_label_create(cir_card);
        lv_label_set_text(s_rows[i].name, "---");
        lv_obj_set_style_text_font(s_rows[i].name, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(s_rows[i].name, C_TXT, 0);
        lv_obj_set_pos(s_rows[i].name, 0, ry + 1);
        lv_obj_set_size(s_rows[i].name, NAME_W, LV_SIZE_CONTENT);

        s_rows[i].bar = make_bar(cir_card, bar_x, ry + 3, BAR_W, 10, C_BLUE);

        s_rows[i].val = lv_label_create(cir_card);
        lv_label_set_text(s_rows[i].val, "---");
        lv_obj_set_style_text_font(s_rows[i].val, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(s_rows[i].val, C_TXT2, 0);
        lv_obj_set_pos(s_rows[i].val, bar_x + BAR_W + PAD, ry + 1);
        lv_obj_set_size(s_rows[i].val, VAL_W, LV_SIZE_CONTENT);
        lv_obj_set_style_text_align(s_rows[i].val, LV_TEXT_ALIGN_RIGHT, 0);
    }
}

/* ======================================================= Chart screen ===== */

static void chart_back_cb(lv_event_t *e)
{
    (void)e;
    lv_obj_t *chart_scr = lv_screen_active();

    lv_obj_t *new_scr = lv_obj_create(NULL);
    build_main_on(new_scr);
    lv_screen_load(new_scr);
    lv_obj_delete_async(chart_scr);

    s_screen = SCREEN_MAIN;
    s_last_activity_tick = lv_tick_get();
    s_dimmed = false;
    board_backlight_set_percent(100);

    if (s_has_last_data) ui_update(&s_last_data);
}

/*
 * Show the combined 7-day chart (grid amber + solar green).
 * Called directly from on_chart_requested() in main.c under lvgl lock --
 * no waiting, instant display from cached data.
 */
void ui_show_chart(const ha_history_t *hist)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scr, chart_back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(scr, screen_press_cb, LV_EVENT_PRESSED, NULL);

    /* Title */
    make_label(scr, LV_SYMBOL_CHARGE "  7 Day Energy History",
               &lv_font_montserrat_20, C_BLUE,
               LV_ALIGN_TOP_LEFT, PAD + 2, 10);

    /* Legend */
    make_label(scr, "-- Grid",  &lv_font_montserrat_14, C_AMBER,
               LV_ALIGN_TOP_RIGHT, -(PAD + 74), 13);
    make_label(scr, "-- Solar", &lv_font_montserrat_14, C_GREEN,
               LV_ALIGN_TOP_RIGHT, -PAD, 13);

    /* Return hint */
    make_label(scr, LV_SYMBOL_LEFT "  Tap anywhere to return",
               &lv_font_montserrat_14, C_TXT2,
               LV_ALIGN_BOTTOM_MID, 0, -6);

    if (!hist || !hist->valid) {
        make_label(scr,
                   "History loading...\n"
                   "Chart will be available ~20 s after boot.\n"
                   "Tap to dismiss and try again shortly.",
                   &lv_font_montserrat_16, C_TXT2,
                   LV_ALIGN_CENTER, 0, 0);
        goto load;
    }

    /* ---- Y-axis range: cover both series ---------------------- */
    float y_min = 0.0f, y_max = 1.0f;
    for (int i = 0; i < HISTORY_DAYS; i++) {
        if (hist->grid[i]  < y_min) y_min = hist->grid[i];
        if (hist->grid[i]  > y_max) y_max = hist->grid[i];
        if (hist->solar[i] > y_max) y_max = hist->solar[i];
    }
    float y_range_lo = (y_min < 0) ? y_min * 1.15f : 0.0f;
    float y_range_hi = y_max * 1.15f;
    if (y_range_hi < 1.0f) y_range_hi = 1.0f;

    /* Store values *10 for one decimal kWh resolution */
    int32_t range_lo = (int32_t)(y_range_lo * 10.0f);
    int32_t range_hi = (int32_t)(y_range_hi * 10.0f);
    if (range_hi <= range_lo) range_hi = range_lo + 10;

    /* ---- Chart widget ----------------------------------------- */
    lv_obj_t *chart = lv_chart_create(scr);
    lv_obj_set_pos(chart, CHART_X, CHART_Y);
    lv_obj_set_size(chart, CHART_W, CHART_H);
    lv_obj_set_style_bg_color(chart, C_CARD, 0);
    lv_obj_set_style_bg_opa(chart, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(chart, C_BORDER, 0);
    lv_obj_set_style_border_width(chart, 1, 0);
    lv_obj_set_style_radius(chart, RADIUS, 0);
    lv_obj_set_style_pad_all(chart, CHART_PAD, 0);
    lv_obj_clear_flag(chart, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(chart, LV_OBJ_FLAG_CLICKABLE);

    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart, HISTORY_DAYS);
    lv_chart_set_div_line_count(chart, 4, 0);
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, range_lo, range_hi);

    lv_obj_set_style_line_color(chart, C_BORDER, LV_PART_MAIN);
    lv_obj_set_style_line_opa(chart, LV_OPA_COVER, LV_PART_MAIN);

    /* Hide data point dots for clean lines */
    lv_obj_set_style_size(chart, 0, 0, LV_PART_INDICATOR);

    /* Line width applies to all series */
    lv_obj_set_style_line_width(chart, 3, LV_PART_ITEMS);

    /* Grid series (amber) */
    lv_chart_series_t *ser_grid = lv_chart_add_series(chart, C_AMBER,
                                                       LV_CHART_AXIS_PRIMARY_Y);
    for (int i = 0; i < HISTORY_DAYS; i++)
        lv_chart_set_next_value(chart, ser_grid,
                                (lv_value_precise_t)(hist->grid[i] * 10.0f));

    /* Solar series (green) */
    lv_chart_series_t *ser_solar = lv_chart_add_series(chart, C_GREEN,
                                                        LV_CHART_AXIS_PRIMARY_Y);
    for (int i = 0; i < HISTORY_DAYS; i++)
        lv_chart_set_next_value(chart, ser_solar,
                                (lv_value_precise_t)(hist->solar[i] * 10.0f));

    /* ---- Y-axis labels ---------------------------------------- */
    {
        int n_ticks = 6;
        int inner_h = CHART_H - 2 * CHART_PAD;
        int lbl_w   = Y_AXIS_W - PAD - 4;
        for (int k = 0; k < n_ticks; k++) {
            float val_s10 = (float)range_hi
                          - (float)k * (float)(range_hi - range_lo) / (float)(n_ticks - 1);
            float val_kwh = val_s10 / 10.0f;
            int y_in = CHART_PAD + inner_h * k / (n_ticks - 1);
            int y_sc = CHART_Y + y_in - 7;
            char tbuf[10];
            snprintf(tbuf, sizeof(tbuf), "%.1f", val_kwh);
            lv_obj_t *y_lbl = lv_label_create(scr);
            lv_label_set_text(y_lbl, tbuf);
            lv_obj_set_style_text_font(y_lbl, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(y_lbl, C_TXT2, 0);
            lv_obj_set_pos(y_lbl, PAD, y_sc);
            lv_obj_set_size(y_lbl, lbl_w, 16);
            lv_obj_set_style_text_align(y_lbl, LV_TEXT_ALIGN_RIGHT, 0);
        }
    }

    /* ---- Day labels below chart ------------------------------- */
    {
        int day_y = CHART_Y + CHART_H + 4;
        for (int i = 0; i < HISTORY_DAYS; i++) {
            int cx = CHART_PT_X(i);
            lv_obj_t *lbl = lv_label_create(scr);
            lv_label_set_text(lbl, hist->day_labels[i]);
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(lbl, C_TXT2, 0);
            lv_obj_set_pos(lbl, cx - 14, day_y);
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

/* ======================================================= Weather screen === */
#if defined(HAS_WEATHER)

#ifndef WEATHER_STATION
#define WEATHER_STATION ""
#endif

#define C_SUN     lv_color_hex(0xf2c94c)
#define C_MOON    lv_color_hex(0xc9d1d9)
#define C_CLOUD   lv_color_hex(0x8b949e)
#define C_RAIN    C_BLUE
#define C_STORM   C_AMBER

/* Weather-screen geometry */
#define WX_CUR_Y   48
#define WX_CUR_H   168
#define WX_HR_Y    (WX_CUR_Y + WX_CUR_H + PAD)
#define WX_HR_H    124
#define WX_DAY_Y   (WX_HR_Y + WX_HR_H + PAD)
#define WX_DAY_H   (SCR_H - WX_DAY_Y - PAD)
#define WX_IN_W    (SCR_W - PAD*2 - 20)          /* card content width */
#define WX_HR_COL  (WX_IN_W / WX_HOURS)
#define WX_DAY_COL (WX_IN_W / WX_DAYS)
#define WX_POP_HI  20                             /* % shown in blue */

static lv_obj_t *s_wx_icon;
static lv_obj_t *s_wx_temp;
static lv_obj_t *s_wx_cond;
static lv_obj_t *s_wx_hilo;
static lv_obj_t *s_wx_hum;
static lv_obj_t *s_wx_wind;
static lv_obj_t *s_wx_updated;
static struct { lv_obj_t *hour, *icon, *temp, *pop; } s_wx_hr[WX_HOURS];
static struct { lv_obj_t *dow,  *icon, *hilo, *pop; } s_wx_day[WX_DAYS];

/* Plain filled shape; not clickable so presses fall through to the screen
 * (which is what wakes the backlight). */
static lv_obj_t *wx_shape(lv_obj_t *parent, int x, int y, int w, int h,
                          lv_color_t col, int radius)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, col, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

/* Cloud: a wide rounded base with a bump on top, in an s×s box. */
static void wx_cloud(lv_obj_t *box, int s, int y_off)
{
    wx_shape(box, s * 8 / 100, y_off + s * 40 / 100, s * 84 / 100, s * 30 / 100,
             C_CLOUD, s * 15 / 100);
    wx_shape(box, s * 28 / 100, y_off + s * 20 / 100, s * 40 / 100, s * 40 / 100,
             C_CLOUD, LV_RADIUS_CIRCLE);
}

/* Redraw a condition icon into an existing s×s container. */
static void wx_draw_icon(lv_obj_t *box, wx_cond_t c, int s)
{
    lv_obj_clean(box);
    switch (c) {
    case WX_SUNNY:
        wx_shape(box, s / 8, s / 8, s * 3 / 4, s * 3 / 4, C_SUN, LV_RADIUS_CIRCLE);
        break;
    case WX_CLEAR_NIGHT:   /* crescent: moon disc with a card-coloured bite */
        wx_shape(box, s / 8, s / 8, s * 3 / 4, s * 3 / 4, C_MOON, LV_RADIUS_CIRCLE);
        wx_shape(box, s * 3 / 8, 0, s * 5 / 8, s * 5 / 8, C_CARD, LV_RADIUS_CIRCLE);
        break;
    case WX_PARTLY:
        wx_shape(box, s * 40 / 100, s * 5 / 100, s * 52 / 100, s * 52 / 100,
                 C_SUN, LV_RADIUS_CIRCLE);
        wx_cloud(box, s, s * 15 / 100);
        break;
    case WX_CLOUDY:
        wx_cloud(box, s, s * 5 / 100);
        break;
    case WX_RAIN:
    case WX_SNOW:
        wx_cloud(box, s, -s * 8 / 100);
        for (int i = 0; i < 3; i++) {
            int x = s * (25 + i * 22) / 100;
            if (c == WX_RAIN)
                wx_shape(box, x, s * 70 / 100, LV_MAX(2, s / 16), s * 22 / 100,
                         C_RAIN, 1);
            else
                wx_shape(box, x, s * 72 / 100, s / 8, s / 8, C_TXT, LV_RADIUS_CIRCLE);
        }
        break;
    case WX_STORM: {
        wx_cloud(box, s, -s * 8 / 100);
        lv_obj_t *bolt = lv_label_create(box);
        lv_label_set_text(bolt, LV_SYMBOL_CHARGE);
        lv_obj_set_style_text_font(bolt,
            s >= 64 ? &lv_font_montserrat_32 : &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(bolt, C_STORM, 0);
        lv_obj_align(bolt, LV_ALIGN_BOTTOM_MID, 0, 0);
        break;
    }
    case WX_WIND:
        for (int i = 0; i < 3; i++)
            wx_shape(box, s * (10 + i * 10) / 100, s * (25 + i * 22) / 100,
                     s * (70 - i * 15) / 100, LV_MAX(3, s / 12), C_CLOUD, 2);
        break;
    default: {
        lv_obj_t *q = lv_label_create(box);
        lv_label_set_text(q, "?");
        lv_obj_set_style_text_font(q, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(q, C_TXT2, 0);
        lv_obj_center(q);
        break;
    }
    }
}

static lv_obj_t *wx_icon_box(lv_obj_t *parent, int x, int y, int s)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_remove_style_all(box);
    lv_obj_set_pos(box, x, y);
    lv_obj_set_size(box, s, s);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return box;
}

/* Centred label inside a column [col_x, col_x + col_w). */
static lv_obj_t *wx_col_label(lv_obj_t *parent, int col_x, int col_w, int y,
                              const lv_font_t *font, lv_color_t col)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, "--");
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, col, 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(l, col_w);
    lv_obj_set_pos(l, col_x, y);
    return l;
}

static lv_obj_t *wx_card(lv_obj_t *scr, int y, int h)
{
    lv_obj_t *c = make_card(scr, PAD, y, SCR_W - PAD*2, h);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_CLICKABLE);
    return c;
}

static void build_weather_on(lv_obj_t *scr)
{
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(scr, screen_press_cb, LV_EVENT_PRESSED, NULL);

    /* ---- Status bar: title | clock | dot | ENERGY > ------------ */
    make_label(scr, LV_SYMBOL_HOME " WEATHER  \xE2\x80\xA2  " WEATHER_LOCATION,
               &lv_font_montserrat_20, C_BLUE,
               LV_ALIGN_TOP_LEFT, PAD + 2, 10);

    lv_obj_t *en_btn = make_nav_btn(scr, "ENERGY " LV_SYMBOL_RIGHT, 130,
                                    LV_ALIGN_TOP_RIGHT, -PAD, 4);
    lv_obj_add_event_cb(en_btn, goto_energy_cb, LV_EVENT_CLICKED, NULL);

    s_status_dot = lv_obj_create(scr);
    lv_obj_set_size(s_status_dot, 12, 12);
    lv_obj_align(s_status_dot, LV_ALIGN_TOP_RIGHT, -(PAD + 130 + 10), 14);
    lv_obj_set_style_radius(s_status_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_status_dot, s_connected ? C_DOT_OK : C_DOT_ERR, 0);
    lv_obj_set_style_bg_opa(s_status_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_status_dot, 0, 0);
    lv_obj_clear_flag(s_status_dot, LV_OBJ_FLAG_CLICKABLE);

    s_time_label = make_label(scr, "--:-- --  --- --- --",
                              &lv_font_montserrat_16, C_TXT2,
                              LV_ALIGN_TOP_RIGHT, -(PAD + 130 + 10 + 20), 12);

    lv_obj_t *sep = wx_shape(scr, 0, 40, SCR_W, 1, C_BORDER, 0);
    (void)sep;

    /* ---- Current conditions ------------------------------------ */
    lv_obj_t *cur = wx_card(scr, WX_CUR_Y, WX_CUR_H);
    make_label(cur, "NOW", &lv_font_montserrat_14, C_TXT2,
               LV_ALIGN_TOP_LEFT, 0, 0);
    s_wx_icon = wx_icon_box(cur, 6, 30, 100);

    s_wx_temp = make_label(cur, "--\xC2\xB0", &lv_font_montserrat_48, C_TXT,
                           LV_ALIGN_TOP_LEFT, 130, 14);
    s_wx_cond = make_label(cur, "--", &lv_font_montserrat_24, C_TXT,
                           LV_ALIGN_TOP_LEFT, 130, 74);
    s_wx_hilo = make_label(cur, "H --\xC2\xB0   L --\xC2\xB0", &lv_font_montserrat_20,
                           C_TXT2, LV_ALIGN_TOP_LEFT, 130, 110);

    s_wx_hum  = make_label(cur, LV_SYMBOL_TINT "  Humidity  --", &lv_font_montserrat_20,
                           C_TXT, LV_ALIGN_TOP_LEFT, 470, 22);
    s_wx_wind = make_label(cur, "Wind  --", &lv_font_montserrat_20,
                           C_TXT, LV_ALIGN_TOP_LEFT, 470, 60);
    s_wx_updated = make_label(cur, "Waiting for NWS data...", &lv_font_montserrat_14,
                              C_TXT2, LV_ALIGN_TOP_LEFT, 470, 112);

    /* ---- Next 12 hours ------------------------------------------ */
    lv_obj_t *hr = wx_card(scr, WX_HR_Y, WX_HR_H);
    make_label(hr, "NEXT 12 HOURS", &lv_font_montserrat_14, C_TXT2,
               LV_ALIGN_TOP_LEFT, 0, 0);
    for (int i = 0; i < WX_HOURS; i++) {
        int x = i * WX_HR_COL;
        s_wx_hr[i].hour = wx_col_label(hr, x, WX_HR_COL, 20, &lv_font_montserrat_14, C_TXT2);
        s_wx_hr[i].icon = wx_icon_box(hr, x + (WX_HR_COL - 28) / 2, 38, 28);
        s_wx_hr[i].temp = wx_col_label(hr, x, WX_HR_COL, 68, &lv_font_montserrat_16, C_TXT);
        s_wx_hr[i].pop  = wx_col_label(hr, x, WX_HR_COL, 88, &lv_font_montserrat_14, C_TXT2);
    }

    /* ---- 5-day outlook ------------------------------------------ */
    lv_obj_t *dy = wx_card(scr, WX_DAY_Y, WX_DAY_H);
    make_label(dy, "5-DAY", &lv_font_montserrat_14, C_TXT2,
               LV_ALIGN_TOP_LEFT, 0, 0);
    for (int i = 0; i < WX_DAYS; i++) {
        int x = i * WX_DAY_COL;
        s_wx_day[i].dow  = wx_col_label(dy, x, WX_DAY_COL, 4,  &lv_font_montserrat_16, C_TXT);
        s_wx_day[i].icon = wx_icon_box(dy, x + (WX_DAY_COL - 34) / 2, 26, 34);
        s_wx_day[i].hilo = wx_col_label(dy, x, WX_DAY_COL, 64, &lv_font_montserrat_16, C_TXT);
        s_wx_day[i].pop  = wx_col_label(dy, x, WX_DAY_COL, 86, &lv_font_montserrat_14, C_TXT2);
    }
}

static void fmt_temp(char *buf, size_t n, int16_t t)
{
    if (t == INT16_MIN) snprintf(buf, n, "--\xC2\xB0");
    else                snprintf(buf, n, "%d\xC2\xB0", t);
}

static const char *compass(int bearing)
{
    static const char *pts[16] = { "N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
                                   "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW" };
    return pts[((bearing * 10 + 112) / 225) % 16];
}

static void set_pop(lv_obj_t *lbl, int8_t pop)
{
    if (pop < 0) {
        lv_label_set_text(lbl, "");
        return;
    }
    lv_label_set_text_fmt(lbl, LV_SYMBOL_TINT " %d%%", pop);
    lv_obj_set_style_text_color(lbl, pop >= WX_POP_HI ? C_RAIN : C_TXT2, 0);
}

void ui_weather_update(const ha_weather_t *wx)
{
    if (!wx->valid) return;

    if (wx != &s_last_wx) {
        s_last_wx       = *wx;
        s_has_last_wx   = true;
        s_wx_fetched_at = time(NULL);
    }
    if (s_screen != SCREEN_WEATHER) return;

    char a[16], b[16];

    /* Current */
    wx_draw_icon(s_wx_icon, wx->cond, 100);
    fmt_temp(a, sizeof(a), wx->temp);
    lv_label_set_text(s_wx_temp, a);
    lv_label_set_text(s_wx_cond, ha_weather_cond_label(wx->cond));
    fmt_temp(a, sizeof(a), wx->today_hi);
    fmt_temp(b, sizeof(b), wx->today_lo);
    lv_label_set_text_fmt(s_wx_hilo, "H %s   L %s", a, b);

    if (wx->humidity >= 0)
        lv_label_set_text_fmt(s_wx_hum, LV_SYMBOL_TINT "  Humidity  %d%%", wx->humidity);
    else
        lv_label_set_text(s_wx_hum, LV_SYMBOL_TINT "  Humidity  --");

    if (wx->wind_mph < 0)
        lv_label_set_text(s_wx_wind, "Wind  --");
    else if (wx->wind_mph == 0 || wx->wind_bearing < 0)
        lv_label_set_text_fmt(s_wx_wind, "Wind  %s",
                              wx->wind_mph == 0 ? "calm" : "--");
    else
        lv_label_set_text_fmt(s_wx_wind, "Wind  %s %d mph",
                              compass(wx->wind_bearing), wx->wind_mph);

    if (s_wx_fetched_at > 1000000000) {
        struct tm tm;
        localtime_r(&s_wx_fetched_at, &tm);
        strftime(a, sizeof(a), "%I:%M %p", &tm);
        lv_label_set_text_fmt(s_wx_updated, "NWS " WEATHER_STATION
                              "  \xE2\x80\xA2  updated %s", a[0] == '0' ? a + 1 : a);
    }

    /* Hourly */
    for (int i = 0; i < WX_HOURS; i++) {
        if (i >= wx->n_hourly) {
            lv_label_set_text(s_wx_hr[i].hour, "");
            lv_label_set_text(s_wx_hr[i].temp, "");
            lv_label_set_text(s_wx_hr[i].pop, "");
            lv_obj_clean(s_wx_hr[i].icon);
            continue;
        }
        const wx_hour_t *h = &wx->hourly[i];
        int h12 = h->hour % 12 ? h->hour % 12 : 12;
        lv_label_set_text_fmt(s_wx_hr[i].hour, "%d%s", h12, h->hour < 12 ? "a" : "p");
        wx_draw_icon(s_wx_hr[i].icon, h->cond, 28);
        fmt_temp(a, sizeof(a), h->temp);
        lv_label_set_text(s_wx_hr[i].temp, a);
        set_pop(s_wx_hr[i].pop, h->pop);
    }

    /* Daily */
    for (int i = 0; i < WX_DAYS; i++) {
        if (i >= wx->n_daily) {
            lv_label_set_text(s_wx_day[i].dow, "");
            lv_label_set_text(s_wx_day[i].hilo, "");
            lv_label_set_text(s_wx_day[i].pop, "");
            lv_obj_clean(s_wx_day[i].icon);
            continue;
        }
        const wx_day_t *d = &wx->daily[i];
        lv_label_set_text(s_wx_day[i].dow, i == 0 ? "Today" : d->dow);
        wx_draw_icon(s_wx_day[i].icon, d->cond, 34);
        fmt_temp(a, sizeof(a), d->hi);
        fmt_temp(b, sizeof(b), d->lo);
        lv_label_set_text_fmt(s_wx_day[i].hilo, "%s / %s", a, b);
        set_pop(s_wx_day[i].pop, d->pop);
    }
}

#endif /* HAS_WEATHER */

/* ======================================================= ui_init ========= */

void ui_init(void)
{
    s_last_activity_tick = lv_tick_get();
    s_dimmed = false;
    lv_obj_t *scr = lv_screen_active();
#if defined(HAS_WEATHER)
    s_screen = SCREEN_WEATHER;          /* weather is the boot default */
    build_weather_on(scr);
#else
    s_screen = SCREEN_MAIN;
    build_main_on(scr);
#endif

    lv_timer_create(clock_tick_cb, 1000, NULL);
    lv_timer_create(dimmer_cb, 10000, NULL);

    ESP_LOGI(TAG, "UI ready");
}

/* ======================================================= ui_update ======== */

void ui_update(const ha_data_t *d)
{
    if (!d->valid) return;

    s_last_data     = *d;
    s_has_last_data = true;

    if (s_screen != SCREEN_MAIN) return;

    char buf[48];

    /* Card 1: TODAY - NET (grid import) and GROSS (total consumption) */
    if (d->grid_kwh_today >= 0)
        snprintf(buf, sizeof(buf), "%.1f", d->grid_kwh_today);
    else
        snprintf(buf, sizeof(buf), "---");
    lv_label_set_text(s_net_val, buf);

    /* GROSS = grid_import + solar_generated - grid_export */
    float gross = -1.0f;
    if (d->grid_kwh_today >= 0 && d->solar_kwh_today >= 0) {
        float exp_kwh = (d->export_kwh_today >= 0) ? d->export_kwh_today : 0.0f;
        gross = d->grid_kwh_today + d->solar_kwh_today - exp_kwh;
    }
    if (gross >= 0)
        snprintf(buf, sizeof(buf), "%.1f", gross);
    else
        snprintf(buf, sizeof(buf), "---");
    lv_label_set_text(s_gross_val, buf);

    /* Card 2: GRID */
    if (d->net_grid_w >= 0) {
        float kw = d->net_grid_w / 1000.0f;
        if (kw >= 1.0f) snprintf(buf, sizeof(buf), "%.2f kW", kw);
        else             snprintf(buf, sizeof(buf), "%.0f W",  d->net_grid_w);
        lv_label_set_text(s_grid_val, buf);
        lv_obj_set_style_text_color(s_grid_val, C_AMBER, 0);
        lv_label_set_text(s_grid_lbl, LV_SYMBOL_DOWN " IMPORTING");
        lv_obj_set_style_text_color(s_grid_lbl, C_AMBER, 0);
    } else {
        float kw = (-d->net_grid_w) / 1000.0f;
        if (kw >= 1.0f) snprintf(buf, sizeof(buf), "%.2f kW", kw);
        else             snprintf(buf, sizeof(buf), "%.0f W",  -d->net_grid_w);
        lv_label_set_text(s_grid_val, buf);
        lv_obj_set_style_text_color(s_grid_val, C_GREEN, 0);
        lv_label_set_text(s_grid_lbl, LV_SYMBOL_UP " EXPORTING");
        lv_obj_set_style_text_color(s_grid_lbl, C_GREEN, 0);
    }

    /* Card 3: SOLAR */
    if (d->solar_power_w >= 0) {
        float kw = d->solar_power_w / 1000.0f;
        if (kw >= 1.0f) snprintf(buf, sizeof(buf), "%.2f kW", kw);
        else             snprintf(buf, sizeof(buf), "%.0f W",  d->solar_power_w);
    } else {
        snprintf(buf, sizeof(buf), "---");
    }
    lv_label_set_text(s_solar_val, buf);

    if (d->solar_kwh_today >= 0)
        snprintf(buf, sizeof(buf), "%.2f kWh today", d->solar_kwh_today);
    else
        snprintf(buf, sizeof(buf), "-- kWh today");
    lv_label_set_text(s_solar_sub, buf);

    /* Sort circuits by power descending */
    const char **names = ha_circuit_names();
    int idx[HA_NUM_CIRCUITS];
    for (int i = 0; i < HA_NUM_CIRCUITS; i++) idx[i] = i;
    for (int i = 1; i < HA_NUM_CIRCUITS; i++) {
        int   key = idx[i];
        float kp  = d->circuit_power[key];
        int   j   = i - 1;
        while (j >= 0) {
            float a = (d->circuit_power[idx[j]] < 0) ? 0 : d->circuit_power[idx[j]];
            float b = (kp < 0) ? 0 : kp;
            if (a <= b) { idx[j+1] = idx[j]; j--; } else break;
        }
        idx[j+1] = key;
    }

    float max_p = 1.0f;
    if (d->circuit_power[idx[0]] > max_p) max_p = d->circuit_power[idx[0]];

    int top = idx[0];
    lv_label_set_text(s_top_name, names[top]);
    if (d->circuit_power[top] >= 0) {
        if (d->total_power_w > 0) {
            float pct = (d->circuit_power[top] / d->total_power_w) * 100.0f;
            snprintf(buf, sizeof(buf), "%.0f W  %.0f%%", d->circuit_power[top], pct);
        } else {
            snprintf(buf, sizeof(buf), "%.0f W", d->circuit_power[top]);
        }
        lv_label_set_text(s_top_val, buf);
        lv_bar_set_value(s_top_bar, 1000, LV_ANIM_OFF);
    } else {
        lv_label_set_text(s_top_val, "---");
        lv_bar_set_value(s_top_bar, 0, LV_ANIM_OFF);
    }

    for (int r = 0; r < CIR_ROWS; r++) {
        int   ci = idx[r + 1];
        float p  = d->circuit_power[ci];
        lv_label_set_text(s_rows[r].name, names[ci]);
        if (p >= 0) {
            snprintf(buf, sizeof(buf), "%.0f W", p);
            lv_label_set_text(s_rows[r].val, buf);
            lv_bar_set_value(s_rows[r].bar, (int)((p / max_p) * 1000.0f), LV_ANIM_OFF);
        } else {
            lv_label_set_text(s_rows[r].val, "---");
            lv_bar_set_value(s_rows[r].bar, 0, LV_ANIM_OFF);
        }
    }

    lv_obj_set_style_bg_color(s_status_dot,
                              s_connected ? C_DOT_OK : C_DOT_ERR, 0);
}

void ui_set_connected(bool connected)
{
    s_connected = connected;
    if ((s_screen == SCREEN_MAIN || s_screen == SCREEN_WEATHER) && s_status_dot)
        lv_obj_set_style_bg_color(s_status_dot,
                                  connected ? C_DOT_OK : C_DOT_ERR, 0);
}

#endif /* DEVICE_TYPE_ENERGY */
