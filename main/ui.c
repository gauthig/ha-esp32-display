/*
 * ui.c — Energy dashboard for 800x480, LVGL 9.
 *
 * Layout (y positions):
 *   0   – 39  : status bar (title + clock + status dot)
 *   48  – 215 : three stat cards (TODAY kWh | GRID W | SOLAR W)
 *   223 – 330 : top consumer card
 *   338 – 474 : circuit list (top 5 by current draw)
 *
 * Color language:
 *   amber  = grid import (costs money)
 *   green  = solar / grid export (saving/making money)
 *   blue   = informational / solar label
 *   white  = primary text
 *   gray   = secondary text / labels
 */
#include "ui.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>

#include "esp_log.h"
#include "lvgl.h"

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
#define C_PURPLE     lv_color_hex(0xbc8cff)
#define C_BAR_BG     lv_color_hex(0x21262d)
#define C_DOT_OK     lv_color_hex(0x3fb950)
#define C_DOT_ERR    lv_color_hex(0xf85149)

/* ------------------------------------------------------------ Geometry */
#define SCR_W  800
#define SCR_H  480
#define PAD    6
#define RADIUS 8

/* Stat card positions — three equal columns */
#define CARD_Y   48
#define CARD_H   168
#define CARD_W   ((SCR_W - PAD*4) / 3)   /* ~262 */
#define CARD1_X  PAD
#define CARD2_X  (CARD1_X + CARD_W + PAD)
#define CARD3_X  (CARD2_X + CARD_W + PAD)

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
#define ROW_H    ((CIR_H - 28) / CIR_ROWS)   /* 28 = section label row */

/* Bar geometry inside circuit list.
 * Interior of circuit card = CIR_W - 2*pad_all(10) = CIR_W - 20.
 * NAME_W + PAD + BAR_W + PAD + VAL_W == CIR_W - 20 */
#define NAME_W   148
#define VAL_W    80
#define BAR_W    (CIR_W - 20 - NAME_W - VAL_W - PAD*2)

/* -------------------------------------------------------- Static widgets */
static lv_obj_t *s_time_label;
static lv_obj_t *s_status_dot;

/* Card 1 — TODAY */
static lv_obj_t *s_kwh_val;
static lv_obj_t *s_kwh_sub;

/* Card 2 — GRID */
static lv_obj_t *s_grid_val;
static lv_obj_t *s_grid_lbl;

/* Card 3 — SOLAR */
static lv_obj_t *s_solar_val;
static lv_obj_t *s_solar_sub;

/* Top consumer */
static lv_obj_t *s_top_name;
static lv_obj_t *s_top_val;
static lv_obj_t *s_top_bar;

/* Circuit rows */
static struct {
    lv_obj_t *name;
    lv_obj_t *bar;
    lv_obj_t *val;
} s_rows[CIR_ROWS];

static bool s_connected = false;

/* ========================================================= Helpers ===== */

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

/* Simple bar using lv_bar: parent must have known size. */
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

/* ======================================================= ui_init ======== */

static void clock_tick_cb(lv_timer_t *t)
{
    (void)t;
    time_t now;
    time(&now);
    if (now < 1000000UL) {
        /* SNTP not synced yet */
        lv_label_set_text(s_time_label, "-- : -- --");
        return;
    }
    struct tm tm;
    localtime_r(&now, &tm);
    char buf[32];
    strftime(buf, sizeof(buf), "%I:%M %p  %a %b %d", &tm);
    lv_label_set_text(s_time_label, buf);
}

void ui_init(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    /* ---- Status bar --------------------------------------------------- */
    /* Title */
    make_label(scr, LV_SYMBOL_CHARGE " ENERGY MONITOR",
               &lv_font_montserrat_20, C_BLUE,
               LV_ALIGN_TOP_LEFT, PAD + 2, 10);

    /* Clock — updated by LVGL timer */
    s_time_label = make_label(scr, "--:-- --  --- --- --",
                              &lv_font_montserrat_16, C_TXT2,
                              LV_ALIGN_TOP_RIGHT, -(PAD + 18), 12);

    /* Status dot */
    s_status_dot = lv_obj_create(scr);
    lv_obj_set_size(s_status_dot, 12, 12);
    lv_obj_align(s_status_dot, LV_ALIGN_TOP_RIGHT, -PAD, 14);
    lv_obj_set_style_radius(s_status_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_status_dot, C_DOT_ERR, 0);
    lv_obj_set_style_bg_opa(s_status_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_status_dot, 0, 0);

    /* Thin separator line under status bar */
    lv_obj_t *sep = lv_obj_create(scr);
    lv_obj_set_pos(sep, 0, 40);
    lv_obj_set_size(sep, SCR_W, 1);
    lv_obj_set_style_bg_color(sep, C_BORDER, 0);
    lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sep, 0, 0);
    lv_obj_set_style_radius(sep, 0, 0);

    /* ---- Card 1: TODAY kWh -------------------------------------------- */
    lv_obj_t *c1 = make_card(scr, CARD1_X, CARD_Y, CARD_W, CARD_H);
    make_label(c1, "TODAY", &lv_font_montserrat_14, C_TXT2,
               LV_ALIGN_TOP_LEFT, 0, 0);

    s_kwh_val = lv_label_create(c1);
    lv_label_set_text(s_kwh_val, "---");
    lv_obj_set_style_text_font(s_kwh_val, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(s_kwh_val, C_AMBER, 0);
    lv_obj_align(s_kwh_val, LV_ALIGN_CENTER, 0, -8);

    make_label(c1, "kWh", &lv_font_montserrat_20, C_TXT2,
               LV_ALIGN_CENTER, 0, 32);

    s_kwh_sub = lv_label_create(c1);
    lv_label_set_text(s_kwh_sub, "from grid today");
    lv_obj_set_style_text_font(s_kwh_sub, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_kwh_sub, C_TXT2, 0);
    lv_obj_align(s_kwh_sub, LV_ALIGN_BOTTOM_MID, 0, -2);

    /* ---- Card 2: GRID net power --------------------------------------- */
    lv_obj_t *c2 = make_card(scr, CARD2_X, CARD_Y, CARD_W, CARD_H);
    make_label(c2, "GRID", &lv_font_montserrat_14, C_TXT2,
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

    /* ---- Card 3: SOLAR ------------------------------------------------ */
    lv_obj_t *c3 = make_card(scr, CARD3_X, CARD_Y, CARD_W, CARD_H);
    make_label(c3, LV_SYMBOL_LOOP "  SOLAR", &lv_font_montserrat_14, C_TXT2,
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

    /* ---- Top consumer card -------------------------------------------- */
    lv_obj_t *top_card = make_card(scr, TOP_X, TOP_Y, TOP_W, TOP_H);
    make_label(top_card, "TOP CONSUMER", &lv_font_montserrat_14, C_TXT2,
               LV_ALIGN_TOP_LEFT, 0, 0);

    s_top_name = lv_label_create(top_card);
    lv_label_set_text(s_top_name, "---");
    lv_obj_set_style_text_font(s_top_name, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(s_top_name, C_TXT, 0);
    lv_obj_set_pos(s_top_name, 0, 22);

    s_top_val = lv_label_create(top_card);
    lv_label_set_text(s_top_val, "--- W");
    lv_obj_set_style_text_font(s_top_val, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(s_top_val, C_AMBER, 0);
    lv_obj_align(s_top_val, LV_ALIGN_TOP_RIGHT, 0, 22);

    /* Full-width bar below names */
    int bar_y = 54;
    int bar_h = 20;
    s_top_bar = make_bar(top_card, 0, bar_y,
                         TOP_W - 20,  /* 20 = card padding*2 */
                         bar_h, C_AMBER);

    /* ---- Circuit list ------------------------------------------------- */
    lv_obj_t *cir_card = make_card(scr, CIR_X, CIR_Y, CIR_W, CIR_H);
    make_label(cir_card, "CIRCUITS", &lv_font_montserrat_14, C_TXT2,
               LV_ALIGN_TOP_LEFT, 0, 0);

    for (int i = 0; i < CIR_ROWS; i++) {
        int ry = 22 + i * ROW_H;

        s_rows[i].name = lv_label_create(cir_card);
        lv_label_set_text(s_rows[i].name, "---");
        lv_obj_set_style_text_font(s_rows[i].name, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(s_rows[i].name, C_TXT, 0);
        lv_obj_set_pos(s_rows[i].name, 0, ry + 1);
        lv_obj_set_width(s_rows[i].name, NAME_W);
        lv_label_set_long_mode(s_rows[i].name, LV_LABEL_LONG_CLIP);

        int bar_x = NAME_W + PAD;
        s_rows[i].bar = make_bar(cir_card, bar_x, ry + 2,
                                 BAR_W, ROW_H - 6, C_BLUE);

        s_rows[i].val = lv_label_create(cir_card);
        lv_label_set_text(s_rows[i].val, "--- W");
        lv_obj_set_style_text_font(s_rows[i].val, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(s_rows[i].val, C_TXT2, 0);
        lv_obj_set_pos(s_rows[i].val, bar_x + BAR_W + PAD, ry + 1);
        lv_obj_set_size(s_rows[i].val, VAL_W, LV_SIZE_CONTENT);
        lv_obj_set_style_text_align(s_rows[i].val, LV_TEXT_ALIGN_RIGHT, 0);
    }

    /* ---- Clock timer -------------------------------------------------- */
    lv_timer_create(clock_tick_cb, 1000, NULL);

    ESP_LOGI(TAG, "UI initialized");
}

/* ======================================================= ui_update ===== */

void ui_update(const ha_data_t *d)
{
    if (!d->valid) return;

    char buf[48];

    /* ---- Card 1: TODAY kWh -------------------------------------------- */
    if (d->grid_kwh_today >= 0) {
        snprintf(buf, sizeof(buf), "%.1f", d->grid_kwh_today);
    } else {
        snprintf(buf, sizeof(buf), "---");
    }
    lv_label_set_text(s_kwh_val, buf);

    /* Sub-label: add solar kWh if available */
    if (d->solar_kwh_today >= 0) {
        snprintf(buf, sizeof(buf), "grid | %.1f kWh solar", d->solar_kwh_today);
        lv_label_set_text(s_kwh_sub, buf);
    } else {
        lv_label_set_text(s_kwh_sub, "from grid today");
    }

    /* ---- Card 2: GRID net power --------------------------------------- */
    if (d->net_grid_w >= 0) {
        /* Importing from grid */
        float kw = d->net_grid_w / 1000.0f;
        if (kw >= 1.0f)
            snprintf(buf, sizeof(buf), "%.2f kW", kw);
        else
            snprintf(buf, sizeof(buf), "%.0f W", d->net_grid_w);
        lv_label_set_text(s_grid_val, buf);
        lv_obj_set_style_text_color(s_grid_val, C_AMBER, 0);
        lv_label_set_text(s_grid_lbl, LV_SYMBOL_DOWN " IMPORTING");
        lv_obj_set_style_text_color(s_grid_lbl, C_AMBER, 0);
    } else {
        /* Exporting to grid (solar > load) */
        float kw = (-d->net_grid_w) / 1000.0f;
        if (kw >= 1.0f)
            snprintf(buf, sizeof(buf), "%.2f kW", kw);
        else
            snprintf(buf, sizeof(buf), "%.0f W", -d->net_grid_w);
        lv_label_set_text(s_grid_val, buf);
        lv_obj_set_style_text_color(s_grid_val, C_GREEN, 0);
        lv_label_set_text(s_grid_lbl, LV_SYMBOL_UP " EXPORTING");
        lv_obj_set_style_text_color(s_grid_lbl, C_GREEN, 0);
    }

    /* ---- Card 3: SOLAR ------------------------------------------------ */
    if (d->solar_power_w >= 0) {
        float kw = d->solar_power_w / 1000.0f;
        if (kw >= 1.0f)
            snprintf(buf, sizeof(buf), "%.2f kW", kw);
        else
            snprintf(buf, sizeof(buf), "%.0f W", d->solar_power_w);
    } else {
        snprintf(buf, sizeof(buf), "---");
    }
    lv_label_set_text(s_solar_val, buf);

    if (d->solar_kwh_today >= 0) {
        snprintf(buf, sizeof(buf), "%.2f kWh today", d->solar_kwh_today);
    } else {
        snprintf(buf, sizeof(buf), "-- kWh today");
    }
    lv_label_set_text(s_solar_sub, buf);

    /* ---- Sort circuits by power descending ---------------------------- */
    const char **names = ha_circuit_names();
    int idx[HA_NUM_CIRCUITS];
    for (int i = 0; i < HA_NUM_CIRCUITS; i++) idx[i] = i;

    /* Insertion sort (13 items — fast enough) */
    for (int i = 1; i < HA_NUM_CIRCUITS; i++) {
        int key = idx[i];
        float kp = d->circuit_power[key];
        int j = i - 1;
        while (j >= 0) {
            float jp = d->circuit_power[idx[j]];
            /* Treat -1 (unavailable) as 0 for sorting */
            float a = (jp < 0) ? 0 : jp;
            float b = (kp < 0) ? 0 : kp;
            if (a <= b) { idx[j+1] = idx[j]; j--; } else break;
        }
        idx[j+1] = key;
    }

    /* Find max power for bar scaling */
    float max_p = 1.0f;   /* avoid div-by-zero */
    if (d->circuit_power[idx[0]] > max_p)
        max_p = d->circuit_power[idx[0]];

    /* ---- Top consumer ------------------------------------------------- */
    int top = idx[0];
    lv_label_set_text(s_top_name, names[top]);
    if (d->circuit_power[top] >= 0) {
        snprintf(buf, sizeof(buf), "%.0f W", d->circuit_power[top]);
        lv_label_set_text(s_top_val, buf);
        lv_bar_set_value(s_top_bar, 1000, LV_ANIM_OFF);   /* top = 100% */
    } else {
        lv_label_set_text(s_top_val, "---");
        lv_bar_set_value(s_top_bar, 0, LV_ANIM_OFF);
    }

    /* ---- Circuit rows ------------------------------------------------- */
    for (int r = 0; r < CIR_ROWS; r++) {
        int ci = idx[r];
        lv_label_set_text(s_rows[r].name, names[ci]);

        float p = d->circuit_power[ci];
        if (p >= 0) {
            snprintf(buf, sizeof(buf), "%.0f W", p);
            lv_label_set_text(s_rows[r].val, buf);
            int bar_val = (int)((p / max_p) * 1000.0f);
            lv_bar_set_value(s_rows[r].bar, bar_val, LV_ANIM_OFF);
        } else {
            lv_label_set_text(s_rows[r].val, "---");
            lv_bar_set_value(s_rows[r].bar, 0, LV_ANIM_OFF);
        }
    }

    /* ---- Status dot --------------------------------------------------- */
    lv_obj_set_style_bg_color(s_status_dot,
                              s_connected ? C_DOT_OK : C_DOT_ERR, 0);
}

void ui_set_connected(bool connected)
{
    s_connected = connected;
}
