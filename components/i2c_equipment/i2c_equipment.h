#ifndef I2C_EQUIPMENT_H
#define I2C_EQUIPMENT_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t week;
} rtc_datetime_t;

typedef struct {
    float acc_x, acc_y, acc_z;
    float gyro_x, gyro_y, gyro_z;
} imu_data_t;

void i2c_rtc_setup(void);
void i2c_rtc_set_time(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second);
rtc_datetime_t i2c_rtc_get(void);

void i2c_qmi_setup(void);
imu_data_t i2c_imu_get(void);
bool i2c_imu_shake_detected(float threshold);

#ifdef __cplusplus
}
#endif

#endif
