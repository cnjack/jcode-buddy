#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "esp_log.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "cJSON.h"
#include "lvgl_lock.h"
#include "app_ble.h"
#include "ui_ble.h"
#include "audio_bsp.h"
#include "freertos/queue.h"

static const char *TAG = "app_ble";

/* ── Embedded PCM audio (16-bit, 24 kHz, stereo) ─────────────────────── */
extern const uint8_t snd_connected_start[]  asm("_binary_connected_pcm_start");
extern const uint8_t snd_connected_end[]    asm("_binary_connected_pcm_end");
extern const uint8_t snd_successful_start[] asm("_binary_successful_pcm_start");
extern const uint8_t snd_successful_end[]   asm("_binary_successful_pcm_end");

/* Audio playback queue — plays sounds without blocking BLE/LVGL tasks */
typedef enum { SND_CONNECTED = 0, SND_SUCCESSFUL } snd_id_t;
static QueueHandle_t s_snd_queue = NULL;

static void audio_task(void *arg)
{
    snd_id_t id;
    for (;;) {
        if (xQueueReceive(s_snd_queue, &id, portMAX_DELAY) == pdTRUE) {
            switch (id) {
            case SND_CONNECTED:
                audio_play_pcm(snd_connected_start,
                               snd_connected_end - snd_connected_start);
                break;
            case SND_SUCCESSFUL:
                audio_play_pcm(snd_successful_start,
                               snd_successful_end - snd_successful_start);
                break;
            }
        }
    }
}

static void play_sound(snd_id_t id)
{
    if (s_snd_queue) {
        xQueueOverwrite(s_snd_queue, &id);
    }
}

/* ── NUS UUIDs (128-bit, stored little-endian for NimBLE) ─────────────────
 * Service:  6E400001-B5A3-F393-E0A9-E50E24DCCA9E
 * RX (W):   6E400002-B5A3-F393-E0A9-E50E24DCCA9E
 * TX (N):   6E400003-B5A3-F393-E0A9-E50E24DCCA9E
 */
static const ble_uuid128_t NUS_SVC_UUID = BLE_UUID128_INIT(
    0x9E,0xCA,0xDC,0x24, 0x0E,0xE5,0xA9,0xE0,
    0x93,0xF3,0xA3,0xB5, 0x01,0x00,0x40,0x6E);

static const ble_uuid128_t NUS_RX_UUID = BLE_UUID128_INIT(
    0x9E,0xCA,0xDC,0x24, 0x0E,0xE5,0xA9,0xE0,
    0x93,0xF3,0xA3,0xB5, 0x02,0x00,0x40,0x6E);

static const ble_uuid128_t NUS_TX_UUID = BLE_UUID128_INIT(
    0x9E,0xCA,0xDC,0x24, 0x0E,0xE5,0xA9,0xE0,
    0x93,0xF3,0xA3,0xB5, 0x03,0x00,0x40,0x6E);

static uint16_t s_tx_handle;
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static bool     s_connected   = false;

/* NDJSON line buffer — accumulate bytes until '\n' */
#define RX_BUF_SIZE 1024
static char s_rx_buf[RX_BUF_SIZE];
static int  s_rx_pos = 0;

static void process_ndjson_line(const char *line);
static void start_adv(void);

static wifi_cred_cb_t s_wifi_cb = NULL;

void app_ble_set_wifi_cb(wifi_cred_cb_t cb)
{
    s_wifi_cb = cb;
}

/* ── GATT access callbacks ──────────────────────────────────────────────── */

static int nus_rx_chr_cb(uint16_t conn_handle, uint16_t attr_handle,
                        struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return 0;

    uint16_t pkt_len = OS_MBUF_PKTLEN(ctxt->om);
    uint8_t  frag[247];
    uint16_t copy_len = (pkt_len > sizeof(frag)) ? sizeof(frag) : pkt_len;
    ble_hs_mbuf_to_flat(ctxt->om, frag, copy_len, NULL);

    for (uint16_t i = 0; i < copy_len; i++) {
        char c = (char)frag[i];
        if (c == '\n') {
            s_rx_buf[s_rx_pos] = '\0';
            if (s_rx_pos > 0) process_ndjson_line(s_rx_buf);
            s_rx_pos = 0;
        } else if (s_rx_pos < RX_BUF_SIZE - 1) {
            s_rx_buf[s_rx_pos++] = c;
        }
    }
    return 0;
}

static int nus_tx_chr_cb(uint16_t conn_handle, uint16_t attr_handle,
                        struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    return BLE_ATT_ERR_READ_NOT_PERMITTED;
}

/* ── GATT service table ─────────────────────────────────────────────────── */

static const struct ble_gatt_svc_def s_nus_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &NUS_SVC_UUID.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid      = &NUS_RX_UUID.u,
                .access_cb = nus_rx_chr_cb,
                .flags     = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {
                .uuid       = &NUS_TX_UUID.u,
                .access_cb  = nus_tx_chr_cb,
                .val_handle = &s_tx_handle,
                .flags      = BLE_GATT_CHR_F_NOTIFY,
            },
            { 0 },
        },
    },
    { 0 },
};

/* ── JSON processing ────────────────────────────────────────────────────── */

static void process_ndjson_line(const char *line)
{
    cJSON *root = cJSON_Parse(line);
    if (!root) {
        ESP_LOGW(TAG, "JSON parse error: %.80s", line);
        return;
    }

    cJSON *j_cmd = cJSON_GetObjectItemCaseSensitive(root, "cmd");
    cJSON *j_val = cJSON_GetObjectItemCaseSensitive(root, "val");

    if (cJSON_IsString(j_cmd)) {
        const char *cmd = j_cmd->valuestring;
        const char *val = cJSON_IsString(j_val) ? j_val->valuestring : "";

        /* WiFi credentials command: {"cmd":"wifi","ssid":"xxx","pass":"xxx"} */
        if (strcmp(cmd, "wifi") == 0) {
            cJSON *j_ssid = cJSON_GetObjectItemCaseSensitive(root, "ssid");
            cJSON *j_pass = cJSON_GetObjectItemCaseSensitive(root, "pass");
            if (cJSON_IsString(j_ssid) && cJSON_IsString(j_pass) && s_wifi_cb) {
                ESP_LOGI(TAG, "WiFi credentials received: %s", j_ssid->valuestring);
                s_wifi_cb(j_ssid->valuestring, j_pass->valuestring);
            }
            cJSON_Delete(root);
            return;
        }

        if (lvgl_lock(200)) {
            if (strcmp(cmd, "heart") == 0) {
                ui_ble_add_message(val);
            } else if (strcmp(cmd, "idle")      == 0 ||
                       strcmp(cmd, "working")   == 0 ||
                       strcmp(cmd, "attention") == 0 ||
                       strcmp(cmd, "complete")  == 0) {
                ui_ble_set_pet_state(cmd);
                if (strcmp(cmd, "complete") == 0) {
                    play_sound(SND_SUCCESSFUL);
                }
                if (val[0] != '\0') {
                    ui_ble_add_message(val);
                }
            }
            lvgl_unlock();
        }
    }
    cJSON_Delete(root);
}

/* ── GAP event handler ──────────────────────────────────────────────────── */

static int gap_event_cb(struct ble_gap_event *ev, void *arg)
{
    switch (ev->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (ev->connect.status == 0) {
            s_conn_handle = ev->connect.conn_handle;
            s_connected   = true;
            s_rx_pos      = 0;
            ble_att_set_preferred_mtu(247);
            ble_gattc_exchange_mtu(s_conn_handle, NULL, NULL);
            ESP_LOGI(TAG, "BLE connected, handle=%d", s_conn_handle);
            play_sound(SND_CONNECTED);
            if (lvgl_lock(200)) {
                ui_ble_set_pet_state("idle");
                ui_ble_set_connected(true);
                lvgl_unlock();
            }
        } else {
            s_connected = false;
            start_adv();
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        s_connected   = false;
        s_rx_pos      = 0;
        ESP_LOGI(TAG, "BLE disconnected, reason=%d", ev->disconnect.reason);
        if (lvgl_lock(200)) {
            ui_ble_set_pet_state("sleep");
            ui_ble_set_connected(false);
            lvgl_unlock();
        }
        start_adv();
        break;

    default:
        break;
    }
    return 0;
}

/* ── Advertising ────────────────────────────────────────────────────────── */

static void start_adv(void)
{
    struct ble_gap_adv_params params = {0};
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    struct ble_hs_adv_fields fields = {0};
    fields.flags                = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl           = BLE_HS_ADV_TX_PWR_LVL_AUTO;

    const char *name = ble_svc_gap_device_name();
    fields.name             = (uint8_t *)name;
    fields.name_len         = (uint8_t)strlen(name);
    fields.name_is_complete = 1;

    ble_gap_adv_set_fields(&fields);
    int rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                               &params, gap_event_cb, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv start failed: %d", rc);
    }
}

/* ── BLE host stack callbacks ───────────────────────────────────────────── */

static void ble_on_reset(int reason)
{
    ESP_LOGE(TAG, "BLE reset, reason=%d", reason);
}

static void ble_on_sync(void)
{
    uint8_t own_addr_type;
    ble_hs_util_ensure_addr(0);
    ble_hs_id_infer_auto(0, &own_addr_type);

    uint8_t addr[6];
    ble_hs_id_copy_addr(own_addr_type, addr, NULL);

    char device_name[20];
    snprintf(device_name, sizeof(device_name), "JCODE-%02X%02X", addr[1], addr[0]);
    ble_svc_gap_device_name_set(device_name);
    ESP_LOGI(TAG, "BLE advertising as: %s", device_name);

    start_adv();
}

static void ble_host_task(void *param)
{
    nimble_port_run();          /* blocks until nimble_port_stop() */
    nimble_port_freertos_deinit();
}

/* ── Public API ─────────────────────────────────────────────────────────── */

bool app_ble_is_connected(void)
{
    return s_connected;
}

void app_ble_init(void)
{
    int rc;

    /* Audio playback queue + task */
    s_snd_queue = xQueueCreate(1, sizeof(snd_id_t));
    assert(s_snd_queue);
    xTaskCreatePinnedToCore(audio_task, "AudioSnd", 4096, NULL, 2, NULL, 1);

    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nimble_port_init failed (%s), retrying...", esp_err_to_name(err));
        vTaskDelay(pdMS_TO_TICKS(500));
        err = nimble_port_init();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "nimble_port_init retry failed (%s)", esp_err_to_name(err));
            return;
        }
    }

    ble_hs_cfg.reset_cb = ble_on_reset;
    ble_hs_cfg.sync_cb  = ble_on_sync;

    ble_svc_gap_init();
    ble_svc_gatt_init();

    rc = ble_gatts_count_cfg(s_nus_svcs);
    assert(rc == 0);
    rc = ble_gatts_add_svcs(s_nus_svcs);
    assert(rc == 0);

    nimble_port_freertos_init(ble_host_task);
    ESP_LOGI(TAG, "NUS BLE initialized");
}
