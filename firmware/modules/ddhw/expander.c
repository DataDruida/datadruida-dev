// ddhw/expander.c — expansores de E/S de las pantallas RGB
// Licencia Apache-2.0 — DataDruida
//
// CH422G (4,3 / 4,3B / 5 / 7 de 800x480): cada "registro" es una dirección I2C
//   0x24 modo · 0x38 salidas IO0..IO7. IO1 = reset del touch, IO2 = backlight
//   (sólo on/off), IO3 = reset del LCD. Arranque 0x2C: backlight y LCD ON, touch
//   EN RESET (se suelta en dd_touch_init, con el INT en bajo -> GT911 en 0x5D).
// IO_EXTENSION (7B): una sola dirección 0x24 con registros [reg, dato]:
//   0x02 modo · 0x03 salidas · 0x05 PWM. Mismos números de pin.

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ddhw.h"

#define CH422G_MODE   0x24
#define CH422G_IO_OUT 0x38
#define CH422G_IO_OE  0x01
#define IOEXT_ADDR    0x24
#define IOEXT_MODE    0x02
#define IOEXT_OUT     0x03

static uint8_t s_shadow = 0x2C;

esp_err_t dd_exp_init(dd_expander_t kind) {
    esp_err_t e = ESP_OK;
    if (kind == DD_EXP_CH422G) {
        uint8_t m = CH422G_IO_OE;
        e = dd_i2c_write(CH422G_MODE, &m, 1);
        s_shadow = 0x2C;
        if (e == ESP_OK) {
            e = dd_i2c_write(CH422G_IO_OUT, &s_shadow, 1);
        }
    } else if (kind == DD_EXP_IOEXT) {
        uint8_t mode[2] = { IOEXT_MODE, 0xFF };     // todos salida
        e = dd_i2c_write(IOEXT_ADDR, mode, 2);
        s_shadow = 0x2C;
        uint8_t out[2] = { IOEXT_OUT, s_shadow };
        if (e == ESP_OK) {
            e = dd_i2c_write(IOEXT_ADDR, out, 2);
        }
    }
    return e;
}

esp_err_t dd_exp_out(dd_expander_t kind, int pin, int level) {
    if (level) {
        s_shadow |= (uint8_t)(1 << pin);
    } else {
        s_shadow &= (uint8_t)~(1 << pin);
    }
    if (kind == DD_EXP_CH422G) {
        return dd_i2c_write(CH422G_IO_OUT, &s_shadow, 1);
    }
    if (kind == DD_EXP_IOEXT) {
        uint8_t out[2] = { IOEXT_OUT, s_shadow };
        return dd_i2c_write(IOEXT_ADDR, out, 2);
    }
    return ESP_OK;
}
