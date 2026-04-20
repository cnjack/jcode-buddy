#include "audio_bsp.h"
#include "esp_log.h"
#include "esp_codec_dev.h"
#include "codec_board.h"
#include "codec_init.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "audio_bsp";

/* TCA9554 PA control — use I2C directly to set IO7 HIGH */
#include "driver/i2c_master.h"

#define TCA9554_ADDR       0x20
#define TCA9554_REG_OUTPUT 0x01
#define TCA9554_REG_CONFIG 0x03

static esp_err_t pa_enable(void)
{
    i2c_master_bus_handle_t bus = NULL;
    esp_err_t ret = i2c_master_get_bus_handle(0, &bus);
    if (ret != ESP_OK) return ret;

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = TCA9554_ADDR,
        .scl_speed_hz    = 400000,
    };
    i2c_master_dev_handle_t dev = NULL;
    ret = i2c_master_bus_add_device(bus, &dev_cfg, &dev);
    if (ret != ESP_OK) return ret;

    /* Read current config register, set IO7 as output (bit7 = 0) */
    uint8_t buf[2];
    buf[0] = TCA9554_REG_CONFIG;
    uint8_t cfg_val = 0xFF;
    i2c_master_transmit_receive(dev, buf, 1, &cfg_val, 1, 100);
    cfg_val &= ~(1 << 7);  /* IO7 = output */
    buf[0] = TCA9554_REG_CONFIG;
    buf[1] = cfg_val;
    i2c_master_transmit(dev, buf, 2, 100);

    /* Read current output register, set IO7 HIGH */
    buf[0] = TCA9554_REG_OUTPUT;
    uint8_t out_val = 0x00;
    i2c_master_transmit_receive(dev, buf, 1, &out_val, 1, 100);
    out_val |= (1 << 7);  /* IO7 = HIGH */
    buf[0] = TCA9554_REG_OUTPUT;
    buf[1] = out_val;
    i2c_master_transmit(dev, buf, 2, 100);

    i2c_master_bus_rm_device(dev);
    return ESP_OK;
}

static esp_codec_dev_handle_t s_playback = NULL;
static SemaphoreHandle_t      s_audio_mux = NULL;
static bool                   s_ready = false;

void audio_bsp_init(void)
{
    if (s_ready) return;

    s_audio_mux = xSemaphoreCreateMutex();
    assert(s_audio_mux);

    set_codec_board_type("S3_LCD_3_49");
    codec_init_cfg_t codec_cfg = {
        .in_mode   = CODEC_I2S_MODE_TDM,
        .out_mode  = CODEC_I2S_MODE_TDM,
        .in_use_tdm = false,
        .reuse_dev  = false,
    };
    int ret = init_codec(&codec_cfg);
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to initialize codec: %d", ret);
        return;
    }

    s_playback = get_playback_handle();
    if (!s_playback) {
        ESP_LOGE(TAG, "Failed to get playback handle");
        return;
    }

    /* Set volume + open playback device (matching Waveshare FactoryProgram) */
    esp_codec_dev_set_out_vol(s_playback, 100);
    esp_codec_dev_sample_info_t fs = {
        .sample_rate    = 24000,
        .channel        = 2,
        .bits_per_sample = 16,
    };
    if (esp_codec_dev_open(s_playback, &fs) != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Failed to open playback device");
        return;
    }

    s_ready = true;
    ESP_LOGI(TAG, "Audio codec initialized (ES8311, 24kHz/16bit/stereo)");

    /* Enable PA via TCA9554 IO7 */
    if (pa_enable() == ESP_OK) {
        ESP_LOGI(TAG, "PA enabled (TCA9554 IO7)");
    } else {
        ESP_LOGW(TAG, "Failed to enable PA via TCA9554");
    }
}

void audio_play_pcm(const uint8_t *data, uint32_t len)
{
    if (!s_ready || !data || len == 0) return;

    xSemaphoreTake(s_audio_mux, portMAX_DELAY);

    uint32_t written = 0;
    while (written < len) {
        uint32_t to_write = len - written;
        if (to_write > 256) to_write = 256;
        esp_codec_dev_write(s_playback, (void *)(data + written), (int)to_write);
        written += to_write;
    }

    xSemaphoreGive(s_audio_mux);
}

void audio_set_volume(uint8_t vol)
{
    if (!s_ready) return;
    if (vol > 100) vol = 100;
    esp_codec_dev_set_out_vol(s_playback, (float)vol);
}

bool audio_is_ready(void)
{
    return s_ready;
}
