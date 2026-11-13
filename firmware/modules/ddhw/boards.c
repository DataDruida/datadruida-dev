// ddhw/boards.c — perfiles de las placas con pantalla
// Licencia Apache-2.0 — DataDruida
//
// Pines y timings tomados de firmwares propios que ya andan en estas placas:
//  - 800x480 (4,3 / 4,3B / 5 / 7):  DataBot (Arduino) y DataBot-LCD4.3 (ESP-IDF),
//    ejemplo oficial 09_lvgl_v9_demo de la 4.3B: 16 MHz, porches 4/8/8.
//  - 7B (1024x600):                 DataBot-LCD7, ejemplo oficial 13_lvgl_v8_demo.
//  - Touch-LCD-2 (240x320 SPI):     DruidaVision2.
// Si la imagen sale corrida o con franjas, lo primero es el pclk y los porches,
// siempre contra el ejemplo oficial de la placa (no promediar entre placas).

#include <string.h>
#include "ddhw.h"

// Bus RGB común a toda la familia ESP32-S3-Touch-LCD (B3..B7, G2..G7, R3..R7)
#define WS_RGB_DATA { 14, 38, 18, 17, 10, 39, 0, 45, 48, 47, 21, 1, 2, 42, 41, 40 }

static const dd_board_t BOARDS[] = {
    {
        .id = "waveshare-esp32-s3-touch-lcd-7b", .name = "Waveshare ESP32-S3-Touch-LCD-7B",
        .bus = DD_BUS_RGB, .w = 1024, .h = 600,
        .pclk_hz = 30850000, .hpw = 162, .hbp = 152, .hfp = 48, .vpw = 45, .vbp = 13, .vfp = 3,
        .de = 5, .hsync = 46, .vsync = 3, .pclk = 7, .data = WS_RGB_DATA,
        .bl = -1, .sda = 8, .scl = 9, .i2c_hz = 400000,
        .expander = DD_EXP_IOEXT, .touch = DD_TP_GT911, .touch_addr = 0x5D, .touch_int = 4,
    },
    {
        .id = "waveshare-esp32-s3-touch-lcd-800x480", .name = "Waveshare ESP32-S3-Touch-LCD 4,3 / 5 / 7 (800x480)",
        .bus = DD_BUS_RGB, .w = 800, .h = 480,
        .pclk_hz = 16000000, .hpw = 4, .hbp = 8, .hfp = 8, .vpw = 4, .vbp = 8, .vfp = 8,
        .de = 5, .hsync = 46, .vsync = 3, .pclk = 7, .data = WS_RGB_DATA,
        .bl = -1, .sda = 8, .scl = 9, .i2c_hz = 400000,
        .expander = DD_EXP_CH422G, .touch = DD_TP_GT911, .touch_addr = 0x5D, .touch_int = 4,
    },
    {
        .id = "waveshare-esp32-s3-touch-lcd-2", .name = "Waveshare ESP32-S3-Touch-LCD-2",
        .bus = DD_BUS_SPI, .w = 240, .h = 320,
        .sck = 39, .mosi = 38, .cs = 45, .dc = 42, .rst = -1, .spi_host = 1 /* SPI2_HOST */,
        .spi_hz = 40000000, .invert = true,
        .bl = 1, .sda = 48, .scl = 47, .i2c_hz = 400000,
        .expander = DD_EXP_NONE, .touch = DD_TP_CST816, .touch_addr = 0x15, .touch_int = -1,
    },
};

const dd_board_t *dd_board_find(const char *id) {
    for (size_t i = 0; i < sizeof(BOARDS) / sizeof(BOARDS[0]); i++) {
        if (strcmp(BOARDS[i].id, id) == 0) {
            return &BOARDS[i];
        }
    }
    return NULL;
}

const dd_board_t *dd_board_list(int *count) {
    *count = sizeof(BOARDS) / sizeof(BOARDS[0]);
    return BOARDS;
}
