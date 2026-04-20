#include <stdio.h>
#include <cstring>
#include "i2c_equipment.h"
#include "i2c_bsp.h"
#include "user_config.h"
#include "SensorPCF85063.hpp"
#include "SensorQMI8658.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_timer.h"

static const char *TAG = "i2c_equip";

static SensorPCF85063 rtc;
static SensorQMI8658 qmi;
static IMUdata acc_data, gyr_data;

static uint32_t hal_callback(SensorCommCustomHal::Operation op, void *param1, void *param2)
{
    switch (op) {
    case SensorCommCustomHal::OP_PINMODE: {
        uint8_t pin = reinterpret_cast<uintptr_t>(param1);
        uint8_t mode = reinterpret_cast<uintptr_t>(param2);
        gpio_config_t config = {};
        config.pin_bit_mask = 1ULL << pin;
        config.mode = (mode == 1) ? GPIO_MODE_OUTPUT : GPIO_MODE_INPUT;
        config.pull_up_en = GPIO_PULLUP_DISABLE;
        config.pull_down_en = GPIO_PULLDOWN_DISABLE;
        config.intr_type = GPIO_INTR_DISABLE;
        gpio_config(&config);
    } break;
    case SensorCommCustomHal::OP_DIGITALWRITE: {
        uint8_t pin = reinterpret_cast<uintptr_t>(param1);
        uint8_t level = reinterpret_cast<uintptr_t>(param2);
        gpio_set_level((gpio_num_t)pin, level);
    } break;
    case SensorCommCustomHal::OP_DIGITALREAD: {
        uint8_t pin = reinterpret_cast<uintptr_t>(param1);
        return gpio_get_level((gpio_num_t)pin);
    } break;
    case SensorCommCustomHal::OP_MILLIS:
        return (uint32_t)(esp_timer_get_time() / 1000LL);
    case SensorCommCustomHal::OP_DELAY: {
        if (param1) {
            uint32_t ms = reinterpret_cast<uintptr_t>(param1);
            vTaskDelay(pdMS_TO_TICKS(ms));
        }
    } break;
    case SensorCommCustomHal::OP_DELAYMICROSECONDS: {
        uint32_t us = reinterpret_cast<uintptr_t>(param1);
        esp_rom_delay_us(us);
    } break;
    default:
        break;
    }
    return 0;
}

static bool i2c_dev_callback(uint8_t addr, uint8_t reg, uint8_t *buf, size_t len, bool writeReg, bool isWrite)
{
    i2c_master_dev_handle_t dev = NULL;
    if (RTC_PCF85063_ADDR == addr)
        dev = rtc_dev_handle;
    else if (IMU_QMI8658_ADDR == addr)
        dev = imu_dev_handle;
    else
        return false;

    uint8_t ret;
    int areg = reg;
    if (isWrite) {
        ret = writeReg ? i2c_write_buff(dev, areg, buf, len)
                       : i2c_write_buff(dev, -1, buf, len);
    } else {
        ret = writeReg ? i2c_read_buff(dev, areg, buf, len)
                       : i2c_read_buff(dev, -1, buf, len);
    }
    return (ret == ESP_OK);
}

void i2c_rtc_setup(void)
{
    if (rtc.begin(i2c_dev_callback)) {
        ESP_LOGI(TAG, "PCF85063 RTC initialized");
    } else {
        ESP_LOGE(TAG, "PCF85063 RTC init failed");
    }
}

void i2c_rtc_set_time(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second)
{
    rtc.setDateTime(year, month, day, hour, minute, second);
    ESP_LOGI(TAG, "RTC time set: %d/%d/%d %d:%d:%d", year, month, day, hour, minute, second);
}

rtc_datetime_t i2c_rtc_get(void)
{
    rtc_datetime_t t = {};
    RTC_DateTime dt = rtc.getDateTime();
    t.year = dt.getYear();
    t.month = dt.getMonth();
    t.day = dt.getDay();
    t.hour = dt.getHour();
    t.minute = dt.getMinute();
    t.second = dt.getSecond();
    t.week = dt.getWeek();
    return t;
}

void i2c_qmi_setup(void)
{
    if (qmi.begin(COMM_I2C, i2c_dev_callback, hal_callback, IMU_QMI8658_ADDR)) {
        ESP_LOGI(TAG, "QMI8658 IMU initialized, ID: %02x", qmi.getChipID());
    } else {
        ESP_LOGE(TAG, "QMI8658 IMU init failed");
        return;
    }

    qmi.configAccelerometer(SensorQMI8658::ACC_RANGE_4G, SensorQMI8658::ACC_ODR_1000Hz, SensorQMI8658::LPF_MODE_0);
    qmi.configGyroscope(SensorQMI8658::GYR_RANGE_64DPS, SensorQMI8658::GYR_ODR_896_8Hz, SensorQMI8658::LPF_MODE_3);
    qmi.enableAccelerometer();
    qmi.enableGyroscope();
}

imu_data_t i2c_imu_get(void)
{
    imu_data_t data = {};
    if (qmi.getDataReady()) {
        qmi.getAccelerometer(acc_data.x, acc_data.y, acc_data.z);
        data.acc_x = acc_data.x;
        data.acc_y = acc_data.y;
        data.acc_z = acc_data.z;
        qmi.getGyroscope(gyr_data.x, gyr_data.y, gyr_data.z);
        data.gyro_x = gyr_data.x;
        data.gyro_y = gyr_data.y;
        data.gyro_z = gyr_data.z;
    }
    return data;
}

bool i2c_imu_shake_detected(float threshold)
{
    imu_data_t data = i2c_imu_get();
    float magnitude = data.acc_x * data.acc_x + data.acc_y * data.acc_y + data.acc_z * data.acc_z;
    static float last_magnitude = 1.0f;
    float delta = magnitude - last_magnitude;
    if (delta < 0) delta = -delta;
    last_magnitude = magnitude;
    return delta > threshold;
}
