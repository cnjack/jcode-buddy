#ifndef UI_STYLES_H
#define UI_STYLES_H

#include "lvgl.h"

#define COLOR_BG          lv_color_hex(0x1A1A2E)
#define COLOR_PANEL       lv_color_hex(0x16213E)
#define COLOR_ACCENT      lv_color_hex(0xE94560)
#define COLOR_TEXT         lv_color_hex(0xFFFFFF)
#define COLOR_TEXT_DIM     lv_color_hex(0x8892B0)
#define COLOR_PROGRESS_BG lv_color_hex(0x0F3460)
#define COLOR_GLASS       lv_color_hex(0x16213E)

void ui_styles_init(void);
lv_style_t *ui_get_panel_style(void);
lv_style_t *ui_get_glass_style(void);

void ui_set_frosted_panel(lv_obj_t *obj);
void ui_set_gradient_bar(lv_obj_t *bar);

#endif
