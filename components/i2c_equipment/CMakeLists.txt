idf_component_register(SRCS "i2c_equipment.cpp"
                    INCLUDE_DIRS "."
                    REQUIRES i2c_bsp driver board_config)
