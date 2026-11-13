// ddhw — hardware de pantalla para el firmware DataDruida (MicroPython + LVGL)
// Licencia Apache-2.0 — DataDruida
//
// Cada placa se describe con un perfil (dd_board_t). init() arma el panel con
// esp_lcd, el touch y el expansor, y registra un lv_display + lv_indev en el
// LVGL que compila el binding de MicroPython (el mismo que usa `import lvgl`).
//
// ⚠ I2C: MicroPython 1.24 usa el driver I2C "legacy" de ESP-IDF 5.2 y los dos
// drivers no pueden convivir. Acá se usa SOLO el legacy (driver/i2c.h).
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_lcd_panel_ops.h"

typedef enum {
    DD_BUS_RGB = 0,     // panel RGB paralelo de 16 bits (4,3" / 5" / 7")
    DD_BUS_SPI = 1,     // controlador SPI (ST7789)
} dd_bus_t;

typedef enum {
    DD_EXP_NONE = 0,
    DD_EXP_CH422G,      // 4,3 / 4,3B / 5 / 7 (800x480)
    DD_EXP_IOEXT,       // "IO_EXTENSION" de Waveshare, una sola dirección (7B)
} dd_expander_t;

typedef enum {
    DD_TP_NONE = 0,
    DD_TP_GT911,
    DD_TP_CST816,
} dd_touch_t;

typedef struct {
    const char *id;         // mismo id que el catálogo
    const char *name;
    dd_bus_t bus;
    uint16_t w, h;

    // RGB
    uint32_t pclk_hz;
    uint16_t hpw, hbp, hfp, vpw, vbp, vfp;
    int8_t de, hsync, vsync, pclk;
    int8_t data[16];

    // SPI
    int8_t sck, mosi, cs, dc, rst;
    uint8_t spi_host;
    uint32_t spi_hz;
    bool invert;

    // backlight por GPIO (-1 = por el expansor)
    int8_t bl;

    // I2C (touch + expansor)
    int8_t sda, scl;
    uint32_t i2c_hz;
    dd_expander_t expander;
    dd_touch_t touch;
    uint8_t touch_addr;
    int8_t touch_int;       // GT911: nivel del INT durante el reset fija la dirección
} dd_board_t;

const dd_board_t *dd_board_find(const char *id);
const dd_board_t *dd_board_list(int *count);

// i2c.c
esp_err_t dd_i2c_init(int sda, int scl, uint32_t hz);
esp_err_t dd_i2c_write(uint8_t addr, const uint8_t *data, size_t len);
esp_err_t dd_i2c_write_read(uint8_t addr, const uint8_t *w, size_t wl, uint8_t *r, size_t rl);

// expander.c
esp_err_t dd_exp_init(dd_expander_t kind);
esp_err_t dd_exp_out(dd_expander_t kind, int pin, int level);

// touch.c
esp_err_t dd_touch_init(const dd_board_t *b);
bool dd_touch_read(const dd_board_t *b, int16_t *x, int16_t *y);

// panel.c
esp_err_t dd_panel_init(const dd_board_t *b, esp_lcd_panel_handle_t *out);
void dd_backlight(const dd_board_t *b, int pct);
