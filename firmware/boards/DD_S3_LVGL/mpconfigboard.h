// DataDruida — ESP32-S3 con pantalla (LVGL)
#ifndef MICROPY_HW_BOARD_NAME
#define MICROPY_HW_BOARD_NAME               "DataDruida ESP32-S3 LVGL"
#endif
#define MICROPY_HW_MCU_NAME                 "ESP32S3"

// REPL sólo por el USB nativo (Web Serial de la página)
#define MICROPY_HW_ENABLE_UART_REPL         (0)   // 43/44 son el RS485 de estas placas
