#include "wifi_prov.h"
#include "wifi_bsp.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "cJSON.h"
#include "lwip/sockets.h"
#include "lwip/ip4_addr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "wifi_prov";

#define AP_SSID "LiuGuang-Setup"

/* Embedded HTML page */
extern const uint8_t wifi_setup_html_start[] asm("_binary_wifi_setup_html_start");
extern const uint8_t wifi_setup_html_end[]   asm("_binary_wifi_setup_html_end");

static httpd_handle_t s_server = NULL;
static volatile bool s_prov_done = false;
static int s_dns_sock = -1;
static TaskHandle_t s_dns_task = NULL;

/* ── DNS captive portal ──────────────────────────────────────────────── */

static void dns_task(void *arg)
{
    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(53),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };

    s_dns_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s_dns_sock < 0) {
        ESP_LOGE(TAG, "DNS socket failed");
        vTaskDelete(NULL);
        return;
    }

    if (bind(s_dns_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        ESP_LOGE(TAG, "DNS bind failed");
        close(s_dns_sock);
        s_dns_sock = -1;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "DNS captive portal started on :53");

    uint8_t buf[512];
    struct sockaddr_in client_addr;
    socklen_t client_len;

    /* AP IP = 192.168.4.1 */
    uint8_t ap_ip[4] = {192, 168, 4, 1};

    while (s_dns_sock >= 0) {
        client_len = sizeof(client_addr);
        int len = recvfrom(s_dns_sock, buf, sizeof(buf), 0,
                           (struct sockaddr *)&client_addr, &client_len);
        if (len < 12) continue;

        /* Build minimal DNS response: copy query, set response flags, add A record pointing to AP IP */
        uint8_t resp[512];
        if (len > (int)(sizeof(resp) - 16)) continue;

        memcpy(resp, buf, len);

        /* Set QR=1, AA=1, RD=1, RA=1 */
        resp[2] = 0x85;
        resp[3] = 0x80;
        /* ANCOUNT = 1 */
        resp[6] = 0x00;
        resp[7] = 0x01;

        int pos = len;
        /* Pointer to query name */
        resp[pos++] = 0xC0;
        resp[pos++] = 0x0C;
        /* Type A */
        resp[pos++] = 0x00; resp[pos++] = 0x01;
        /* Class IN */
        resp[pos++] = 0x00; resp[pos++] = 0x01;
        /* TTL = 60 */
        resp[pos++] = 0x00; resp[pos++] = 0x00; resp[pos++] = 0x00; resp[pos++] = 0x3C;
        /* RDLENGTH = 4 */
        resp[pos++] = 0x00; resp[pos++] = 0x04;
        /* IP */
        memcpy(&resp[pos], ap_ip, 4);
        pos += 4;

        sendto(s_dns_sock, resp, pos, 0,
               (struct sockaddr *)&client_addr, client_len);
    }

    vTaskDelete(NULL);
}

static void dns_start(void)
{
    xTaskCreatePinnedToCore(dns_task, "dns", 4096, NULL, 3, &s_dns_task, 1);
}

static void dns_stop(void)
{
    if (s_dns_sock >= 0) {
        int sock = s_dns_sock;
        s_dns_sock = -1;
        close(sock);
    }
    if (s_dns_task) {
        s_dns_task = NULL;
    }
}

/* ── HTTP handlers ───────────────────────────────────────────────────── */

static esp_err_t root_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, (const char *)wifi_setup_html_start,
                    wifi_setup_html_end - wifi_setup_html_start);
    return ESP_OK;
}

static esp_err_t scan_handler(httpd_req_t *req)
{
    wifi_ap_record_t ap_list[20];
    uint16_t count = 20;
    esp_err_t err = wifi_scan(ap_list, &count);

    cJSON *arr = cJSON_CreateArray();
    if (err == ESP_OK) {
        /* Deduplicate by SSID, keep strongest signal */
        for (int i = 0; i < count; i++) {
            if (ap_list[i].ssid[0] == '\0') continue;

            bool dup = false;
            for (int j = 0; j < i; j++) {
                if (strcmp((char *)ap_list[i].ssid, (char *)ap_list[j].ssid) == 0) {
                    dup = true;
                    break;
                }
            }
            if (dup) continue;

            cJSON *obj = cJSON_CreateObject();
            cJSON_AddStringToObject(obj, "ssid", (char *)ap_list[i].ssid);
            cJSON_AddNumberToObject(obj, "rssi", ap_list[i].rssi);
            cJSON_AddNumberToObject(obj, "auth", ap_list[i].authmode);
            cJSON_AddItemToArray(arr, obj);
        }
    }

    char *json = cJSON_PrintUnformatted(arr);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);
    free(json);
    cJSON_Delete(arr);
    return ESP_OK;
}

static esp_err_t connect_handler(httpd_req_t *req)
{
    char body[256] = {0};
    int len = httpd_req_recv(req, body, sizeof(body) - 1);
    if (len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No body");
        return ESP_FAIL;
    }
    body[len] = '\0';

    cJSON *root = cJSON_Parse(body);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *j_ssid = cJSON_GetObjectItemCaseSensitive(root, "ssid");
    cJSON *j_pass = cJSON_GetObjectItemCaseSensitive(root, "pass");

    if (!cJSON_IsString(j_ssid)) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing ssid");
        return ESP_FAIL;
    }

    const char *ssid = j_ssid->valuestring;
    const char *pass = cJSON_IsString(j_pass) ? j_pass->valuestring : "";

    ESP_LOGI(TAG, "Connecting to: %s", ssid);
    wifi_sta_start(ssid, pass);

    /* Wait for STA to connect (up to 15s) */
    int retry = 0;
    while (!wifi_is_connected() && retry < 15) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        retry++;
    }

    cJSON *resp = cJSON_CreateObject();
    if (wifi_is_connected()) {
        /* Get STA IP */
        esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        esp_netif_ip_info_t ip_info;
        char ip_str[16] = "0.0.0.0";
        if (sta && esp_netif_get_ip_info(sta, &ip_info) == ESP_OK) {
            esp_ip4addr_ntoa(&ip_info.ip, ip_str, sizeof(ip_str));
        }
        cJSON_AddBoolToObject(resp, "ok", 1);
        cJSON_AddStringToObject(resp, "ip", ip_str);
        s_prov_done = true;
    } else {
        cJSON_AddBoolToObject(resp, "ok", 0);
        cJSON_AddStringToObject(resp, "msg", "Connection timeout");
    }

    char *json = cJSON_PrintUnformatted(resp);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);
    free(json);
    cJSON_Delete(resp);
    cJSON_Delete(root);
    return ESP_OK;
}

/* Redirect any other request to root (captive portal trigger) */
static esp_err_t redirect_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static void http_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.stack_size = 8192;

    if (httpd_start(&s_server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return;
    }

    httpd_uri_t uri_root = {.uri = "/", .method = HTTP_GET, .handler = root_handler};
    httpd_uri_t uri_scan = {.uri = "/scan", .method = HTTP_GET, .handler = scan_handler};
    httpd_uri_t uri_conn = {.uri = "/connect", .method = HTTP_POST, .handler = connect_handler};
    httpd_uri_t uri_redir = {.uri = "/*", .method = HTTP_GET, .handler = redirect_handler};

    httpd_register_uri_handler(s_server, &uri_root);
    httpd_register_uri_handler(s_server, &uri_scan);
    httpd_register_uri_handler(s_server, &uri_conn);
    httpd_register_uri_handler(s_server, &uri_redir);

    ESP_LOGI(TAG, "HTTP server started");
}

static void http_stop(void)
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
}

/* ── Public API ──────────────────────────────────────────────────────── */

void wifi_prov_start(void)
{
    s_prov_done = false;

    ESP_LOGI(TAG, "Starting WiFi provisioning AP: %s", AP_SSID);
    wifi_ap_start(AP_SSID);

    dns_start();
    http_start();

    /* Block until provisioning completes */
    while (!s_prov_done) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    ESP_LOGI(TAG, "Provisioning complete, cleaning up");
    vTaskDelay(pdMS_TO_TICKS(2000));  /* Let HTTP response reach client */

    http_stop();
    dns_stop();
    wifi_ap_stop();
}

bool wifi_prov_is_done(void)
{
    return s_prov_done;
}
