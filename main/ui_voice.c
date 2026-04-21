#include "ui_voice.h"
#include "ui_styles.h"
#include "asr_client.h"
#include "lvgl_lock.h"
#include "user_config.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "ui_voice";

/* ── Layout constants (640×172 landscape) ─────────────────────────── */
#define BTN_SIZE      80
#define TEXT_AREA_W   (EXAMPLE_LCD_H_RES - BTN_SIZE - 30)
#define TEXT_AREA_H   (EXAMPLE_LCD_V_RES - 20)

/* ── UI elements ─────────────────────────────────────────────────── */
static lv_obj_t *s_page       = NULL;
static lv_obj_t *s_btn        = NULL;
static lv_obj_t *s_btn_label  = NULL;
static lv_obj_t *s_status_lbl = NULL;
static lv_obj_t *s_text_area  = NULL;

/* Accumulated text from completed transcriptions */
static char s_full_text[1024] = {0};

/* ── ASR Callbacks (called from ASR task, NOT LVGL thread) ──────── */

static void on_asr_text(const char *text, const char *stash)
{
    if (!lvgl_lock(50)) return;

    /* Show real-time preview: confirmed text + stash */
    char preview[1024];
    int len = snprintf(preview, sizeof(preview), "%s%s%s",
                       s_full_text, text ? text : "", stash ? stash : "");
    if (len > 0) {
        lv_label_set_text(s_text_area, preview);
    }
    lv_label_set_text(s_status_lbl, "Listening...");

    lvgl_unlock();
}

static void on_asr_done(const char *transcript)
{
    if (!lvgl_lock(50)) return;

    /* Append final result to accumulated text */
    if (transcript && transcript[0]) {
        size_t cur_len = strlen(s_full_text);
        size_t tr_len = strlen(transcript);
        if (cur_len + tr_len + 1 < sizeof(s_full_text)) {
            strcat(s_full_text, transcript);
        }
    }
    lv_label_set_text(s_text_area, s_full_text);
    lv_label_set_text(s_status_lbl, "Done");
    ESP_LOGI(TAG, "ASR result: %s", transcript);

    lvgl_unlock();
}

static void on_asr_error(const char *message)
{
    if (!lvgl_lock(50)) return;

    lv_label_set_text(s_status_lbl, "Error");
    ESP_LOGE(TAG, "ASR error: %s", message);

    /* Reset button appearance */
    lv_obj_set_style_bg_color(s_btn, COLOR_ACCENT, 0);
    lv_label_set_text(s_btn_label, LV_SYMBOL_AUDIO);

    lvgl_unlock();
}

/* ── Toggle recording (called from ui_main floating mic button) ── */

void ui_voice_toggle_recording(void)
{
    ESP_LOGI(TAG, "toggle_recording called, asr_active=%d", asr_is_active());
    if (asr_is_active()) {
        /* Stop recording */
        ESP_LOGI(TAG, "Stopping ASR...");
        asr_stop();
        lv_obj_set_style_bg_color(s_btn, COLOR_ACCENT, 0);
        lv_label_set_text(s_btn_label, LV_SYMBOL_AUDIO);
        lv_label_set_text(s_status_lbl, "Stopped");
    } else {
        /* Clear previous text and start new session */
        memset(s_full_text, 0, sizeof(s_full_text));
        lv_label_set_text(s_text_area, "");

        asr_callbacks_t cbs = {
            .on_text  = on_asr_text,
            .on_done  = on_asr_done,
            .on_error = on_asr_error,
        };
        ESP_LOGI(TAG, "Starting ASR...");
        if (asr_start(&cbs)) {
            ESP_LOGI(TAG, "asr_start() returned true");
            lv_obj_set_style_bg_color(s_btn, lv_color_hex(0xFF3333), 0);
            lv_label_set_text(s_btn_label, LV_SYMBOL_STOP);
            lv_label_set_text(s_status_lbl, "Connecting...");
        } else {
            ESP_LOGE(TAG, "asr_start() returned false!");
            lv_label_set_text(s_status_lbl, "Start failed");
        }
    }
}

/* ── Button click handler (on-page button) ───── */

static void btn_click_cb(lv_event_t *e)
{
    ESP_LOGI(TAG, ">>> Voice btn_click_cb triggered");
    ui_voice_toggle_recording();
}

/* ── Create UI ───────────────────────────────────────────────────── */

void ui_voice_create(lv_obj_t *parent)
{
    /* Page container */
    s_page = lv_obj_create(parent);
    lv_obj_remove_style_all(s_page);
    lv_obj_set_size(s_page, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(s_page, COLOR_BG, 0);
    lv_obj_set_style_bg_opa(s_page, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_page, LV_OBJ_FLAG_SCROLLABLE);

    /* ── Left: Record button area ── */
    lv_obj_t *btn_area = lv_obj_create(s_page);
    lv_obj_remove_style_all(btn_area);
    lv_obj_set_size(btn_area, BTN_SIZE + 20, LV_VER_RES);
    lv_obj_align(btn_area, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_flex_flow(btn_area, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(btn_area, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(btn_area, 5, 0);
    lv_obj_set_style_pad_row(btn_area, 4, 0);
    lv_obj_clear_flag(btn_area, LV_OBJ_FLAG_SCROLLABLE);

    /* Round mic button */
    s_btn = lv_button_create(btn_area);
    lv_obj_set_size(s_btn, BTN_SIZE, BTN_SIZE);
    lv_obj_set_style_radius(s_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_btn, COLOR_ACCENT, 0);
    lv_obj_set_style_bg_opa(s_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(s_btn, 15, 0);
    lv_obj_set_style_shadow_color(s_btn, COLOR_ACCENT, 0);
    lv_obj_set_style_shadow_opa(s_btn, 120, 0);
    lv_obj_add_event_cb(s_btn, btn_click_cb, LV_EVENT_CLICKED, NULL);

    s_btn_label = lv_label_create(s_btn);
    lv_label_set_text(s_btn_label, LV_SYMBOL_AUDIO);
    lv_obj_set_style_text_color(s_btn_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(s_btn_label, &lv_font_montserrat_20, 0);
    lv_obj_center(s_btn_label);

    /* Status label under button */
    s_status_lbl = lv_label_create(btn_area);
    lv_label_set_text(s_status_lbl, "Tap to talk");
    lv_obj_set_style_text_color(s_status_lbl, COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(s_status_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_align(s_status_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_status_lbl, BTN_SIZE + 10);

    /* ── Right: Text display area ── */
    lv_obj_t *text_panel = lv_obj_create(s_page);
    lv_obj_remove_style_all(text_panel);
    ui_set_frosted_panel(text_panel);
    lv_obj_set_size(text_panel, TEXT_AREA_W, TEXT_AREA_H);
    lv_obj_align(text_panel, LV_ALIGN_RIGHT_MID, -5, 0);
    lv_obj_set_style_pad_all(text_panel, 8, 0);
    lv_obj_add_flag(text_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(text_panel, LV_DIR_VER);

    s_text_area = lv_label_create(text_panel);
    lv_label_set_long_mode(s_text_area, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_text_area, TEXT_AREA_W - 20);
    lv_label_set_text(s_text_area, "");
    lv_obj_set_style_text_color(s_text_area, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(s_text_area, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_line_space(s_text_area, 4, 0);
}

lv_obj_t *ui_voice_get_page(void)
{
    return s_page;
}
