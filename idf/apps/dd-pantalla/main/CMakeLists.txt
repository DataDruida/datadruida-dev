idf_component_register(
    SRCS "main.c" "ui.c" "fonts/dd_font_16.c" "fonts/dd_font_20.c" "fonts/dd_font_28.c"
    INCLUDE_DIRS "."
    REQUIRES dd_core rgb_lcd_port esp_wifi esp_timer
)
idf_build_get_property(build_components BUILD_COMPONENTS)
if("lvgl__lvgl" IN_LIST build_components)
    idf_component_get_property(lvgl_lib lvgl__lvgl COMPONENT_LIB)
    target_compile_options(${lvgl_lib} PRIVATE -Wno-format)
endif()
