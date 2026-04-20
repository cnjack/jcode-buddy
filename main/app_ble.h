#pragma once
#include <stdbool.h>

typedef void (*wifi_cred_cb_t)(const char *ssid, const char *pass);

void app_ble_init(void);
bool app_ble_is_connected(void);
void app_ble_set_wifi_cb(wifi_cred_cb_t cb);
