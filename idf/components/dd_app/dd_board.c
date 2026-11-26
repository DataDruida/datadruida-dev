/*
 * dd_board.c — perfiles de placa, relés y entradas.
 *
 * La placa sale de dd_core (fija en los firmwares con pantalla, elegida en el
 * panel en dd-reles). Mientras no se elija no se toca ningún pin: en la
 * ETH-8DI-8RO los GPIO 41/42 son el bus I2C y en la 6CH son relés.
 */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"

#include "dd_app_priv.h"

static const char *TAG = "dd_board";

const dd_board_t DD_BOARDS[] = {
    { .id = "waveshare-esp32-s3-eth-8di-8ro", .name = "ESP32-S3-ETH-8DI-8RO (8 relés)",
      .relay_count = 8, .tca9554 = true, .sda = 42, .scl = 41,
      .input_count = 8, .input_pins = { 4, 5, 6, 7, 8, 9, 10, 11 },
      .rs485_tx = 17, .rs485_rx = 18, .rs485_de = -1 },
    { .id = "waveshare-esp32-s3-relay-6ch", .name = "ESP32-S3-Relay-6CH (6 relés)",
      .relay_count = 6, .relay_pins = { 1, 2, 41, 42, 45, 46 },
      .rs485_tx = 17, .rs485_rx = 18, .rs485_de = -1 },
    { .id = "waveshare-esp32-s3-relay-1ch", .name = "ESP32-S3-Relay-1CH (1 relé)",
      .relay_count = 1, .relay_pins = { 47 },
      .rs485_tx = 17, .rs485_rx = 18, .rs485_de = 21 },
    /* RS485 a bordo en 43/44 = los pines de la consola UART0: al abrir el bus
     * el log por ese puerto se corta (sigue en el panel web). Sin probar. */
    { .id = "waveshare-esp32-s3-touch-lcd-4.3b", .name = "ESP32-S3-Touch-LCD-4.3B",
      .relay_count = 0, .rs485_tx = 44, .rs485_rx = 43, .rs485_de = -1 },
};
const int DD_N_BOARDS = sizeof(DD_BOARDS) / sizeof(DD_BOARDS[0]);

static const dd_board_t *s_board;
static SemaphoreHandle_t s_mx;
static i2c_master_dev_handle_t s_tca;
static bool     s_relay[9];
static int64_t  s_on_since[9];     /* µs; 0 = apagado */
static uint32_t s_max_on[9];       /* s; 0 = sin tope */

const dd_board_t *dd_board(void) { return s_board; }

/* ── relés ───────────────────────────────────────────────────────────── */

static void tca_write(uint8_t reg, uint8_t val)
{
    uint8_t b[2] = { reg, val };
    if (s_tca) i2c_master_transmit(s_tca, b, 2, 100);
}

static void apply(void)
{
    if (!s_board) return;
    if (s_board->tca9554) {
        uint8_t mask = 0;
        for (int i = 0; i < s_board->relay_count; i++) if (s_relay[i + 1]) mask |= 1u << i;
        tca_write(0x01, mask);   /* salida */
        tca_write(0x03, 0x00);   /* todo salida: re-asienta si el chip se reseteó */
    } else {
        for (int i = 0; i < s_board->relay_count; i++) gpio_set_level(s_board->relay_pins[i], s_relay[i + 1]);
    }
}

int dd_relay_count(void) { return s_board ? s_board->relay_count : 0; }

void dd_relay_set(int ch, bool on)
{
    if (!s_board || ch < 1 || ch > s_board->relay_count) return;
    xSemaphoreTake(s_mx, portMAX_DELAY);
    if (on && !s_relay[ch]) s_on_since[ch] = esp_timer_get_time();
    if (!on) s_on_since[ch] = 0;
    s_relay[ch] = on;
    apply();
    xSemaphoreGive(s_mx);
}

bool dd_relay_get(int ch) { return s_board && ch >= 1 && ch <= s_board->relay_count && s_relay[ch]; }

void dd_relay_all_off(void)
{
    if (!s_mx) return;
    xSemaphoreTake(s_mx, portMAX_DELAY);
    memset(s_relay, 0, sizeof(s_relay));
    memset(s_on_since, 0, sizeof(s_on_since));
    apply();
    xSemaphoreGive(s_mx);
}

void dd_relay_max_on(int ch, uint32_t seconds) { if (ch >= 1 && ch <= 8) s_max_on[ch] = seconds; }

uint32_t dd_relay_on_for(int ch)
{
    if (ch < 1 || ch > 8 || !s_on_since[ch]) return 0;
    return (uint32_t)((esp_timer_get_time() - s_on_since[ch]) / 1000000);
}

void dd_relays_heal_and_limits(void)
{
    if (!s_board || !s_board->relay_count) return;
    for (int ch = 1; ch <= s_board->relay_count; ch++) {
        if (s_max_on[ch] && s_relay[ch] && dd_relay_on_for(ch) >= s_max_on[ch]) {
            dd_relay_set(ch, false);
            dd_log("relé %d apagado por tope de %u s", ch, (unsigned)s_max_on[ch]);
        }
    }
    if (s_board->tca9554) {
        /* Un transitorio puede resetear el TCA9554 y dejar los relés sueltos
         * mientras el programa cree que están prendidos (visto en el DruidaBot). */
        xSemaphoreTake(s_mx, portMAX_DELAY);
        apply();
        xSemaphoreGive(s_mx);
    }
}

/* ── entradas ────────────────────────────────────────────────────────── */

int dd_input_count(void) { return s_board ? s_board->input_count : 0; }

bool dd_input(int n)
{
    if (!s_board || n < 1 || n > s_board->input_count) return false;
    return gpio_get_level(s_board->input_pins[n - 1]) == 0;
}

/* ── arranque ────────────────────────────────────────────────────────── */

esp_err_t dd_board_init(void)
{
    s_mx = xSemaphoreCreateMutex();
    dd_status_t st;
    dd_status_get(&st);
    for (int i = 0; i < DD_N_BOARDS; i++) if (!strcmp(st.board_id, DD_BOARDS[i].id)) s_board = &DD_BOARDS[i];
    if (!s_board) {
        ESP_LOGW(TAG, "Placa sin elegir: los pines quedan sin tocar");
        return ESP_OK;
    }
    const dd_board_t *b = s_board;
    if (b->tca9554) {
        i2c_master_bus_config_t bc = {
            .i2c_port = I2C_NUM_0, .sda_io_num = b->sda, .scl_io_num = b->scl,
            .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7,
            .flags.enable_internal_pullup = true,
        };
        i2c_master_bus_handle_t bus;
        ESP_ERROR_CHECK(i2c_new_master_bus(&bc, &bus));
        if (i2c_master_probe(bus, 0x20, 100) != ESP_OK)
            ESP_LOGE(TAG, "El expansor de relés (TCA9554 en 0x20) no responde: ¿es otra placa?");
        i2c_device_config_t dc = { .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = 0x20, .scl_speed_hz = 100000 };
        ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &dc, &s_tca));
    } else {
        for (int i = 0; i < b->relay_count; i++) {
            gpio_reset_pin(b->relay_pins[i]);
            gpio_set_direction(b->relay_pins[i], GPIO_MODE_OUTPUT);
            gpio_set_level(b->relay_pins[i], 0);
        }
    }
    for (int i = 0; i < b->input_count; i++) {
        gpio_config_t io = { .pin_bit_mask = 1ULL << b->input_pins[i], .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE };
        gpio_config(&io);
    }
    dd_relay_all_off();
    ESP_LOGI(TAG, "Placa: %s", b->name);
    return ESP_OK;
}
