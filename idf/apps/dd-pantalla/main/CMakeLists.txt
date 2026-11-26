# app.c = el programa del usuario (dd_setup / dd_loop). El servicio de
# compilación de /developers reemplaza sólo ese archivo.
idf_component_register(
    SRCS "main.c" "ui.c" "app.c" "fonts/dd_font_16.c" "fonts/dd_font_20.c" "fonts/dd_font_28.c"
    INCLUDE_DIRS "."
    REQUIRES dd_core dd_app rgb_lcd_port esp_wifi esp_timer
    # ⚠ WHOLE_ARCHIVE: sin esto el enlazador usa los dd_setup/dd_loop vacíos
    # (weak) de dd_app y descarta los de app.c.
    WHOLE_ARCHIVE
)
idf_build_get_property(build_components BUILD_COMPONENTS)
if("lvgl__lvgl" IN_LIST build_components)
    idf_component_get_property(lvgl_lib lvgl__lvgl COMPONENT_LIB)
    target_compile_options(${lvgl_lib} PRIVATE -Wno-format)
endif()
