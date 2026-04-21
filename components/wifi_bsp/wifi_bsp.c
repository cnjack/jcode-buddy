#include <string.h>
#include "wifi_bsp.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_sntp.h"
#include "lwip/ip_addr.h"

static const char *TAG = "wifi_bsp";
static bool s_connected = false;
static bool s_wifi_started = false;
static bool s_ap_active = false;
static bool s_reconnect_enabled = true;
static wifi_connected_cb_t s_on_connected = NULL;
static volatile bool s_sntp_synced = false;
static esp_netif_t *s_ap_netif = NULL;

#define NVS_WIFI_NAMESPACE "wifi_cfg"
#define NVS_KEY_SSID       "ssid"
#define NVS_KEY_PASS       "pass"

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        ESP_LOGI(TAG, "WiFi connecting...");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        if (s_reconnect_enabled) {
            ESP_LOGW(TAG, "WiFi disconnected, reconnecting...");
            vTaskDelay(pdMS_TO_TICKS(2000));
            esp_wifi_connect();
        } else {
            ESP_LOGW(TAG, "WiFi disconnected, auto-reconnect disabled");
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_connected = true;
        if (s_on_connected) {
            s_on_connected();
        }
    }
}

void wifi_init(void)
{
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_instance_t inst_wifi, inst_ip;
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &inst_wifi);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &inst_ip);
}

void wifi_ap_start(const char *ap_ssid)
{
    if (!s_ap_netif) {
        s_ap_netif = esp_netif_create_default_wifi_ap();
    }
    esp_wifi_set_mode(WIFI_MODE_APSTA);

    wifi_config_t ap_config = {
        .ap = {
            .max_connection = 4,
            .authmode = WIFI_AUTH_OPEN,
            .channel = 1,
        },
    };
    strncpy((char *)ap_config.ap.ssid, ap_ssid, sizeof(ap_config.ap.ssid) - 1);
    ap_config.ap.ssid_len = strlen(ap_ssid);

    esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    if (!s_wifi_started) {
        esp_wifi_start();
        s_wifi_started = true;
    }
    s_ap_active = true;
    ESP_LOGI(TAG, "AP started: %s", ap_ssid);
}

void wifi_ap_stop(void)
{
    if (s_ap_active) {
        esp_wifi_set_mode(WIFI_MODE_STA);
        s_ap_active = false;
        ESP_LOGI(TAG, "AP stopped, switched to STA-only");
    }
}

esp_err_t wifi_scan(wifi_ap_record_t *ap_list, uint16_t *count)
{
    wifi_scan_config_t scan_cfg = {
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
    };

    /* In APSTA mode, scan briefly on the home channel first, then do a full scan
       in small batches to keep the AP responsive for connected clients. */
    if (s_ap_active) {
        /* Get current AP channel */
        uint8_t primary = 0;
        wifi_second_chan_t second = 0;
        esp_wifi_get_channel(&primary, &second);
        if (primary > 0) {
            /* Quick scan on AP's own channel first (no off-channel hop) */
            scan_cfg.channel = primary;
            esp_wifi_scan_start(&scan_cfg, true);
            uint16_t home_count = *count;
            esp_wifi_scan_get_ap_records(&home_count, ap_list);

            /* Now do full scan with short dwell to minimize off-channel time */
            scan_cfg.channel = 0;
            esp_err_t err = esp_wifi_scan_start(&scan_cfg, true);
            if (err != ESP_OK) {
                /* Return home-channel results if full scan failed */
                *count = home_count;
                return (home_count > 0) ? ESP_OK : err;
            }
            return esp_wifi_scan_get_ap_records(count, ap_list);
        }
    }

    esp_err_t err = esp_wifi_scan_start(&scan_cfg, true);
    if (err != ESP_OK) return err;

    return esp_wifi_scan_get_ap_records(count, ap_list);
}

void wifi_sta_stop(void)
{
    s_reconnect_enabled = false;
    if (s_wifi_started) {
        esp_wifi_disconnect();
    }
    s_connected = false;
    ESP_LOGI(TAG, "STA stopped, auto-reconnect disabled");
}

void wifi_sta_start(const char *ssid, const char *password)
{
    /* Save to NVS */
    nvs_handle_t h;
    if (nvs_open(NVS_WIFI_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_str(h, NVS_KEY_SSID, ssid);
        nvs_set_str(h, NVS_KEY_PASS, password);
        nvs_commit(h);
        nvs_close(h);
    }

    wifi_config_t wifi_config = {};
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);

    s_reconnect_enabled = true;
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (s_wifi_started) {
        esp_wifi_disconnect();
        esp_wifi_connect();
    } else {
        esp_wifi_start();
        s_wifi_started = true;
    }
    ESP_LOGI(TAG, "WiFi starting, SSID: %s", ssid);
}

bool wifi_load_credentials(char *ssid, size_t ssid_len, char *pass, size_t pass_len)
{
    nvs_handle_t h;
    if (nvs_open(NVS_WIFI_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    esp_err_t r1 = nvs_get_str(h, NVS_KEY_SSID, ssid, &ssid_len);
    esp_err_t r2 = nvs_get_str(h, NVS_KEY_PASS, pass, &pass_len);
    nvs_close(h);
    return (r1 == ESP_OK && r2 == ESP_OK && ssid[0] != '\0');
}

void wifi_set_connected_callback(wifi_connected_cb_t cb)
{
    s_on_connected = cb;
}

bool wifi_is_connected(void)
{
    return s_connected;
}

static void sntp_sync_cb(struct timeval *tv)
{
    ESP_LOGI(TAG, "SNTP time synced: %lld", tv->tv_sec);
    s_sntp_synced = true;
}

void sntp_start(void)
{
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.nist.gov");
    esp_sntp_set_time_sync_notification_cb(sntp_sync_cb);
    esp_sntp_init();
    ESP_LOGI(TAG, "SNTP started");
}

void sntp_wait_sync(void)
{
    int retry = 0;
    while (!s_sntp_synced && retry < 30) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        retry++;
    }
    if (s_sntp_synced) {
        ESP_LOGI(TAG, "SNTP sync completed");
    } else {
        ESP_LOGW(TAG, "SNTP sync timeout");
    }
}
