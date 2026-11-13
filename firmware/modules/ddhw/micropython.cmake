# ddhw — módulo C de pantallas para el firmware DataDruida
add_library(usermod_ddhw INTERFACE)

target_sources(usermod_ddhw INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/modddhw.c
    ${CMAKE_CURRENT_LIST_DIR}/boards.c
    ${CMAKE_CURRENT_LIST_DIR}/i2c.c
    ${CMAKE_CURRENT_LIST_DIR}/expander.c
    ${CMAKE_CURRENT_LIST_DIR}/touch.c
    ${CMAKE_CURRENT_LIST_DIR}/panel.c
)

target_include_directories(usermod_ddhw INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}
)

# El componente principal de MicroPython no pide esp_lcd en su REQUIRES: se
# enlaza acá (el target se resuelve al generar, aunque se defina después).
if(ESP_PLATFORM)
    target_link_libraries(usermod_ddhw INTERFACE idf::esp_lcd)
endif()

target_link_libraries(usermod INTERFACE usermod_ddhw)
