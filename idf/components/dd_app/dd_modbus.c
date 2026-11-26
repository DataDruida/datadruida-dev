/*
 * dd_modbus.c — Modbus RTU maestro por RS485 (4800 8N1, como todo el catálogo).
 * El bus se abre la primera vez que se usa: en la 4.3B comparte pines con la
 * consola y no conviene tomarlos si el programa no tiene sensores.
 */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/uart.h"

#include "dd_app_priv.h"

static const char *TAG = "dd_modbus";
#define DD_UART   UART_NUM_1
#define DD_BAUD   4800
#define DD_TIMEOUT_MS 300

static SemaphoreHandle_t s_mx;
static bool s_open, s_failed;
static int64_t s_last_us;

static uint16_t crc16(const uint8_t *d, int n)
{
    uint16_t c = 0xFFFF;
    for (int i = 0; i < n; i++) {
        c ^= d[i];
        for (int b = 0; b < 8; b++) c = (c & 1) ? (c >> 1) ^ 0xA001 : c >> 1;
    }
    return c;
}

static bool bus_open(void)
{
    if (s_open) return true;
    if (s_failed) return false;
    const dd_board_t *b = dd_board();
    if (!b || b->rs485_tx < 0) { s_failed = true; ESP_LOGW(TAG, "Esta placa no tiene RS485"); return false; }
    uart_config_t uc = {
        .baud_rate = DD_BAUD, .data_bits = UART_DATA_8_BITS, .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1, .flow_ctrl = UART_HW_FLOWCTRL_DISABLE, .source_clk = UART_SCLK_DEFAULT,
    };
    if (uart_driver_install(DD_UART, 512, 0, 0, NULL, 0) != ESP_OK ||
        uart_param_config(DD_UART, &uc) != ESP_OK ||
        uart_set_pin(DD_UART, b->rs485_tx, b->rs485_rx, b->rs485_de, UART_PIN_NO_CHANGE) != ESP_OK) {
        s_failed = true;
        ESP_LOGE(TAG, "No pude abrir el RS485");
        return false;
    }
    /* Con pin DE (Relay-1CH) el UART maneja la dirección solo (RTS = DE). */
    if (b->rs485_de >= 0) uart_set_mode(DD_UART, UART_MODE_RS485_HALF_DUPLEX);
    s_open = true;
    ESP_LOGI(TAG, "RS485 abierto (TX %d / RX %d, %d baud)", b->rs485_tx, b->rs485_rx, DD_BAUD);
    return true;
}

/* Manda req y espera `expect` bytes. Devuelve los bytes recibidos (o -1). */
static int xfer(const uint8_t *req, int len, uint8_t *resp, int expect)
{
    /* silencio de 3,5 caracteres entre tramas (~8 ms a 4800) */
    int64_t gap = 10000 - (esp_timer_get_time() - s_last_us);
    if (gap > 0) vTaskDelay(pdMS_TO_TICKS(gap / 1000 + 1));
    uart_flush_input(DD_UART);
    uart_write_bytes(DD_UART, req, len);
    uart_wait_tx_done(DD_UART, pdMS_TO_TICKS(200));
    int got = 0;
    const int64_t t0 = esp_timer_get_time();
    while (got < expect && (esp_timer_get_time() - t0) < DD_TIMEOUT_MS * 1000) {
        int r = uart_read_bytes(DD_UART, resp + got, expect - got, pdMS_TO_TICKS(20));
        if (r > 0) got += r;
        if (got >= 5 && (resp[1] & 0x80)) { got = 5; break; }   /* respuesta de excepción */
    }
    s_last_us = esp_timer_get_time();
    return got;
}

bool dd_modbus_read(int addr, int reg, int count, int fc, int16_t *out)
{
    if (count < 1 || count > 32 || (fc != 3 && fc != 4)) return false;
    if (!s_mx) s_mx = xSemaphoreCreateMutex();
    xSemaphoreTake(s_mx, portMAX_DELAY);
    bool ok = false;
    if (bus_open()) {
        uint8_t req[8] = { addr, fc, reg >> 8, reg & 0xFF, count >> 8, count & 0xFF };
        uint16_t c = crc16(req, 6);
        req[6] = c & 0xFF; req[7] = c >> 8;
        uint8_t resp[5 + 64];
        int expect = 5 + 2 * count;
        int n = xfer(req, 8, resp, expect);
        if (n == expect && resp[0] == addr && resp[1] == fc && resp[2] == 2 * count) {
            uint16_t rc = crc16(resp, n - 2);
            if ((resp[n - 2] | (resp[n - 1] << 8)) == rc) {
                for (int i = 0; i < count; i++) out[i] = (int16_t)((resp[3 + 2 * i] << 8) | resp[4 + 2 * i]);
                ok = true;
            }
        }
    }
    xSemaphoreGive(s_mx);
    return ok;
}

bool dd_modbus_write(int addr, int reg, uint16_t value)
{
    if (!s_mx) s_mx = xSemaphoreCreateMutex();
    xSemaphoreTake(s_mx, portMAX_DELAY);
    bool ok = false;
    if (bus_open()) {
        uint8_t req[8] = { addr, 6, reg >> 8, reg & 0xFF, value >> 8, value & 0xFF };
        uint16_t c = crc16(req, 6);
        req[6] = c & 0xFF; req[7] = c >> 8;
        uint8_t resp[8];
        ok = xfer(req, 8, resp, 8) == 8 && memcmp(req, resp, 8) == 0;
    }
    xSemaphoreGive(s_mx);
    return ok;
}
