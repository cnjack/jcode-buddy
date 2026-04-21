#include "asr_client.h"
#include "audio_bsp.h"
#include "user_config.h"
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"
#include "mbedtls/base64.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include <string.h>

static const char *TAG = "asr_client";

extern const char dashscope_ca_pem_start[] asm("_binary_dashscope_ca_pem_start");
extern const char dashscope_ca_pem_end[]   asm("_binary_dashscope_ca_pem_end");

#define ASR_WS_URL       "wss://dashscope.aliyuncs.com/api-ws/v1/realtime?model=qwen3-asr-flash-realtime"
#define ASR_API_KEY       CONFIG_ASR_API_KEY
#define ASR_AUDIO_CHUNK   1024       /* 1024 bytes = 512 samples @ 16kHz/16bit = 32ms */
#define ASR_SEND_INTERVAL_MS  30     /* Send interval matching ~32ms chunks */
#define ASR_TASK_STACK    (8 * 1024)

/* Event bits */
#define EVT_WS_CONNECTED   BIT0
#define EVT_SESSION_READY  BIT1
#define EVT_STOP_REQUEST   BIT2
#define EVT_SESSION_DONE   BIT3

static esp_websocket_client_handle_t s_ws = NULL;
static TaskHandle_t s_asr_task = NULL;
static EventGroupHandle_t s_evt = NULL;
static asr_callbacks_t s_cbs = {0};
static volatile bool s_active = false;

static void send_json(const char *json)
{
    if (s_ws) {
        esp_websocket_client_send_text(s_ws, json, strlen(json), portMAX_DELAY);
    }
}

static void send_session_update(void)
{
    const char *msg =
        "{"
        "\"type\":\"session.update\","
        "\"session\":{"
            "\"input_audio_format\":\"pcm\","
            "\"sample_rate\":16000,"
            "\"input_audio_transcription\":{\"language\":\"en\"},"
            "\"turn_detection\":{\"type\":\"server_vad\",\"threshold\":0.0,\"silence_duration_ms\":400}"
        "}"
        "}";
    send_json(msg);
    ESP_LOGI(TAG, "Sent session.update");
}

static void send_session_finish(void)
{
    const char *msg = "{\"type\":\"session.finish\"}";
    send_json(msg);
    ESP_LOGI(TAG, "Sent session.finish");
}

static void send_audio_chunk(const uint8_t *pcm, uint32_t len)
{
    /* Base64 encode the PCM chunk */
    size_t b64_len = 0;
    mbedtls_base64_encode(NULL, 0, &b64_len, pcm, len);

    /* Build JSON: {"type":"input_audio_buffer.append","audio":"<base64>"} */
    size_t json_len = 50 + b64_len + 4;
    char *json = heap_caps_malloc(json_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!json) return;

    int offset = snprintf(json, json_len,
        "{\"type\":\"input_audio_buffer.append\",\"audio\":\"");

    size_t written = 0;
    mbedtls_base64_encode((unsigned char *)json + offset, json_len - offset,
                          &written, pcm, len);
    offset += written;
    json[offset++] = '"';
    json[offset++] = '}';
    json[offset] = '\0';

    esp_websocket_client_send_text(s_ws, json, offset, portMAX_DELAY);
    free(json);
}

static void handle_server_event(const char *data, int len)
{
    cJSON *root = cJSON_ParseWithLength(data, len);
    if (!root) return;

    cJSON *type_obj = cJSON_GetObjectItem(root, "type");
    if (!type_obj || !cJSON_IsString(type_obj)) {
        cJSON_Delete(root);
        return;
    }
    const char *type = type_obj->valuestring;

    if (strcmp(type, "session.created") == 0) {
        ESP_LOGI(TAG, "Session created");
        send_session_update();

    } else if (strcmp(type, "session.updated") == 0) {
        ESP_LOGI(TAG, "Session updated, ready to stream");
        xEventGroupSetBits(s_evt, EVT_SESSION_READY);

    } else if (strcmp(type, "conversation.item.input_audio_transcription.text") == 0) {
        cJSON *text_obj  = cJSON_GetObjectItem(root, "text");
        cJSON *stash_obj = cJSON_GetObjectItem(root, "stash");
        const char *text  = (text_obj && cJSON_IsString(text_obj))   ? text_obj->valuestring  : "";
        const char *stash = (stash_obj && cJSON_IsString(stash_obj)) ? stash_obj->valuestring : "";
        if (s_cbs.on_text) {
            s_cbs.on_text(text, stash);
        }

    } else if (strcmp(type, "conversation.item.input_audio_transcription.completed") == 0) {
        cJSON *tr = cJSON_GetObjectItem(root, "transcript");
        const char *transcript = (tr && cJSON_IsString(tr)) ? tr->valuestring : "";
        ESP_LOGI(TAG, "Final: %s", transcript);
        if (s_cbs.on_done) {
            s_cbs.on_done(transcript);
        }

    } else if (strcmp(type, "session.finished") == 0) {
        ESP_LOGI(TAG, "Session finished");
        xEventGroupSetBits(s_evt, EVT_SESSION_DONE);

    } else if (strcmp(type, "error") == 0) {
        cJSON *err = cJSON_GetObjectItem(root, "error");
        cJSON *msg = err ? cJSON_GetObjectItem(err, "message") : NULL;
        const char *errmsg = (msg && cJSON_IsString(msg)) ? msg->valuestring : "unknown";
        ESP_LOGE(TAG, "Server error: %s", errmsg);
        if (s_cbs.on_error) {
            s_cbs.on_error(errmsg);
        }

    } else if (strcmp(type, "input_audio_buffer.speech_started") == 0) {
        ESP_LOGI(TAG, "VAD: speech started");

    } else if (strcmp(type, "input_audio_buffer.speech_stopped") == 0) {
        ESP_LOGI(TAG, "VAD: speech stopped");
    }

    cJSON_Delete(root);
}

static void ws_event_handler(void *arg, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;

    /* Guard: event group may be deleted during cleanup */
    if (!s_evt) return;

    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "WebSocket connected");
        if (s_evt) xEventGroupSetBits(s_evt, EVT_WS_CONNECTED);
        break;

    case WEBSOCKET_EVENT_DATA:
        if (data->op_code == 0x01 && data->data_len > 0) {  /* text frame */
            handle_server_event(data->data_ptr, data->data_len);
        }
        break;

    case WEBSOCKET_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "WebSocket disconnected");
        if (s_evt) xEventGroupSetBits(s_evt, EVT_STOP_REQUEST);
        break;

    case WEBSOCKET_EVENT_ERROR:
        ESP_LOGE(TAG, "WebSocket error");
        if (s_cbs.on_error) s_cbs.on_error("WebSocket connection error");
        if (s_evt) xEventGroupSetBits(s_evt, EVT_STOP_REQUEST);
        break;
    }
}

static void asr_task(void *arg)
{
    ESP_LOGI(TAG, "ASR task started");

    /* Start recording */
    if (audio_record_start() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start recording");
        if (s_cbs.on_error) s_cbs.on_error("Failed to start recording");
        goto cleanup;
    }

    /* Connect WebSocket */
    char auth_header[256];
    snprintf(auth_header, sizeof(auth_header), "Authorization: bearer %s\r\n", ASR_API_KEY);
    ESP_LOGI(TAG, "Connecting WS to: %s", ASR_WS_URL);
    ESP_LOGI(TAG, "API key (first 8): %.8s...", ASR_API_KEY);

    esp_websocket_client_config_t ws_cfg = {
        .uri = ASR_WS_URL,
        .headers = auth_header,
        .buffer_size = 2048,
        .task_stack = 8 * 1024,
        .cert_pem = dashscope_ca_pem_start,
        .reconnect_timeout_ms = 5000,
        .network_timeout_ms = 10000,
    };
    s_ws = esp_websocket_client_init(&ws_cfg);
    if (!s_ws) {
        ESP_LOGE(TAG, "WS client init failed");
        if (s_cbs.on_error) s_cbs.on_error("WebSocket init failed");
        goto cleanup;
    }

    esp_websocket_register_events(s_ws, WEBSOCKET_EVENT_ANY, ws_event_handler, NULL);
    esp_websocket_client_start(s_ws);

    /* Wait for connection */
    EventBits_t bits = xEventGroupWaitBits(s_evt, EVT_WS_CONNECTED | EVT_STOP_REQUEST,
                                            pdTRUE, pdFALSE, pdMS_TO_TICKS(10000));
    if (!(bits & EVT_WS_CONNECTED)) {
        ESP_LOGE(TAG, "WS connection timeout");
        if (s_cbs.on_error) s_cbs.on_error("Connection timeout");
        goto cleanup;
    }

    /* Wait for session.updated after session.created → session.update → session.updated */
    bits = xEventGroupWaitBits(s_evt, EVT_SESSION_READY | EVT_STOP_REQUEST,
                                pdTRUE, pdFALSE, pdMS_TO_TICKS(10000));
    if (!(bits & EVT_SESSION_READY)) {
        ESP_LOGE(TAG, "Session setup timeout");
        if (s_cbs.on_error) s_cbs.on_error("Session setup timeout");
        goto cleanup;
    }

    ESP_LOGI(TAG, "Streaming audio...");
    if (s_cbs.on_ready) s_cbs.on_ready();

    /* Audio streaming loop */
    uint8_t *audio_buf = heap_caps_malloc(ASR_AUDIO_CHUNK, MALLOC_CAP_DEFAULT);
    if (!audio_buf) goto cleanup;

    while (s_active) {
        /* Check for stop */
        bits = xEventGroupGetBits(s_evt);
        if (bits & EVT_STOP_REQUEST) break;

        int nread = audio_record_read(audio_buf, ASR_AUDIO_CHUNK);
        if (nread > 0 && esp_websocket_client_is_connected(s_ws)) {
            send_audio_chunk(audio_buf, nread);
        }

        vTaskDelay(pdMS_TO_TICKS(ASR_SEND_INTERVAL_MS));
    }
    free(audio_buf);

    /* Send finish and wait for server response */
    if (esp_websocket_client_is_connected(s_ws)) {
        send_session_finish();
        xEventGroupWaitBits(s_evt, EVT_SESSION_DONE | EVT_STOP_REQUEST,
                            pdTRUE, pdFALSE, pdMS_TO_TICKS(5000));
    }

cleanup:
    audio_record_stop();

    if (s_ws) {
        /* Stop WS client first to prevent further event callbacks */
        esp_websocket_client_close(s_ws, pdMS_TO_TICKS(2000));
        esp_websocket_client_destroy(s_ws);
        s_ws = NULL;
    }

    /* Small delay to let any pending WS events drain */
    vTaskDelay(pdMS_TO_TICKS(100));

    s_active = false;
    s_asr_task = NULL;

    if (s_evt) {
        EventGroupHandle_t tmp = s_evt;
        s_evt = NULL;
        vEventGroupDelete(tmp);
    }

    ESP_LOGI(TAG, "ASR task finished");
    vTaskDelete(NULL);
}

bool asr_start(const asr_callbacks_t *cbs)
{
    ESP_LOGI(TAG, "asr_start() called, active=%d", s_active);
    if (s_active) {
        ESP_LOGW(TAG, "asr_start: already active, returning false");
        return false;
    }

    if (cbs) {
        s_cbs = *cbs;
    } else {
        memset(&s_cbs, 0, sizeof(s_cbs));
    }

    s_evt = xEventGroupCreate();
    if (!s_evt) {
        ESP_LOGE(TAG, "asr_start: failed to create event group");
        return false;
    }

    s_active = true;
    ESP_LOGI(TAG, "Creating ASR task on core 1...");
    BaseType_t ret = xTaskCreatePinnedToCore(asr_task, "ASR", ASR_TASK_STACK, NULL, 5, &s_asr_task, 1);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "asr_start: xTaskCreate failed (%d)", ret);
        s_active = false;
        vEventGroupDelete(s_evt);
        s_evt = NULL;
        return false;
    }

    ESP_LOGI(TAG, "asr_start: task created successfully");
    return true;
}

void asr_stop(void)
{
    if (!s_active) return;
    s_active = false;
    if (s_evt) {
        xEventGroupSetBits(s_evt, EVT_STOP_REQUEST);
    }
}

bool asr_is_active(void)
{
    return s_active;
}
