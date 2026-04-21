set(MAIN_SRCS "main.c" "ui_main.c" "ui_clock.c" "ui_ble.c" "ui_styles.c" "app_ble.c" "ui_wifi_setup.c" "wifi_prov.c" "asr_client.c")

idf_component_register(SRCS ${MAIN_SRCS}
                    INCLUDE_DIRS "."
                    REQUIRES board_config i2c_bsp lcd_bl_pwm_bsp i2c_equipment audio_bsp wifi_bsp
                             cjson lvgl esp_driver_gpio esp_driver_spi bt esp_http_server nvs_flash
                             esp_websocket_client mbedtls
                    PRIV_REQUIRES esp_timer
                    EMBED_FILES "asset/connected.pcm" "asset/successful.pcm" "asset/wifi_setup.html"
                    EMBED_TXTFILES "asset/dashscope_ca.pem")
