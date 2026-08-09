/*
 * ui.c -- Energy dashboard for 800x480, LVGL 9.
 *
 * Screens:
 *   SCREEN_MAIN  -- status bar, three stat cards, top-consumer, circuit list.
 *   SCREEN_CHART -- 7-day dual-series chart (grid amber + solar green); tap anywhere -> main.
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
typedef enum { SCREEN_MAIN, SCREEN_CHART } screen_t;
static screen_t s_screen = SCREEN_MAIN;

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
    if (s_screen != SCREEN_MAIN || !s_time_label) return;
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

/* ======================================================= ui_init ========= */

void ui_init(void)
{
    s_last_activity_tick = lv_tick_get();
    s_dimmed = false;
    s_screen = SCREEN_MAIN;

    lv_obj_t *scr = lv_screen_active();
    build_main_on(scr);

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
    if (s_screen == SCREEN_MAIN && s_status_dot)
        lv_obj_set_style_bg_color(s_status_dot,
                                  connected ? C_DOT_OK : C_DOT_ERR, 0);
}

#endif /* DEVICE_TYPE_ENERGY */
