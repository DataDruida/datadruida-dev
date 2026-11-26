idf_component_register(
    SRCS "dd_board.c" "dd_modbus.c" "dd_sensors.c" "dd_misc.c" "dd_runtime.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES dd_core
    PRIV_REQUIRES esp_driver_gpio esp_driver_i2c esp_driver_uart esp_timer esp_netif json lwip
)
