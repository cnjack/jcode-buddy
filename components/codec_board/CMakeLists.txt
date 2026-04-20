set(COMPONENT_PRIV_REQUIRE driver esp_codec_dev)

if("${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR}" VERSION_GREATER_EQUAL "5.3")
list(APPEND COMPONENT_PRIV_REQUIRE esp_driver_i2c)
endif()

idf_component_register(SRCS  "cfg_parse.c" "codec_board.c" "codec_init.c" "dummy_codec.c"
                       INCLUDE_DIRS ./include
                       REQUIRES "${COMPONENT_PRIV_REQUIRE}"
                       EMBED_TXTFILES "board_cfg.txt")
