idf_component_register(SRCS "lcd_bl_pwm_bsp.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_driver_ledc board_config)
