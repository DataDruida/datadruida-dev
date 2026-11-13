# DataDruida — ESP32-S3 (16 MB flash, PSRAM octal) con LVGL + ddhw
# Una sola imagen para todas las placas con pantalla: la placa se elige en
# tiempo de ejecución (ddhw.init(id), que lee dd.board).
set(IDF_TARGET esp32s3)

set(SDKCONFIG_DEFAULTS
    boards/sdkconfig.base
    ${SDKCONFIG_IDF_VERSION_SPECIFIC}
    boards/sdkconfig.usb
    boards/sdkconfig.ble
    boards/sdkconfig.spiram_sx
    boards/sdkconfig.240mhz
    boards/sdkconfig.spiram_oct
    ${MICROPY_BOARD_DIR}/sdkconfig.board
)

list(APPEND MICROPY_DEF_BOARD
    MICROPY_HW_BOARD_NAME="DataDruida ESP32-S3 LVGL"
)

set(MICROPY_FROZEN_MANIFEST ${MICROPY_BOARD_DIR}/manifest.py)
