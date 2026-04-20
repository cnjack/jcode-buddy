#ifndef UI_WIFI_SETUP_H
#define UI_WIFI_SETUP_H

#include "lvgl.h"

/**
 * @brief Create WiFi setup page on the given parent.
 */
void ui_wifi_setup_create(lv_obj_t *parent);

/**
 * @brief Update status text on the WiFi setup page.
 */
void ui_wifi_setup_set_status(const char *status);

#endif
