#pragma once
#include <stdbool.h>

typedef void (*wifi_cred_cb_t)(const char *ssid, const char *pass);

void app_ble_init(void);
bool app_ble_is_connected(void);
void app_ble_set_wifi_cb(wifi_cred_cb_t cb);

/**
 * @brief Send a string to the connected BLE client via NUS TX (notify).
 *        A newline is appended automatically (NDJSON framing).
 * @return true if the notification was sent
 */
bool app_ble_send(const char *data);
