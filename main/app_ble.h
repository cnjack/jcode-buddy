#pragma once
#include <stdbool.h>

typedef void (*wifi_cred_cb_t)(const char *ssid, const char *pass);

void app_ble_init(void);
bool app_ble_is_connected(void);
void app_ble_set_wifi_cb(wifi_cred_cb_t cb);

/**
 * @brief Pause BLE advertising to free the radio for WiFi.
 *        No-op if a BLE central is currently connected.
 */
void app_ble_pause_adv(void);

/**
 * @brief Resume BLE advertising after WiFi connection attempt.
 */
void app_ble_resume_adv(void);

/**
 * @brief Enable and start BLE advertising.
 *        Call after WiFi STA is connected and mode switched to STA-only.
 */
void app_ble_start_adv(void);

/**
 * @brief Send a string to the connected BLE client via NUS TX (notify).
 *        A newline is appended automatically (NDJSON framing).
 * @return true if the notification was sent
 */
bool app_ble_send(const char *data);
