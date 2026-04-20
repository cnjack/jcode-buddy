#include "ui_ble.h"
#include "ui_styles.h"
#include "user_config.h"
#include "lvgl.h"
#include <string.h>
#include <stdbool.h>
#include <math.h>

/* ── Layout ───────────────────────────────────────────────────────────── */
#define MAX_MESSAGES  5
#define PET_AREA_W    100
#define MSG_AREA_W    (EXAMPLE_LCD_H_RES - PET_AREA_W)

/* Canvas: 32x32 source pixels drawn at 2x scale = 64x64 on screen */
#define CAT_SRC_W     32
#define CAT_SRC_H     32
#define CAT_SCALE     2
#define CAT_DISP_W    (CAT_SRC_W * CAT_SCALE)
#define CAT_DISP_H    (CAT_SRC_H * CAT_SCALE)

#define C_BG_PAGE     0x0D0D1A
#define C_PET_BG      0x16213E

/* ── Cat palette ──────────────────────────────────────────────────────── */
#define C_OUTLINE  lv_color_hex(0x2d1b4e)
#define C_FUR      lv_color_hex(0xff6b9d)
#define C_FUR_LT   lv_color_hex(0xff9eb5)
#define C_FUR_DK   lv_color_hex(0xc44569)
#define C_WHITE    lv_color_hex(0xffffff)
#define C_IRIS     lv_color_hex(0x4ecdc4)
#define C_NOSE     lv_color_hex(0xfd79a8)
#define C_CLEAR    lv_color_hex(0x000000)

/* ── State enum ──────────────────────────────────────────────────────── */
typedef enum {
    PET_IDLE = 0, PET_SLEEP, PET_WORKING, PET_ATTENTION, PET_COMPLETE,
} pet_state_t;

/* ── Statics ──────────────────────────────────────────────────────────── */
static lv_obj_t  *s_ble_page;
static lv_obj_t  *s_canvas;
static lv_obj_t  *s_conn_dot;
static lv_obj_t  *s_state_label;
static lv_obj_t  *s_zzz_label;
static lv_obj_t  *s_pet_area;

static uint8_t   *s_cbuf;          /* canvas pixel buffer */
static lv_timer_t *s_anim_timer;
static pet_state_t s_pet_state = PET_SLEEP;
static uint32_t   s_frame = 0;

static lv_obj_t  *s_msg_rows[MAX_MESSAGES];
static lv_obj_t  *s_msg_labels[MAX_MESSAGES];
static char       s_msg_texts[MAX_MESSAGES][128];
static int        s_msg_count = 0;

/* ── Pixel helpers (2x scale) ────────────────────────────────────────── */
static inline void px(int x, int y, lv_color_t c)
{
    if (x < 0 || x >= CAT_SRC_W || y < 0 || y >= CAT_SRC_H) return;
    int sx = x * CAT_SCALE, sy = y * CAT_SCALE;
    for (int dy = 0; dy < CAT_SCALE; dy++)
        for (int dx = 0; dx < CAT_SCALE; dx++)
            lv_canvas_set_px(s_canvas, sx + dx, sy + dy, c, LV_OPA_COVER);
}

/* ── Draw cat parts ──────────────────────────────────────────────────── */
static void draw_body(int ox, int oy, float tail_wave)
{
    /* Tail */
    for (int i = 0; i < 6; i++) {
        int ty = (int)(sinf(i * 0.5f) * tail_wave);
        px(ox + 14 + i, oy + 10 + ty, C_FUR);
    }
    /* Body oval */
    for (int y = 12; y < 22; y++)
        for (int x = 8; x < 18; x++)
            px(ox + x, oy + y, C_FUR);
    /* Body highlight */
    for (int y = 13; y < 21; y++) {
        px(ox + 9, oy + y, C_FUR_LT);
    }
    /* Belly */
    for (int y = 16; y < 21; y++)
        for (int x = 11; x < 15; x++)
            px(ox + x, oy + y, C_WHITE);
}

static void draw_head(int ox, int oy, int eye_type)
{
    /* Ears */
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 3; j++)
            if (i + j < 5) {
                px(ox + 9 + i, oy + 4 + j, C_FUR);
                px(ox + 17 - i, oy + 4 + j, C_FUR);
            }
    /* Inner ears */
    px(ox + 10, oy + 5, C_FUR_DK);
    px(ox + 16, oy + 5, C_FUR_DK);

    /* Head shape */
    for (int y = 6; y < 14; y++)
        for (int x = 7; x < 19; x++)
            px(ox + x, oy + y, C_FUR);
    /* Head highlight */
    for (int y = 7; y < 13; y++) {
        px(ox + 8, oy + y, C_FUR_LT);
    }

    /* Eyes */
    if (eye_type == 0) { /* open */
        px(ox + 10, oy + 9, C_WHITE); px(ox + 11, oy + 9, C_IRIS); px(ox + 12, oy + 9, C_WHITE);
        px(ox + 10, oy + 10, C_WHITE); px(ox + 11, oy + 10, C_WHITE); px(ox + 12, oy + 10, C_WHITE);
        px(ox + 14, oy + 9, C_WHITE); px(ox + 15, oy + 9, C_IRIS); px(ox + 16, oy + 9, C_WHITE);
        px(ox + 14, oy + 10, C_WHITE); px(ox + 15, oy + 10, C_WHITE); px(ox + 16, oy + 10, C_WHITE);
    } else if (eye_type == 1) { /* closed / sleep */
        px(ox + 10, oy + 9, C_OUTLINE); px(ox + 11, oy + 9, C_OUTLINE); px(ox + 12, oy + 9, C_OUTLINE);
        px(ox + 14, oy + 9, C_OUTLINE); px(ox + 15, oy + 9, C_OUTLINE); px(ox + 16, oy + 9, C_OUTLINE);
    } else if (eye_type == 2) { /* happy ^ ^ */
        px(ox + 10, oy + 10, C_OUTLINE); px(ox + 11, oy + 9, C_OUTLINE); px(ox + 12, oy + 10, C_OUTLINE);
        px(ox + 14, oy + 10, C_OUTLINE); px(ox + 15, oy + 9, C_OUTLINE); px(ox + 16, oy + 10, C_OUTLINE);
    } else if (eye_type == 3) { /* angry */
        px(ox + 10, oy + 9, C_OUTLINE); px(ox + 11, oy + 9, C_OUTLINE); px(ox + 12, oy + 10, C_OUTLINE);
        px(ox + 14, oy + 10, C_OUTLINE); px(ox + 15, oy + 9, C_OUTLINE); px(ox + 16, oy + 9, C_OUTLINE);
    } else if (eye_type == 4) { /* wide / surprised */
        px(ox + 10, oy + 8, C_WHITE); px(ox + 11, oy + 8, C_WHITE); px(ox + 12, oy + 8, C_WHITE);
        px(ox + 10, oy + 9, C_WHITE); px(ox + 11, oy + 9, C_IRIS);  px(ox + 12, oy + 9, C_WHITE);
        px(ox + 10, oy + 10, C_WHITE); px(ox + 11, oy + 10, C_WHITE); px(ox + 12, oy + 10, C_WHITE);
        px(ox + 14, oy + 8, C_WHITE); px(ox + 15, oy + 8, C_WHITE); px(ox + 16, oy + 8, C_WHITE);
        px(ox + 14, oy + 9, C_WHITE); px(ox + 15, oy + 9, C_IRIS);  px(ox + 16, oy + 9, C_WHITE);
        px(ox + 14, oy + 10, C_WHITE); px(ox + 15, oy + 10, C_WHITE); px(ox + 16, oy + 10, C_WHITE);
    }

    /* Nose */
    px(ox + 12, oy + 11, C_NOSE); px(ox + 13, oy + 11, C_NOSE);

    /* Mouth */
    if (eye_type == 2) { /* happy smile */
        px(ox + 11, oy + 12, C_OUTLINE); px(ox + 12, oy + 13, C_OUTLINE);
        px(ox + 13, oy + 13, C_OUTLINE); px(ox + 14, oy + 13, C_OUTLINE);
        px(ox + 15, oy + 12, C_OUTLINE);
    } else {
        px(ox + 12, oy + 12, C_OUTLINE); px(ox + 13, oy + 12, C_OUTLINE);
    }

    /* Whiskers */
    px(ox + 6, oy + 10, C_OUTLINE); px(ox + 7, oy + 10, C_OUTLINE);
    px(ox + 6, oy + 11, C_OUTLINE); px(ox + 7, oy + 11, C_OUTLINE);
    px(ox + 19, oy + 10, C_OUTLINE); px(ox + 18, oy + 10, C_OUTLINE);
    px(ox + 19, oy + 11, C_OUTLINE); px(ox + 18, oy + 11, C_OUTLINE);
}

static void draw_legs(int ox, int oy, int fl_dy, int fr_dy)
{
    /* Front left */
    for (int y = 22; y < 26 + fl_dy; y++) { px(ox + 9, oy + y, C_FUR); px(ox + 10, oy + y, C_FUR); }
    px(ox + 9, oy + 25 + fl_dy, C_OUTLINE); px(ox + 10, oy + 25 + fl_dy, C_OUTLINE);
    /* Front right */
    for (int y = 22; y < 26 + fr_dy; y++) { px(ox + 11, oy + y, C_FUR); px(ox + 12, oy + y, C_FUR); }
    px(ox + 11, oy + 25 + fr_dy, C_OUTLINE); px(ox + 12, oy + 25 + fr_dy, C_OUTLINE);
    /* Back left */
    for (int y = 22; y < 26 - fl_dy; y++) { px(ox + 15, oy + y, C_FUR); px(ox + 16, oy + y, C_FUR); }
    px(ox + 15, oy + 25 - fl_dy, C_OUTLINE); px(ox + 16, oy + 25 - fl_dy, C_OUTLINE);
    /* Back right */
    for (int y = 22; y < 26 - fr_dy; y++) { px(ox + 17, oy + y, C_FUR); px(ox + 18, oy + y, C_FUR); }
    px(ox + 17, oy + 25 - fr_dy, C_OUTLINE); px(ox + 18, oy + 25 - fr_dy, C_OUTLINE);
}

/* ── Draw full cat frame ─────────────────────────────────────────────── */
static void draw_cat(pet_state_t state, uint32_t frame)
{
    lv_canvas_fill_bg(s_canvas, lv_color_hex(C_PET_BG), LV_OPA_COVER);
    int ox = 3, oy = 2;

    switch (state) {
    case PET_IDLE: {
        float tw = sinf(frame * 0.12f) * 2.0f;
        draw_body(ox, oy, tw);
        draw_head(ox, oy, 0);
        draw_legs(ox, oy, 0, 0);
        /* Blink every ~3s (at 12fps ≈ 36 frames) */
        if ((frame % 36) < 2) {
            /* Overdraw with closed eyes */
            for (int y = 9; y <= 10; y++)
                for (int x = 10; x <= 16; x++)
                    px(ox + x, oy + y, C_FUR);
            px(ox + 10, oy + 9, C_OUTLINE); px(ox + 11, oy + 9, C_OUTLINE); px(ox + 12, oy + 9, C_OUTLINE);
            px(ox + 14, oy + 9, C_OUTLINE); px(ox + 15, oy + 9, C_OUTLINE); px(ox + 16, oy + 9, C_OUTLINE);
        }
        break;
    }
    case PET_SLEEP: {
        draw_body(ox, oy, 1.0f);
        draw_head(ox, oy, 1);
        draw_legs(ox, oy, 0, 0);
        /* Zzz handled by LVGL label + animation */
        lv_obj_set_style_opa(s_zzz_label, LV_OPA_COVER, 0);
        break;
    }
    case PET_WORKING: {
        /* Walk animation — legs cycle, body bobs, tail waves */
        float wc = sinf(frame * 0.2f);
        float tw = wc * 3.0f;
        int bob = (int)(fabsf(wc) * 2.0f);
        draw_body(ox, oy - bob, tw);
        draw_head(ox, oy - bob, 0);
        draw_legs(ox, oy, (int)(wc * 3), (int)(-wc * 3));
        break;
    }
    case PET_ATTENTION: {
        /* Attack animation — lunge forward with angry eyes + claws */
        float ac = sinf(frame * 0.3f);
        int lunge = ac > 0 ? (int)(ac * 4.0f) : 0;
        draw_body(ox + lunge, oy, ac * 2.0f);
        draw_head(ox + lunge, oy, 3);
        draw_legs(ox + lunge, oy, (int)(ac * 2), (int)(-ac * 2));
        /* Claws */
        if (ac > 0.5f) {
            px(ox + lunge + 7, oy + 24, C_WHITE);
            px(ox + lunge + 6, oy + 23, C_WHITE);
            px(ox + lunge + 19, oy + 24, C_WHITE);
            px(ox + lunge + 20, oy + 23, C_WHITE);
        }
        break;
    }
    case PET_COMPLETE: {
        float bounce = fabsf(sinf(frame * 0.2f)) * 4.0f;
        int by = (int)bounce;
        draw_body(ox, oy - by, sinf(frame * 0.25f) * 3.0f);
        draw_head(ox, oy - by, 2);
        draw_legs(ox, oy - by, 0, 0);
        /* Hearts */
        if ((frame / 6) % 2 == 0) {
            px(ox + 20, oy + 2 - by, lv_color_hex(0xe94560));
            px(ox + 22, oy + 2 - by, lv_color_hex(0xe94560));
            px(ox + 19, oy + 3 - by, lv_color_hex(0xe94560));
            px(ox + 21, oy + 3 - by, lv_color_hex(0xe94560));
            px(ox + 23, oy + 3 - by, lv_color_hex(0xe94560));
            px(ox + 20, oy + 4 - by, lv_color_hex(0xe94560));
            px(ox + 22, oy + 4 - by, lv_color_hex(0xe94560));
            px(ox + 21, oy + 5 - by, lv_color_hex(0xe94560));
        }
        break;
    }
    }

    lv_obj_invalidate(s_canvas);
}

/* ── Timer callback (~12 FPS) ─────────────────────────────────────────── */
static void cat_anim_timer_cb(lv_timer_t *t)
{
    (void)t;
    s_frame++;
    draw_cat(s_pet_state, s_frame);

    /* Zzz float animation for sleep */
    if (s_pet_state == PET_SLEEP) {
        int zy = -(int)(s_frame % 20);
        lv_obj_set_y(s_zzz_label, 10 + zy);
        lv_opa_t opa = (lv_opa_t)(255 - (s_frame % 20) * 12);
        lv_obj_set_style_opa(s_zzz_label, opa, 0);
    } else {
        lv_obj_set_style_opa(s_zzz_label, 0, 0);
    }
}

/* ── Public: create BLE page ─────────────────────────────────────────── */
void ui_ble_create(lv_obj_t *parent)
{
    s_ble_page = lv_obj_create(parent);
    lv_obj_remove_style_all(s_ble_page);
    lv_obj_set_size(s_ble_page, EXAMPLE_LCD_H_RES, EXAMPLE_LCD_V_RES);
    lv_obj_set_style_bg_color(s_ble_page, lv_color_hex(C_BG_PAGE), 0);
    lv_obj_set_style_bg_opa(s_ble_page, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_ble_page, LV_OBJ_FLAG_SCROLLABLE);

    /* ── Pet area (left) ─────────────────────────────────────────── */
    s_pet_area = lv_obj_create(s_ble_page);
    lv_obj_remove_style_all(s_pet_area);
    lv_obj_set_size(s_pet_area, PET_AREA_W, EXAMPLE_LCD_V_RES);
    lv_obj_align(s_pet_area, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(s_pet_area, lv_color_hex(C_PET_BG), 0);
    lv_obj_set_style_bg_opa(s_pet_area, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_pet_area, lv_color_hex(0xE94560), 0);
    lv_obj_set_style_border_side(s_pet_area, LV_BORDER_SIDE_RIGHT, 0);
    lv_obj_set_style_border_width(s_pet_area, 1, 0);
    lv_obj_clear_flag(s_pet_area, LV_OBJ_FLAG_SCROLLABLE);

    /* BLE connection dot */
    s_conn_dot = lv_obj_create(s_pet_area);
    lv_obj_remove_style_all(s_conn_dot);
    lv_obj_set_size(s_conn_dot, 8, 8);
    lv_obj_align(s_conn_dot, LV_ALIGN_TOP_RIGHT, -4, 4);
    lv_obj_set_style_radius(s_conn_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_conn_dot, lv_color_hex(0x444444), 0);
    lv_obj_set_style_bg_opa(s_conn_dot, LV_OPA_COVER, 0);

    /* Pixel cat canvas */
    s_canvas = lv_canvas_create(s_pet_area);
    s_cbuf = (uint8_t *)lv_malloc(LV_CANVAS_BUF_SIZE(CAT_DISP_W, CAT_DISP_H, 16, LV_DRAW_BUF_STRIDE_ALIGN));
    lv_canvas_set_buffer(s_canvas, s_cbuf, CAT_DISP_W, CAT_DISP_H, LV_COLOR_FORMAT_RGB565);
    lv_canvas_fill_bg(s_canvas, lv_color_hex(C_PET_BG), LV_OPA_COVER);
    lv_obj_align(s_canvas, LV_ALIGN_CENTER, 0, -10);

    /* Zzz label (for sleep state) */
    s_zzz_label = lv_label_create(s_pet_area);
    lv_label_set_text(s_zzz_label, "Z z z");
    lv_obj_set_style_text_font(s_zzz_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_zzz_label, lv_color_hex(0x90B8E0), 0);
    lv_obj_align(s_zzz_label, LV_ALIGN_TOP_RIGHT, -6, 10);
    lv_obj_set_style_opa(s_zzz_label, 0, 0);

    /* State label */
    s_state_label = lv_label_create(s_pet_area);
    lv_label_set_text(s_state_label, "sleep");
    lv_obj_set_style_text_font(s_state_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_state_label, lv_color_hex(0x506070), 0);
    lv_obj_align(s_state_label, LV_ALIGN_BOTTOM_MID, 0, -4);

    /* ── Messages area (right) ───────────────────────────────────── */
    lv_obj_t *msg_cont = lv_obj_create(s_ble_page);
    lv_obj_remove_style_all(msg_cont);
    lv_obj_set_size(msg_cont, MSG_AREA_W, EXAMPLE_LCD_V_RES);
    lv_obj_align(msg_cont, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_opa(msg_cont, 0, 0);
    lv_obj_set_flex_flow(msg_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(msg_cont, 6, 0);
    lv_obj_set_style_pad_row(msg_cont, 3, 0);
    lv_obj_clear_flag(msg_cont, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < MAX_MESSAGES; i++) {
        s_msg_rows[i] = lv_obj_create(msg_cont);
        lv_obj_remove_style_all(s_msg_rows[i]);
        lv_obj_set_size(s_msg_rows[i], LV_PCT(100), 28);
        lv_obj_set_style_radius(s_msg_rows[i], 4, 0);
        lv_obj_set_style_bg_color(s_msg_rows[i], lv_color_hex(0x16213E), 0);
        lv_obj_set_style_bg_opa(s_msg_rows[i], 0, 0);
        lv_obj_set_style_pad_hor(s_msg_rows[i], 6, 0);
        lv_obj_clear_flag(s_msg_rows[i], LV_OBJ_FLAG_SCROLLABLE);

        s_msg_labels[i] = lv_label_create(s_msg_rows[i]);
        lv_label_set_text(s_msg_labels[i], "");
        lv_label_set_long_mode(s_msg_labels[i], LV_LABEL_LONG_DOT);
        lv_obj_set_width(s_msg_labels[i], LV_PCT(100));
        lv_obj_align(s_msg_labels[i], LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_set_style_text_font(s_msg_labels[i], &lv_font_source_han_sans_sc_14_cjk, 0);
        lv_obj_set_style_text_color(s_msg_labels[i], lv_color_hex(0x506070), 0);
    }

    /* Start animation timer (~12 FPS) */
    s_anim_timer = lv_timer_create(cat_anim_timer_cb, 83, NULL);
    draw_cat(PET_SLEEP, 0);
}

/* ── Public: add a message ───────────────────────────────────────────── */
void ui_ble_add_message(const char *msg)
{
    for (int i = MAX_MESSAGES - 1; i > 0; i--) {
        strncpy(s_msg_texts[i], s_msg_texts[i - 1], sizeof(s_msg_texts[i]) - 1);
        s_msg_texts[i][sizeof(s_msg_texts[i]) - 1] = '\0';
    }
    strncpy(s_msg_texts[0], msg, sizeof(s_msg_texts[0]) - 1);
    s_msg_texts[0][sizeof(s_msg_texts[0]) - 1] = '\0';
    if (s_msg_count < MAX_MESSAGES) s_msg_count++;

    static const lv_color_t COL[MAX_MESSAGES] = {
        {.red = 0xE9, .green = 0xED, .blue = 0xF0},
        {.red = 0xA0, .green = 0xB4, .blue = 0xC8},
        {.red = 0x70, .green = 0x88, .blue = 0xA0},
        {.red = 0x50, .green = 0x60, .blue = 0x78},
        {.red = 0x38, .green = 0x48, .blue = 0x58},
    };
    static const uint8_t ALPHA[MAX_MESSAGES] = {220, 160, 110, 70, 40};

    for (int i = 0; i < MAX_MESSAGES; i++) {
        if (i < s_msg_count) {
            lv_label_set_text(s_msg_labels[i], s_msg_texts[i]);
            lv_obj_set_style_bg_opa(s_msg_rows[i], ALPHA[i], 0);
            lv_obj_set_style_text_color(s_msg_labels[i], COL[i], 0);
        } else {
            lv_label_set_text(s_msg_labels[i], "");
            lv_obj_set_style_bg_opa(s_msg_rows[i], 0, 0);
        }
    }
}

/* ── Public: change pet state ────────────────────────────────────────── */
void ui_ble_set_pet_state(const char *state)
{
    pet_state_t new_state = PET_IDLE;
    if      (strcmp(state, "sleep")     == 0) new_state = PET_SLEEP;
    else if (strcmp(state, "working")   == 0) new_state = PET_WORKING;
    else if (strcmp(state, "attention") == 0) new_state = PET_ATTENTION;
    else if (strcmp(state, "complete")  == 0) new_state = PET_COMPLETE;

    s_pet_state = new_state;
    s_frame = 0;

    /* Clear messages on sleep */
    if (new_state == PET_SLEEP) {
        s_msg_count = 0;
        for (int i = 0; i < MAX_MESSAGES; i++) {
            s_msg_texts[i][0] = '\0';
            lv_label_set_text(s_msg_labels[i], "");
            lv_obj_set_style_bg_opa(s_msg_rows[i], 0, 0);
        }
    }

    /* Reset flash bg */
    lv_label_set_text(s_state_label, state);
}

/* ── Public: BLE connection indicator ───────────────────────────────── */
void ui_ble_set_connected(bool connected)
{
    lv_obj_set_style_bg_color(s_conn_dot,
        connected ? lv_color_hex(0x00CC66) : lv_color_hex(0x444444), 0);
}
