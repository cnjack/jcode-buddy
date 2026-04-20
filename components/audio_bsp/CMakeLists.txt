idf_component_register(SRCS "audio_bsp.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_codec_dev codec_board esp_driver_i2c)
