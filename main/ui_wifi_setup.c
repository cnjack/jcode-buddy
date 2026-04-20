#include "ui_wifi_setup.h"
#include "ui_styles.h"

static lv_obj_t *s_status_label = NULL;

void ui_wifi_setup_create(lv_obj_t *parent)
{
    lv_obj_t *page = lv_obj_create(parent);
    lv_obj_remove_style_all(page);
    lv_obj_set_size(page, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(page, COLOR_BG, 0);
    lv_obj_set_style_bg_opa(page, LV_OPA_COVER, 0);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(page, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(page, 6, 0);

    /* Title */
    lv_obj_t *title = lv_label_create(page);
    lv_label_set_text(title, LV_SYMBOL_WIFI "  WiFi Setup");
    lv_obj_set_style_text_color(title, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);

    /* Instruction */
    lv_obj_t *instr = lv_label_create(page);
    lv_label_set_text(instr,
        "1. Connect to WiFi: LiuGuang-Setup\n"
        "2. Open browser, WiFi config page will appear\n"
        "3. Select your WiFi and enter password");
    lv_obj_set_style_text_color(instr, COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(instr, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(instr, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(instr, LV_HOR_RES - 20);

    /* Status label */
    s_status_label = lv_label_create(page);
    lv_label_set_text(s_status_label, "Waiting for connection...");
    lv_obj_set_style_text_color(s_status_label, COLOR_ACCENT, 0);
    lv_obj_set_style_text_font(s_status_label, &lv_font_montserrat_16, 0);
}

void ui_wifi_setup_set_status(const char *status)
{
    if (s_status_label) {
        lv_label_set_text(s_status_label, status);
    }
}
