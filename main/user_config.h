#ifndef USER_CONFIG_H
#define USER_CONFIG_H

#include "driver/gpio.h"

// ==================== SPI ====================
#define SDSPI_HOST   SPI2_HOST
#define LCD_HOST     SPI3_HOST

// ==================== I2C Bus 0 (RTC + IMU) ====================
#define ESP32_SCL_NUM  (GPIO_NUM_48)
#define ESP32_SDA_NUM  (GPIO_NUM_47)

// ==================== I2C Bus 1 (Touch) ====================
#define Touch_SCL_NUM  (GPIO_NUM_18)
#define Touch_SDA_NUM  (GPIO_NUM_17)

// ==================== Display ====================
#define EXAMPLE_LCD_H_RES              640  // Landscape mode
#define EXAMPLE_LCD_V_RES              172
#define LVGL_DMA_BUFF_LEN    (EXAMPLE_LCD_V_RES * 40 * 2)  // 172*40*2=13760, portrait strip for DMA
#define LVGL_SPIRAM_BUFF_LEN (EXAMPLE_LCD_H_RES * EXAMPLE_LCD_V_RES * 2)

#define EXAMPLE_PIN_NUM_LCD_CS            (GPIO_NUM_9)
#define EXAMPLE_PIN_NUM_LCD_PCLK          (GPIO_NUM_10)
#define EXAMPLE_PIN_NUM_LCD_DATA0         (GPIO_NUM_11)
#define EXAMPLE_PIN_NUM_LCD_DATA1         (GPIO_NUM_12)
#define EXAMPLE_PIN_NUM_LCD_DATA2         (GPIO_NUM_13)
#define EXAMPLE_PIN_NUM_LCD_DATA3         (GPIO_NUM_14)
#define EXAMPLE_PIN_NUM_LCD_RST           (GPIO_NUM_21)
#define EXAMPLE_PIN_NUM_BK_LIGHT          (GPIO_NUM_8)

#define LCD_BIT_PER_PIXEL 16

// ==================== Touch ====================
#define DISP_TOUCH_ADDR                   0x3B

// ==================== RTC ====================
#define RTC_PCF85063_ADDR                 0x51

// ==================== IMU ====================
#define IMU_QMI8658_ADDR                  0x6B

// ==================== WiFi ====================
#define WIFI_SSID      "jack"
#define WIFI_PASSWORD  "4473358105"

// ==================== Display Settings ====================
#define LCD_PWM_MODE_255   255
#define LCD_PWM_MODE_175   175
#define LCD_PWM_MODE_125   125
#define LCD_PWM_MODE_0     0
#define LCD_PWM_MODE_DIM   30

// ==================== Screen Timeout ====================
#define SCREEN_DIM_TIMEOUT_MS    (60 * 1000)
#define SCREEN_OFF_TIMEOUT_MS    (180 * 1000)
#define SHAKE_WAKE_THRESHOLD     2.5f

// ==================== Schedule ====================
#define MAX_SCHEDULE_EVENTS  20

#endif
