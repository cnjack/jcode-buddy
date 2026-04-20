idf_component_register(SRCS "wifi_bsp.c"
                    INCLUDE_DIRS "."
                    REQUIRES board_config esp_wifi esp_netif esp_event nvs_flash lwip log)
