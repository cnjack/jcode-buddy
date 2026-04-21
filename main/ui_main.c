#include "ui_main.h"
#include "ui_styles.h"
#include "ui_clock.h"
#include "ui_ble.h"
#include "lvgl.h"
#include "esp_log.h"

static const char *TAG = "ui_main";

static lv_obj_t *main_screen = NULL;
static lv_obj_t *s_page_clock = NULL;
static lv_obj_t *s_page_ble   = NULL;
static int s_cur_page = 0;  /* 0 = clock, 1 = ble */
#define NUM_PAGES 2

void ui_main_create(void)
{
    ui_styles_init();

    main_screen = lv_screen_active();
    lv_obj_set_style_bg_color(main_screen, lv_color_hex(0x1A1A2E), 0);
    lv_obj_set_style_bg_opa(main_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(main_screen, LV_OBJ_FLAG_SCROLLABLE);

    // Page 1: Clock + State
    s_page_clock = lv_obj_create(main_screen);
    lv_obj_remove_style_all(s_page_clock);
    lv_obj_set_size(s_page_clock, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(s_page_clock, lv_color_hex(0x1A1A2E), 0);
    lv_obj_set_style_bg_opa(s_page_clock, LV_OPA_COVER, 0);
    lv_obj_set_flex_flow(s_page_clock, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(s_page_clock, 0, 0);
    lv_obj_set_style_pad_row(s_page_clock, 4, 0);
    lv_obj_clear_flag(s_page_clock, LV_OBJ_FLAG_SCROLLABLE);

    ui_clock_create(s_page_clock);

    // Page 2: BLE messages + pet (hidden by default)
    ui_ble_create(main_screen);
    s_page_ble = lv_obj_get_child(main_screen, -1);  /* last child = ble page */
    lv_obj_add_flag(s_page_ble, LV_OBJ_FLAG_HIDDEN);

    ESP_LOGI(TAG, "UI created: 2 pages (clock/ble), BOOT to cycle");
}

void ui_main_toggle_page(void)
{
    /* Hide current page */
    lv_obj_t *pages[] = { s_page_clock, s_page_ble };
    lv_obj_add_flag(pages[s_cur_page], LV_OBJ_FLAG_HIDDEN);

    /* Advance to next page */
    s_cur_page = (s_cur_page + 1) % NUM_PAGES;
    lv_obj_clear_flag(pages[s_cur_page], LV_OBJ_FLAG_HIDDEN);
    ESP_LOGI(TAG, "Toggled to page %d", s_cur_page);
}
