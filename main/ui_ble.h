#pragma once
#include "lvgl.h"
#include <stdbool.h>

void ui_ble_create(lv_obj_t *parent);
void ui_ble_add_message(const char *msg);
void ui_ble_set_pet_state(const char *state);
void ui_ble_set_connected(bool connected);
