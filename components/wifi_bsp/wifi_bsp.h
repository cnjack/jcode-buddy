#ifndef WIFI_BSP_H
#define WIFI_BSP_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_wifi_types.h"

typedef void (*wifi_connected_cb_t)(void);

/**
 * @brief Initialize WiFi subsystem and start in APSTA mode.
 *        AP is available for provisioning immediately.
 */
void wifi_init(void);

/**
 * @brief Configure the AP with the given SSID for provisioning.
 *        Ensures APSTA mode is active.
 */
void wifi_ap_start(const char *ap_ssid);

/**
 * @brief Stop AP mode and switch to STA-only.
 */
void wifi_ap_stop(void);

/**
 * @brief Switch from APSTA to STA-only mode.
 *        Call after STA is connected and provisioning is not needed.
 */
void wifi_switch_to_sta(void);

/**
 * @brief Scan for nearby WiFi networks.
 * @param[out] ap_list  Caller-provided array of wifi_ap_record_t.
 * @param[in,out] count On input: max entries. On output: actual count.
 * @return ESP_OK on success.
 */
esp_err_t wifi_scan(wifi_ap_record_t *ap_list, uint16_t *count);

/**
 * @brief Start WiFi STA with the given SSID/password.
 *        Saves credentials to NVS for next boot.
 */
void wifi_sta_start(const char *ssid, const char *password);

/**
 * @brief Stop STA reconnection attempts.
 *        Call before starting provisioning to prevent reconnect storms.
 */
void wifi_sta_stop(void);

/**
 * @brief Load saved WiFi credentials from NVS.
 * @return true if credentials were found and loaded into ssid/pass buffers.
 */
bool wifi_load_credentials(char *ssid, size_t ssid_len, char *pass, size_t pass_len);

bool wifi_is_connected(void);
void wifi_set_connected_callback(wifi_connected_cb_t cb);

void sntp_start(void);
void sntp_wait_sync(void);

#endif
