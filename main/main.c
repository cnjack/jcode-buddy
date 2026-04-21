#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_timer.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "lvgl.h"
#include "esp_lcd_axs15231b.h"
#include "user_config.h"
#include "i2c_bsp.h"
#include "lcd_bl_pwm_bsp.h"
#include "i2c_equipment.h"
#include "audio_bsp.h"
#include "wifi_bsp.h"
#include "wifi_prov.h"
#include "ui_main.h"
#include "ui_wifi_setup.h"
#include "app_ble.h"
#include "lvgl_lock.h"
#include "driver/i2c_master.h"

static const char *TAG = "main";

static SemaphoreHandle_t lvgl_mux = NULL;
static SemaphoreHandle_t flush_done_sem = NULL;
static uint16_t *trans_buf_1 = NULL;
static bool main_ui_created = false;

// BOOT button (GPIO0) for page switching
#define BOOT_BUTTON_GPIO  GPIO_NUM_0

/* ── Power-latch via TCA9554 IO6 ──────────────────────────────────────
 * The board's battery power circuit requires the MCU to drive TCA9554 IO6
 * HIGH after boot to keep the power on.  Without this, releasing the PWR
 * button cuts power immediately.
 */
#define TCA9554_ADDR       0x20
#define TCA9554_REG_OUTPUT 0x01
#define TCA9554_REG_CONFIG 0x03

static void power_latch_enable(void)
{
    i2c_master_bus_handle_t bus = NULL;
    if (i2c_master_get_bus_handle(0, &bus) != ESP_OK) {
        ESP_LOGE(TAG, "Power latch: cannot get I2C bus");
        return;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = TCA9554_ADDR,
        .scl_speed_hz    = 400000,
    };
    i2c_master_dev_handle_t dev = NULL;
    if (i2c_master_bus_add_device(bus, &dev_cfg, &dev) != ESP_OK) {
        ESP_LOGE(TAG, "Power latch: cannot add TCA9554 device");
        return;
    }

    uint8_t buf[2];

    /* Set IO6 as output (bit6 = 0 in config register) */
    buf[0] = TCA9554_REG_CONFIG;
    uint8_t cfg_val = 0xFF;
    i2c_master_transmit_receive(dev, buf, 1, &cfg_val, 1, 100);
    cfg_val &= ~(1 << 6);
    buf[0] = TCA9554_REG_CONFIG;
    buf[1] = cfg_val;
    i2c_master_transmit(dev, buf, 2, 100);

    /* Drive IO6 HIGH to latch power on */
    buf[0] = TCA9554_REG_OUTPUT;
    uint8_t out_val = 0x00;
    i2c_master_transmit_receive(dev, buf, 1, &out_val, 1, 100);
    out_val |= (1 << 6);
    buf[0] = TCA9554_REG_OUTPUT;
    buf[1] = out_val;
    i2c_master_transmit(dev, buf, 2, 100);

    i2c_master_bus_rm_device(dev);
    ESP_LOGI(TAG, "Power latch enabled (TCA9554 IO6 = HIGH)");
}

#define BYTES_PER_PIXEL (LV_COLOR_FORMAT_GET_SIZE(LV_COLOR_FORMAT_RGB565))
#define BUFF_SIZE (EXAMPLE_LCD_H_RES * EXAMPLE_LCD_V_RES * BYTES_PER_PIXEL)

#define LVGL_TICK_PERIOD_MS    5
#define LVGL_TASK_MAX_DELAY_MS 500
#define LVGL_TASK_MIN_DELAY_MS 10
#define LVGL_TASK_STACK_SIZE   (8 * 1024)
#define LVGL_TASK_PRIORITY     2

static const axs15231b_lcd_init_cmd_t lcd_init_cmds[] = {
    {0x11, (uint8_t[]){0x00}, 0, 100},
    {0x29, (uint8_t[]){0x00}, 0, 100},
};

static bool notify_flush_ready(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    BaseType_t high_task_awoken = pdFALSE;
    xSemaphoreGiveFromISR(flush_done_sem, &high_task_awoken);
    return false;
}

static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *color_p)
{
    esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t)lv_display_get_user_data(disp);

    // LVGL renders in landscape (640w x 172h). The QSPI display only sends CASET
    // (no RASET), so data MUST be in portrait format (172w x 640h).
    // We transpose the framebuffer: portrait[row][col] = landscape[col][row]
    // where portrait row = landscape X (0-639), portrait col = landscape Y (0-171).

    const int lw = EXAMPLE_LCD_H_RES;  // 640 landscape width
    const int lh = EXAMPLE_LCD_V_RES;  // 172 landscape height
    const int pw = lh;                 // 172 portrait width
    const int ph = lw;                 // 640 portrait height

    // Byte-swap the entire landscape buffer first (sequential, cache-friendly)
    lv_draw_sw_rgb565_swap(color_p, lw * lh);

    uint16_t *landscape = (uint16_t *)color_p;
    int max_lines = LVGL_DMA_BUFF_LEN / (pw * 2);
    if (max_lines < 1) max_lines = 1;

    int py = 0;
    xSemaphoreGive(flush_done_sem);
    while (py < ph) {
        int lines = ph - py;
        if (lines > max_lines) lines = max_lines;

        // Wait for previous DMA transfer to finish before writing to trans_buf_1
        xSemaphoreTake(flush_done_sem, portMAX_DELAY);

        // Transpose landscape → portrait into DMA buffer (with Y-flip to fix mirror)
        uint16_t *dst = trans_buf_1;
        for (int row = py; row < py + lines; row++) {
            for (int col = 0; col < pw; col++) {
                *dst++ = landscape[(lh - 1 - col) * lw + row];
            }
        }

        esp_lcd_panel_draw_bitmap(panel, 0, py, pw, py + lines, trans_buf_1);
        py += lines;
        taskYIELD();  // Prevent WDT starvation during long transpose
    }
    xSemaphoreTake(flush_done_sem, portMAX_DELAY);
    lv_disp_flush_ready(disp);
}

static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    uint8_t cmd[11] = {0xb5, 0xab, 0xa5, 0x5a, 0x0, 0x0, 0x0, 0x0e, 0x0, 0x0, 0x0};
    uint8_t buf[32] = {0};
    ESP_ERROR_CHECK_WITHOUT_ABORT(i2c_master_write_read_dev(disp_touch_dev_handle, cmd, 11, buf, 32));

    /* Touch panel reports in portrait coordinates:
     *   px (buf[2:3]) = portrait Y (0-639) → landscape X
     *   py (buf[4:5]) = portrait X (0-171) → landscape Y
     */
    uint16_t px = (((uint16_t)buf[2] & 0x0f) << 8) | (uint16_t)buf[3];
    uint16_t py = (((uint16_t)buf[4] & 0x0f) << 8) | (uint16_t)buf[5];

    if (buf[1] > 0 && buf[1] < 5) {
        data->state = LV_INDEV_STATE_PRESSED;

        /* Clamp to actual display ranges */
        if (px >= EXAMPLE_LCD_H_RES) px = EXAMPLE_LCD_H_RES - 1;  /* 0-639 */
        if (py >= EXAMPLE_LCD_V_RES) py = EXAMPLE_LCD_V_RES - 1;  /* 0-171 */

        /* Map portrait→landscape, X mirrored to match display transpose */
        data->point.x = (EXAMPLE_LCD_H_RES - 1 - px);
        data->point.y = (EXAMPLE_LCD_V_RES - 1 - py);

    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

static void lvgl_tick_cb(void *arg)
{
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

bool lvgl_lock(int timeout_ms)
{
    const TickType_t ticks = (timeout_ms == -1) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTake(lvgl_mux, ticks) == pdTRUE;
}

void lvgl_unlock(void)
{
    xSemaphoreGive(lvgl_mux);
}

static void lvgl_task(void *arg)
{
    uint32_t delay_ms = LVGL_TASK_MAX_DELAY_MS;
    for (;;) {
        if (lvgl_lock(-1)) {
            delay_ms = lv_timer_handler();
            lvgl_unlock();
        }
        if (delay_ms > LVGL_TASK_MAX_DELAY_MS) delay_ms = LVGL_TASK_MAX_DELAY_MS;
        else if (delay_ms < LVGL_TASK_MIN_DELAY_MS) delay_ms = LVGL_TASK_MIN_DELAY_MS;
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

static void clock_update_task(void *arg)
{
    // Wait for main UI to be created
    while (!main_ui_created) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    ESP_LOGI(TAG, "Clock task starting UI updates");
    for (;;) {
        if (lvgl_lock(100)) {
            ui_clock_update();
            lvgl_unlock();
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static void button_task(void *arg)
{
    // Configure BOOT button (GPIO0) as input with pull-up
    gpio_config_t btn_cfg = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << BOOT_BUTTON_GPIO),
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    gpio_config(&btn_cfg);

    bool last_pressed = false;
    for (;;) {
        bool pressed = (gpio_get_level(BOOT_BUTTON_GPIO) == 0);
        if (pressed && !last_pressed) {
            if (lvgl_lock(100)) {
                ui_main_toggle_page();
                lvgl_unlock();
            }
            ESP_LOGI(TAG, "Page toggled");
        }
        last_pressed = pressed;
        vTaskDelay(pdMS_TO_TICKS(50));  // Debounce
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== 流光 Smart Desktop Companion ===");

    lcd_bl_pwm_bsp_init(100);
    flush_done_sem = xSemaphoreCreateBinary();
    assert(flush_done_sem);

    i2c_master_init();
    power_latch_enable();  /* Must be called ASAP after I2C init to keep battery power on */
    touch_i2c_master_init();
    i2c_rtc_setup();
    i2c_qmi_setup();

    ESP_LOGI(TAG, "Init SPI bus for LCD");
    gpio_config_t rst_gpio = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << EXAMPLE_PIN_NUM_LCD_RST),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&rst_gpio);

    spi_bus_config_t buscfg = {
        .sclk_io_num = EXAMPLE_PIN_NUM_LCD_PCLK,
        .data0_io_num = EXAMPLE_PIN_NUM_LCD_DATA0,
        .data1_io_num = EXAMPLE_PIN_NUM_LCD_DATA1,
        .data2_io_num = EXAMPLE_PIN_NUM_LCD_DATA2,
        .data3_io_num = EXAMPLE_PIN_NUM_LCD_DATA3,
        .max_transfer_sz = LVGL_DMA_BUFF_LEN,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_handle_t panel_io = NULL;
    esp_lcd_panel_handle_t panel = NULL;

    esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = EXAMPLE_PIN_NUM_LCD_CS,
        .dc_gpio_num = -1,
        .spi_mode = 3,
        .pclk_hz = 40 * 1000 * 1000,
        .trans_queue_depth = 10,
        .on_color_trans_done = notify_flush_ready,
        .lcd_cmd_bits = 32,
        .lcd_param_bits = 8,
        .flags.quad_mode = true,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(LCD_HOST, &io_config, &panel_io));

    axs15231b_vendor_config_t vendor_config = {
        .flags.use_qspi_interface = 1,
        .init_cmds = lcd_init_cmds,
        .init_cmds_size = sizeof(lcd_init_cmds) / sizeof(lcd_init_cmds[0]),
    };
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = -1,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = LCD_BIT_PER_PIXEL,
        .vendor_config = &vendor_config,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_axs15231b(panel_io, &panel_config, &panel));

    gpio_set_level(EXAMPLE_PIN_NUM_LCD_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(30));
    gpio_set_level(EXAMPLE_PIN_NUM_LCD_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(250));
    gpio_set_level(EXAMPLE_PIN_NUM_LCD_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(30));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    
    // No hardware rotation - flush callback transposes landscape→portrait manually
    
    ESP_LOGI(TAG, "Init LVGL");
    lv_init();
    lv_display_t *disp = lv_display_create(EXAMPLE_LCD_H_RES, EXAMPLE_LCD_V_RES);
    lv_display_set_flush_cb(disp, lvgl_flush_cb);

    ESP_LOGI(TAG, "Allocating display buffers: BUFF_SIZE=%d bytes (%.1f KB)", 
             BUFF_SIZE, BUFF_SIZE/1024.0);
    
    // Use single buffer to save memory (640x172x2 = 220KB is large)
    uint8_t *buf1 = (uint8_t *)heap_caps_malloc(BUFF_SIZE, MALLOC_CAP_SPIRAM);
    trans_buf_1 = (uint16_t *)heap_caps_malloc(LVGL_DMA_BUFF_LEN, MALLOC_CAP_DMA);
    
    if (!buf1) {
        ESP_LOGE(TAG, "Failed to allocate PSRAM buffer (size: %d)", BUFF_SIZE);
    }
    if (!trans_buf_1) {
        ESP_LOGE(TAG, "Failed to allocate DMA buffer (size: %d)", LVGL_DMA_BUFF_LEN);
    }
    assert(buf1 && trans_buf_1);
    
    ESP_LOGI(TAG, "Display buffers allocated successfully");

    lv_display_set_buffers(disp, buf1, NULL, BUFF_SIZE, LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_user_data(disp, panel);

    lv_indev_t *touch = lv_indev_create();
    lv_indev_set_type(touch, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(touch, touch_read_cb);

    esp_timer_handle_t tick_timer;
    esp_timer_create(&(esp_timer_create_args_t){.callback = lvgl_tick_cb, .name = "lvgl_tick"}, &tick_timer);
    esp_timer_start_periodic(tick_timer, LVGL_TICK_PERIOD_MS * 1000);

    lvgl_mux = xSemaphoreCreateMutex();
    assert(lvgl_mux);

    xTaskCreatePinnedToCore(lvgl_task, "LVGL", LVGL_TASK_STACK_SIZE, NULL, LVGL_TASK_PRIORITY, NULL, 0);

    // Create main UI directly
    ESP_LOGI(TAG, "Creating main UI...");
    if (lvgl_lock(-1)) {
        ui_main_create();
        main_ui_created = true;
        lv_refr_now(NULL);
        lvgl_unlock();
    }

    audio_bsp_init();

    /* NVS init first -- required by both BLE (RF cal) and WiFi */
    {
        esp_err_t ret = nvs_flash_init();
        if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            nvs_flash_erase();
            nvs_flash_init();
        }
    }

    /* BLE init before WiFi -- must start before WiFi connects (shared RF resources) */
    app_ble_init();

    /* -- WiFi initialization -- */
    wifi_init();

    char ssid[33] = {0};
    char pass[65] = {0};
    bool has_cred = wifi_load_credentials(ssid, sizeof(ssid), pass, sizeof(pass));

    if (has_cred) {
        ESP_LOGI(TAG, "WiFi credentials found, connecting to: %s", ssid);
        wifi_sta_start(ssid, pass);

        /* Wait for connection (up to 10s) */
        int retry = 0;
        while (!wifi_is_connected() && retry < 10) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            retry++;
        }
        if (!wifi_is_connected()) {
            ESP_LOGW(TAG, "Saved WiFi failed, starting provisioning");
            wifi_sta_stop();
            has_cred = false;
        }
    }

    if (!has_cred) {
        ESP_LOGI(TAG, "Starting WiFi provisioning (AP captive portal)");
        /* Show WiFi setup UI overlay */
        lv_obj_t *wifi_scr = NULL;
        if (lvgl_lock(-1)) {
            wifi_scr = lv_obj_create(lv_screen_active());
            lv_obj_remove_style_all(wifi_scr);
            lv_obj_set_size(wifi_scr, LV_HOR_RES, LV_VER_RES);
            ui_wifi_setup_create(wifi_scr);
            lv_refr_now(NULL);
            lvgl_unlock();
        }

        /* Blocks until user configures WiFi via captive portal */
        wifi_prov_start();

        if (lvgl_lock(200)) {
            ui_wifi_setup_set_status("Connected!");
            lvgl_unlock();
        }
        vTaskDelay(pdMS_TO_TICKS(1500));

        /* Remove WiFi setup overlay */
        if (wifi_scr && lvgl_lock(-1)) {
            lv_obj_delete(wifi_scr);
            lv_refr_now(NULL);
            lvgl_unlock();
        }
    }

    xTaskCreatePinnedToCore(clock_update_task, "Clock", 4 * 1024, NULL, 3, NULL, 1);
    xTaskCreatePinnedToCore(button_task, "Button", 2 * 1024, NULL, 1, NULL, 1);

    /* Start SNTP after WiFi is connected */
    if (wifi_is_connected()) {
        sntp_start();
        sntp_wait_sync();
    }

    ESP_LOGI(TAG, "All tasks started");
}
