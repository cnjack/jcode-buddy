#include "ui_styles.h"

static lv_style_t style_panel;
static lv_style_t style_glass;

void ui_styles_init(void)
{
    lv_style_init(&style_panel);
    lv_style_set_bg_color(&style_panel, COLOR_PANEL);
    lv_style_set_bg_opa(&style_panel, LV_OPA_COVER);
    lv_style_set_radius(&style_panel, 10);
    lv_style_set_pad_all(&style_panel, 6);
    lv_style_set_border_width(&style_panel, 0);

    lv_style_init(&style_glass);
    lv_style_set_bg_color(&style_glass, COLOR_GLASS);
    lv_style_set_bg_opa(&style_glass, 160);
    lv_style_set_radius(&style_glass, 12);
    lv_style_set_pad_all(&style_glass, 4);
    lv_style_set_border_width(&style_glass, 0);
    lv_style_set_border_color(&style_glass, lv_color_hex(0xFFFFFF));
    lv_style_set_border_opa(&style_glass, 30);
}

lv_style_t *ui_get_panel_style(void) { return &style_panel; }
lv_style_t *ui_get_glass_style(void) { return &style_glass; }

void ui_set_frosted_panel(lv_obj_t *obj)
{
    lv_obj_set_style_bg_color(obj, COLOR_GLASS, 0);
    lv_obj_set_style_bg_opa(obj, 160, 0);
    lv_obj_set_style_radius(obj, 12, 0);
    lv_obj_set_style_pad_all(obj, 4, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
}

void ui_set_gradient_bar(lv_obj_t *bar)
{
    lv_obj_set_style_bg_color(bar, COLOR_PROGRESS_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 4, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, COLOR_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 4, LV_PART_INDICATOR);
}
