// ddhw/touch.c — touch GT911 y CST816 por polling (sin interrupción)
// Licencia Apache-2.0 — DataDruida

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ddhw.h"

esp_err_t dd_touch_init(const dd_board_t *b) {
    if (b->touch == DD_TP_GT911 && b->expander != DD_EXP_NONE) {
        // Secuencia de reset de los ejemplos de Waveshare: INT en bajo mientras
        // se suelta el reset -> el GT911 queda en 0x5D.
        gpio_reset_pin(b->touch_int);
        gpio_set_direction(b->touch_int, GPIO_MODE_OUTPUT);
        dd_exp_out(b->expander, 1, 0);
        vTaskDelay(pdMS_TO_TICKS(100));
        gpio_set_level(b->touch_int, 0);
        vTaskDelay(pdMS_TO_TICKS(100));
        dd_exp_out(b->expander, 1, 1);
        vTaskDelay(pdMS_TO_TICKS(200));
        // en polling el INT no se usa: se suelta para no pelearle al GT911
        gpio_set_direction(b->touch_int, GPIO_MODE_INPUT);
    }
    return ESP_OK;
}

// Lee el GT911 si tiene dato nuevo. Devuelve true si actualizó *down/x/y.
static bool gt911_poll(uint8_t addr, bool *down, int16_t *x, int16_t *y) {
    uint8_t reg[2] = { 0x81, 0x4E };
    uint8_t st = 0;
    if (dd_i2c_write_read(addr, reg, 2, &st, 1) != ESP_OK || !(st & 0x80)) {
        return false;                       // sin dato nuevo
    }
    *down = false;
    if ((st & 0x0F) > 0) {
        uint8_t preg[2] = { 0x81, 0x50 };
        uint8_t p[4];
        if (dd_i2c_write_read(addr, preg, 2, p, 4) == ESP_OK) {
            *x = (int16_t)(p[0] | (p[1] << 8));
            *y = (int16_t)(p[2] | (p[3] << 8));
            *down = true;
        }
    }
    uint8_t clr[3] = { 0x81, 0x4E, 0x00 };  // "leído": el GT911 prepara el próximo
    dd_i2c_write(addr, clr, 3);
    return true;
}

static bool cst816_read(uint8_t addr, int16_t *x, int16_t *y) {
    uint8_t reg = 0x02;
    uint8_t d[5];
    if (dd_i2c_write_read(addr, &reg, 1, d, 5) != ESP_OK) {
        return false;
    }
    if (!(d[0] & 0x0F)) {
        return false;
    }
    *x = (int16_t)(((d[1] & 0x0F) << 8) | d[2]);
    *y = (int16_t)(((d[3] & 0x0F) << 8) | d[4]);
    return true;
}

// Devuelve true mientras hay un dedo. El GT911 sólo avisa cuando hay dato
// nuevo: se recuerda el último estado para no "soltar" entre muestras.
bool dd_touch_read(const dd_board_t *b, int16_t *x, int16_t *y) {
    static bool last = false;
    static int16_t lx = 0, ly = 0;
    if (b->touch == DD_TP_CST816) {
        return cst816_read(b->touch_addr, x, y);
    }
    if (b->touch == DD_TP_GT911) {
        gt911_poll(b->touch_addr, &last, &lx, &ly);
        *x = lx;
        *y = ly;
        return last;
    }
    return false;
}
