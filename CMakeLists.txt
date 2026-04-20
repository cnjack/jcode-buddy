# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# ESP-IDF version check
set(IDF_VERSION_CHECK 1)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(time)

# Workaround for LVGL IRAM_ATTR section attribute conflicts
idf_build_get_property(build_components BUILD_COMPONENTS)
if("lvgl__lvgl" IN_LIST build_components OR "lvgl" IN_LIST build_components)
    idf_component_get_property(lvgl_lib lvgl__lvgl COMPONENT_LIB)
    if(lvgl_lib)
        target_compile_options(${lvgl_lib} PRIVATE -Wno-error=attributes)
    endif()
endif()
