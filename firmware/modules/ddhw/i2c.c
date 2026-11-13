// ddhw/i2c.c — bus I2C del touch y el expansor (driver legacy de ESP-IDF)
// Licencia Apache-2.0 — DataDruida
//
// Usa I2C_NUM_1 para no pisar machine.I2C(0) del programa del usuario.
// ⚠ Si el usuario abre machine.I2C(1) con los mismos pines, rompe el touch.

#include "driver/i2c.h"
#include "ddhw.h"

#define DD_I2C_PORT I2C_NUM_1
#define DD_I2C_TIMEOUT pdMS_TO_TICKS(50)

static bool s_ready = false;

esp_err_t dd_i2c_init(int sda, int scl, uint32_t hz) {
    if (s_ready) {
        return ESP_OK;
    }
    i2c_config_t c = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = sda,
        .scl_io_num = scl,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = hz,
    };
    esp_err_t e = i2c_param_config(DD_I2C_PORT, &c);
    if (e == ESP_OK) {
        e = i2c_driver_install(DD_I2C_PORT, I2C_MODE_MASTER, 0, 0, 0);
    }
    s_ready = (e == ESP_OK);
    return e;
}

esp_err_t dd_i2c_write(uint8_t addr, const uint8_t *data, size_t len) {
    return i2c_master_write_to_device(DD_I2C_PORT, addr, data, len, DD_I2C_TIMEOUT);
}

esp_err_t dd_i2c_write_read(uint8_t addr, const uint8_t *w, size_t wl, uint8_t *r, size_t rl) {
    return i2c_master_write_read_device(DD_I2C_PORT, addr, w, wl, r, rl, DD_I2C_TIMEOUT);
}
