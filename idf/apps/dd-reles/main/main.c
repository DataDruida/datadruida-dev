/*
 * dd-reles — firmware de prueba DataDruida para las placas Waveshare SIN
 * pantalla (ESP32-S3-ETH-8DI-8RO, ESP32-S3-Relay-6CH, ESP32-S3-Relay-1CH).
 *
 * Un solo binario para las tres: la placa se elige una vez en el panel
 * (queda en NVS). Mientras no se elija, no se toca ningún pin: en la
 * ETH-8DI-8RO los GPIO 41/42 son el bus I2C y en la 6CH son relés, así que
 * manejarlos a ciegas podría pelear con el hardware.
 *
 * Qué hace:
 *   - Placa nueva: abre la red "DataDruida-XXXX"; desde el celular se elige
 *     el WiFi de la casa (portal cautivo).
 *   - Ya en la red: panel en http://dd-xxxxxx.local para probar relés y
 *     entradas, y actualizar por OTA (nube o .bin local).
 *   - Mantener BOOT apretado 3 s al encender = modo seguro (reabre el portal).
 */
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_system.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "cJSON.h"

#include "dd_core.h"

static const char *TAG = "dd-reles";

typedef struct {
    const char *id;
    const char *name;
    int  relay_count;
    bool tca9554;               // relés por expansor I2C (si no, GPIO directo)
    int  sda, scl;              // bus I2C del expansor
    int  relay_pins[8];
    int  input_count;
    int  input_pins[8];         // contacto seco contra GND: 0 = activo
} board_t;

static const board_t BOARDS[] = {
    { .id = "waveshare-esp32-s3-eth-8di-8ro", .name = "ESP32-S3-ETH-8DI-8RO (8 relés)",
      .relay_count = 8, .tca9554 = true, .sda = 42, .scl = 41,
      .input_count = 8, .input_pins = { 4, 5, 6, 7, 8, 9, 10, 11 } },
    { .id = "waveshare-esp32-s3-relay-6ch", .name = "ESP32-S3-Relay-6CH (6 relés)",
      .relay_count = 6, .relay_pins = { 1, 2, 41, 42, 45, 46 } },
    { .id = "waveshare-esp32-s3-relay-1ch", .name = "ESP32-S3-Relay-1CH (1 relé)",
      .relay_count = 1, .relay_pins = { 47 } },
};
#define N_BOARDS (sizeof(BOARDS) / sizeof(BOARDS[0]))

static const board_t *s_board;
static bool s_relay[8];
static SemaphoreHandle_t s_mx;
static i2c_master_dev_handle_t s_tca;
static volatile bool s_testing;

/* ── relés ───────────────────────────────────────────────────────────── */

static void tca_write(uint8_t reg, uint8_t val)
{
    uint8_t b[2] = { reg, val };
    if (s_tca) i2c_master_transmit(s_tca, b, 2, 100);
}

static void relays_apply(void)
{
    if (!s_board) return;
    if (s_board->tca9554) {
        uint8_t mask = 0;
        for (int i = 0; i < s_board->relay_count; i++) if (s_relay[i]) mask |= 1u << i;
        tca_write(0x01, mask);   // registro de salida
        tca_write(0x03, 0x00);   // todo salida (re-asienta si el chip se reseteó)
    } else {
        for (int i = 0; i < s_board->relay_count; i++) gpio_set_level(s_board->relay_pins[i], s_relay[i]);
    }
}

static void relay_set(int ch, bool on)
{
    if (!s_board || ch < 1 || ch > s_board->relay_count) return;
    xSemaphoreTake(s_mx, portMAX_DELAY);
    s_relay[ch - 1] = on;
    relays_apply();
    xSemaphoreGive(s_mx);
}

static void relays_all_off(void)
{
    xSemaphoreTake(s_mx, portMAX_DELAY);
    memset(s_relay, 0, sizeof(s_relay));
    relays_apply();
    xSemaphoreGive(s_mx);
}

static esp_err_t board_init(const board_t *b)
{
    if (b->tca9554) {
        i2c_master_bus_config_t bc = {
            .i2c_port = I2C_NUM_0, .sda_io_num = b->sda, .scl_io_num = b->scl,
            .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7,
            .flags.enable_internal_pullup = true,
        };
        i2c_master_bus_handle_t bus;
        ESP_ERROR_CHECK(i2c_new_master_bus(&bc, &bus));
        if (i2c_master_probe(bus, 0x20, 100) != ESP_OK) {
            ESP_LOGE(TAG, "El expansor de relés (TCA9554 en 0x20) no responde: ¿es otra placa?");
        }
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
    s_board = b;
    relays_all_off();
    ESP_LOGI(TAG, "Placa: %s", b->name);
    return ESP_OK;
}

static void test_task(void *arg)
{
    (void)arg;
    relays_all_off();
    for (int i = 1; i <= s_board->relay_count && s_testing; i++) {
        relay_set(i, true);
        vTaskDelay(pdMS_TO_TICKS(500));
        relay_set(i, false);
        vTaskDelay(pdMS_TO_TICKS(150));
    }
    s_testing = false;
    vTaskDelete(NULL);
}

/* ── panel: rutas y estado extra ─────────────────────────────────────── */

static esp_err_t reply_ok(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

static esp_err_t reply_err(httpd_req_t *req, const char *msg)
{
    httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_set_type(req, "application/json");
    char b[160];
    snprintf(b, sizeof(b), "{\"error\":\"%s\"}", msg);
    return httpd_resp_sendstr(req, b);
}

static esp_err_t h_relay(httpd_req_t *req)
{
    if (dd_web_auth(req) != ESP_OK) return ESP_OK;
    if (!s_board) return reply_err(req, "Primero elegí la placa");
    char body[96];
    if (dd_web_read_body(req, body, sizeof(body)) < 0) return reply_err(req, "Body inválido");
    cJSON *j = cJSON_Parse(body);
    const cJSON *ch = cJSON_GetObjectItem(j, "ch"), *on = cJSON_GetObjectItem(j, "on");
    bool ok = cJSON_IsNumber(ch) && cJSON_IsBool(on);
    if (ok) { s_testing = false; relay_set(ch->valueint, cJSON_IsTrue(on)); }
    cJSON_Delete(j);
    return ok ? reply_ok(req) : reply_err(req, "Faltan ch / on");
}

static esp_err_t h_test(httpd_req_t *req)
{
    if (dd_web_auth(req) != ESP_OK) return ESP_OK;
    if (!s_board) return reply_err(req, "Primero elegí la placa");
    if (!s_testing) { s_testing = true; xTaskCreate(test_task, "relay_test", 3072, NULL, 3, NULL); }
    return reply_ok(req);
}

static esp_err_t h_off(httpd_req_t *req)
{
    if (dd_web_auth(req) != ESP_OK) return ESP_OK;
    s_testing = false;
    if (s_board) relays_all_off();
    return reply_ok(req);
}

static void restart_task(void *arg) { (void)arg; vTaskDelay(pdMS_TO_TICKS(1000)); esp_restart(); }

static esp_err_t h_board(httpd_req_t *req)
{
    if (dd_web_auth(req) != ESP_OK) return ESP_OK;
    char body[128];
    if (dd_web_read_body(req, body, sizeof(body)) < 0) return reply_err(req, "Body inválido");
    cJSON *j = cJSON_Parse(body);
    const cJSON *b = cJSON_GetObjectItem(j, "board");
    bool ok = false;
    for (size_t i = 0; cJSON_IsString(b) && i < N_BOARDS; i++) {
        if (!strcmp(b->valuestring, BOARDS[i].id)) { ok = dd_board_set(BOARDS[i].id) == ESP_OK; break; }
    }
    cJSON_Delete(j);
    if (!ok) return reply_err(req, "Placa desconocida");
    // Reinicio: los pines se configuran limpios desde el arranque.
    xTaskCreate(restart_task, "rst", 2048, NULL, 5, NULL);
    return reply_ok(req);
}

static void register_routes(httpd_handle_t s)
{
    const httpd_uri_t r[] = {
        { .uri = "/api/relay",       .method = HTTP_POST, .handler = h_relay },
        { .uri = "/api/relays/test", .method = HTTP_POST, .handler = h_test },
        { .uri = "/api/relays/off",  .method = HTTP_POST, .handler = h_off },
        { .uri = "/api/board",       .method = HTTP_POST, .handler = h_board },
    };
    for (size_t i = 0; i < sizeof(r) / sizeof(r[0]); i++) httpd_register_uri_handler(s, &r[i]);
}

static void status_json(void *obj)
{
    cJSON *j = obj;
    cJSON *bs = cJSON_AddArrayToObject(j, "boards");
    for (size_t i = 0; i < N_BOARDS; i++) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "id", BOARDS[i].id);
        cJSON_AddStringToObject(o, "name", BOARDS[i].name);
        cJSON_AddItemToArray(bs, o);
    }
    cJSON_AddBoolToObject(j, "board_unknown", s_board == NULL);
    if (!s_board) return;
    cJSON *rl = cJSON_AddArrayToObject(j, "relays");
    for (int i = 0; i < s_board->relay_count; i++) cJSON_AddItemToArray(rl, cJSON_CreateBool(s_relay[i]));
    cJSON *in = cJSON_AddArrayToObject(j, "inputs");
    for (int i = 0; i < s_board->input_count; i++)
        cJSON_AddItemToArray(in, cJSON_CreateBool(gpio_get_level(s_board->input_pins[i]) == 0));
}

void app_main(void)
{
    s_mx = xSemaphoreCreateMutex();

    dd_core_config_t cfg = DD_CORE_DEFAULT_CONFIG("dd-reles");
    cfg.register_routes = register_routes;
    cfg.status_json = status_json;
    ESP_ERROR_CHECK(dd_core_start(&cfg));

    if (dd_safe_mode()) {
        ESP_LOGW(TAG, "Modo seguro: no se inicializan los relés");
        return;
    }

    dd_status_t st;
    dd_status_get(&st);
    for (size_t i = 0; i < N_BOARDS; i++) {
        if (!strcmp(st.board_id, BOARDS[i].id)) { board_init(&BOARDS[i]); break; }
    }
    if (!s_board) ESP_LOGW(TAG, "Placa sin elegir: entrá al panel y elegila (los pines quedan sin tocar)");

    // Vigilancia del expansor: si un transitorio lo resetea, los relés quedan
    // sueltos mientras el firmware cree que están prendidos (pasó en el DruidaBot).
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        if (s_board && s_board->tca9554) {
            xSemaphoreTake(s_mx, portMAX_DELAY);
            relays_apply();
            xSemaphoreGive(s_mx);
        }
    }
}
