#include "ui_clock.h"
#include "ui_styles.h"
#include "i2c_equipment.h"
#include <stdio.h>
#include <string.h>

static lv_obj_t *lbl_time = NULL;
static lv_obj_t *lbl_seconds = NULL;
static lv_obj_t *lbl_date = NULL;
static lv_obj_t *arc_seconds = NULL;

static const char *weekdays[] = {"日", "一", "二", "三", "四", "五", "六"};

void ui_clock_create(lv_obj_t *parent)
{

    lv_obj_t *clock_cont = lv_obj_create(parent);
    lv_obj_remove_style_all(clock_cont);
    lv_obj_set_size(clock_cont, LV_PCT(100), 130);
    lv_obj_set_style_bg_color(clock_cont, COLOR_PANEL, 0);
    lv_obj_set_style_bg_opa(clock_cont, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(clock_cont, 12, 0);
    lv_obj_set_style_pad_all(clock_cont, 4, 0);
    lv_obj_clear_flag(clock_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(clock_cont, 0, 0);

    lbl_date = lv_label_create(clock_cont);
    lv_label_set_text(lbl_date, "");
    lv_obj_set_style_text_color(lbl_date, COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(lbl_date, &lv_font_source_han_sans_sc_14_cjk, 0);
    lv_obj_align(lbl_date, LV_ALIGN_TOP_MID, 0, 4);

    lbl_time = lv_label_create(clock_cont);
    lv_label_set_text(lbl_time, "00:00");
    lv_obj_set_style_text_color(lbl_time, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(lbl_time, &lv_font_montserrat_48, 0);
    lv_obj_align(lbl_time, LV_ALIGN_CENTER, 0, -6);

    lbl_seconds = lv_label_create(clock_cont);
    lv_label_set_text(lbl_seconds, "00");
    lv_obj_set_style_text_color(lbl_seconds, COLOR_ACCENT, 0);
    lv_obj_set_style_text_font(lbl_seconds, &lv_font_montserrat_16, 0);
    lv_obj_align(lbl_seconds, LV_ALIGN_CENTER, 0, 30);

    arc_seconds = lv_arc_create(clock_cont);
    lv_arc_set_range(arc_seconds, 0, 59);
    lv_arc_set_rotation(arc_seconds, 270);
    lv_arc_set_bg_angles(arc_seconds, 0, 360);
    lv_obj_set_size(arc_seconds, 110, 110);
    lv_obj_align(arc_seconds, LV_ALIGN_CENTER, 0, -6);
    lv_obj_set_style_arc_color(arc_seconds, COLOR_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc_seconds, 2, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc_seconds, lv_color_hex(0x0F3460), LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc_seconds, 1, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(arc_seconds, 0, LV_PART_KNOB);
    lv_obj_set_style_pad_all(arc_seconds, 0, LV_PART_KNOB);
    lv_obj_clear_flag(arc_seconds, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_move_background(arc_seconds);
}

void ui_clock_update(void)
{
    static rtc_datetime_t rtc_time;
    rtc_time = i2c_rtc_get();

    char buf[32];
    snprintf(buf, sizeof(buf), "%02d:%02d", rtc_time.hour, rtc_time.minute);
    lv_label_set_text(lbl_time, buf);

    snprintf(buf, sizeof(buf), "%02d", rtc_time.second);
    lv_label_set_text(lbl_seconds, buf);

    const char *wday = (rtc_time.week < 7) ? weekdays[rtc_time.week] : "?";
    snprintf(buf, sizeof(buf), "星期%s  %d月%d日", wday, rtc_time.month, rtc_time.day);
    lv_label_set_text(lbl_date, buf);

    lv_arc_set_value(arc_seconds, rtc_time.second);
}
