#ifndef LCD_BL_PWM_BSP_H
#define LCD_BL_PWM_BSP_H

#include <stdint.h>

void lcd_bl_pwm_bsp_init(uint16_t duty);
void set_backlight_duty(uint16_t duty);

#endif
