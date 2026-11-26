/*
 * dd_runtime.c — corre el programa del usuario (dd_setup / dd_loop) con las
 * redes de seguridad puestas, y le suma al panel de la placa los relés, la
 * elección de placa y los mensajes del programa.
 *
 * Watchdog: si dd_loop() no vuelve en 60 s, un task aparte apaga los relés y
 * reinicia. Si el programa se cae (panic) 3 veces seguidas al arrancar,
 * dd_core arranca en modo seguro y el programa no corre: queda WiFi + OTA
 * para instalar uno corregido.
 */
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "cJSON.h"

#include "dd_app_priv.h"

static const char *TAG = "app";
#define WATCHDOG_MS 60000

/* Si el programa no las define, no hace nada. */
__attribute__((weak)) void dd_setup(void) {}
__attribute__((weak)) void dd_loop(void) {}

static volatile uint32_t s_every_ms = 1000;
static volatile int64_t  s_fed_us;
static volatile bool     s_running;
static volatile uint32_t s_loops;
static volatile bool     s_test_running;

void dd_every(uint32_t ms) { s_every_ms = ms < 50 ? 50 : ms; }

/* ── mensajes del programa (últimas 40 líneas, para el panel) ────────── */

#define LOG_LINES 40
#define LOG_LEN   120
static char s_log[LOG_LINES][LOG_LEN];
static uint32_t s_log_seq;            /* nº de la próxima línea */
static SemaphoreHandle_t s_log_mx;

void dd_log_init(void) { if (!s_log_mx) s_log_mx = xSemaphoreCreateMutex(); }

void dd_log(const char *fmt, ...)
{
    char line[LOG_LEN];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    ESP_LOGI(TAG, "%s", line);
    if (!s_log_mx) return;
    xSemaphoreTake(s_log_mx, portMAX_DELAY);
    strlcpy(s_log[s_log_seq % LOG_LINES], line, LOG_LEN);
    s_log_seq++;
    xSemaphoreGive(s_log_mx);
}

/* ── el programa ─────────────────────────────────────────────────────── */

static void app_task(void *arg)
{
    (void)arg;
    s_fed_us = esp_timer_get_time();
    s_running = true;
    dd_setup();
    int64_t next = esp_timer_get_time();
    int64_t next_heal = next;
    for (;;) {
        s_fed_us = esp_timer_get_time();
        const int64_t now = esp_timer_get_time();
        if (now >= next_heal) { dd_relays_heal_and_limits(); next_heal = now + 1000000; }
        if (now >= next && !s_test_running) {
            next = now + (int64_t)s_every_ms * 1000;
            dd_loop();
            s_loops++;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static void watchdog_task(void *arg)
{
    (void)arg;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        if (s_running && (esp_timer_get_time() - s_fed_us) / 1000 > WATCHDOG_MS) {
            ESP_LOGE(TAG, "El programa se colgó más de %d s: apago los relés y reinicio", WATCHDOG_MS / 1000);
            dd_relay_all_off();
            vTaskDelay(pdMS_TO_TICKS(200));
            esp_restart();
        }
    }
}

esp_err_t dd_app_start(void)
{
    dd_log_init();
    dd_board_init();       /* relés apagados antes que nada */
    dd_clock_start();
    if (dd_safe_mode()) {
        ESP_LOGW(TAG, "Modo seguro: el programa no corre");
        return ESP_OK;
    }
    xTaskCreate(watchdog_task, "dd_wdt", 2560, NULL, 10, NULL);
    /* Prioridad baja: la pantalla y la red tienen que seguir respondiendo aunque
     * el programa del usuario sea pesado. Stack generoso para printf de floats. */
    xTaskCreate(app_task, "dd_app", 8192, NULL, 2, NULL);
    return ESP_OK;
}

/* ── panel: relés, placa, mensajes ───────────────────────────────────── */

static esp_err_t reply(httpd_req_t *req, const char *status, const char *json)
{
    if (status) httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, json);
}

static esp_err_t h_relay(httpd_req_t *req)
{
    if (dd_web_auth(req) != ESP_OK) return ESP_OK;
    if (!dd_relay_count()) return reply(req, "400 Bad Request", "{\"error\":\"Primero elegí la placa\"}");
    char body[96];
    if (dd_web_read_body(req, body, sizeof(body)) < 0) return reply(req, "400 Bad Request", "{\"error\":\"Body inválido\"}");
    cJSON *j = cJSON_Parse(body);
    const cJSON *ch = cJSON_GetObjectItem(j, "ch"), *on = cJSON_GetObjectItem(j, "on");
    bool ok = cJSON_IsNumber(ch) && cJSON_IsBool(on);
    if (ok) dd_relay_set(ch->valueint, cJSON_IsTrue(on));
    cJSON_Delete(j);
    return ok ? reply(req, NULL, "{\"ok\":true}") : reply(req, "400 Bad Request", "{\"error\":\"Faltan ch / on\"}");
}

static void test_task(void *arg)
{
    (void)arg;
    dd_relay_all_off();
    for (int i = 1; i <= dd_relay_count() && s_test_running; i++) {
        dd_relay_set(i, true);
        vTaskDelay(pdMS_TO_TICKS(500));
        dd_relay_set(i, false);
        vTaskDelay(pdMS_TO_TICKS(150));
    }
    s_test_running = false;
    vTaskDelete(NULL);
}

static esp_err_t h_test(httpd_req_t *req)
{
    if (dd_web_auth(req) != ESP_OK) return ESP_OK;
    if (!dd_relay_count()) return reply(req, "400 Bad Request", "{\"error\":\"Primero elegí la placa\"}");
    /* Mientras dura la prueba el programa no corre: si no, pisaría los relés. */
    if (!s_test_running) { s_test_running = true; xTaskCreate(test_task, "relay_test", 3072, NULL, 3, NULL); }
    return reply(req, NULL, "{\"ok\":true}");
}

static esp_err_t h_off(httpd_req_t *req)
{
    if (dd_web_auth(req) != ESP_OK) return ESP_OK;
    s_test_running = false;
    dd_relay_all_off();
    return reply(req, NULL, "{\"ok\":true}");
}

static void restart_task(void *arg) { (void)arg; vTaskDelay(pdMS_TO_TICKS(1000)); esp_restart(); }

static esp_err_t h_board(httpd_req_t *req)
{
    if (dd_web_auth(req) != ESP_OK) return ESP_OK;
    char body[128];
    if (dd_web_read_body(req, body, sizeof(body)) < 0) return reply(req, "400 Bad Request", "{\"error\":\"Body inválido\"}");
    cJSON *j = cJSON_Parse(body);
    const cJSON *b = cJSON_GetObjectItem(j, "board");
    bool ok = false;
    for (int i = 0; cJSON_IsString(b) && i < DD_N_BOARDS; i++) {
        if (DD_BOARDS[i].relay_count && !strcmp(b->valuestring, DD_BOARDS[i].id)) { ok = dd_board_set(DD_BOARDS[i].id) == ESP_OK; break; }
    }
    cJSON_Delete(j);
    if (!ok) return reply(req, "400 Bad Request", "{\"error\":\"Placa desconocida\"}");
    xTaskCreate(restart_task, "rst", 2048, NULL, 5, NULL);   /* pines limpios desde el arranque */
    return reply(req, NULL, "{\"ok\":true}");
}

static esp_err_t h_log(httpd_req_t *req)
{
    if (dd_web_auth(req) != ESP_OK) return ESP_OK;
    cJSON *j = cJSON_CreateObject();
    cJSON *arr = cJSON_AddArrayToObject(j, "lines");
    xSemaphoreTake(s_log_mx, portMAX_DELAY);
    uint32_t first = s_log_seq > LOG_LINES ? s_log_seq - LOG_LINES : 0;
    for (uint32_t i = first; i < s_log_seq; i++) cJSON_AddItemToArray(arr, cJSON_CreateString(s_log[i % LOG_LINES]));
    cJSON_AddNumberToObject(j, "seq", s_log_seq);
    xSemaphoreGive(s_log_mx);
    char *s = cJSON_PrintUnformatted(j);
    cJSON_Delete(j);
    esp_err_t e = reply(req, NULL, s ? s : "{}");
    free(s);
    return e;
}

void dd_web_routes(httpd_handle_t s)
{
    const httpd_uri_t r[] = {
        { .uri = "/api/relay",       .method = HTTP_POST, .handler = h_relay },
        { .uri = "/api/relays/test", .method = HTTP_POST, .handler = h_test },
        { .uri = "/api/relays/off",  .method = HTTP_POST, .handler = h_off },
        { .uri = "/api/board",       .method = HTTP_POST, .handler = h_board },
        { .uri = "/api/log",         .method = HTTP_GET,  .handler = h_log },
    };
    for (size_t i = 0; i < sizeof(r) / sizeof(r[0]); i++) httpd_register_uri_handler(s, &r[i]);
}

void dd_web_status(void *obj)
{
    cJSON *j = obj;
    cJSON *app = cJSON_AddObjectToObject(j, "program");
    cJSON_AddBoolToObject(app, "running", s_running);
    cJSON_AddNumberToObject(app, "loops", s_loops);
    cJSON_AddNumberToObject(app, "log_seq", s_log_seq);
    cJSON_AddBoolToObject(app, "time_ok", dd_time_ok());

    const dd_board_t *b = dd_board();
    /* La lista de placas sólo tiene sentido donde la placa se elige (dd-reles). */
    dd_status_t st;
    dd_status_get(&st);
    bool fixed = false;
    for (int i = 0; i < DD_N_BOARDS; i++) if (!strcmp(st.board_id, DD_BOARDS[i].id) && !DD_BOARDS[i].relay_count) fixed = true;
    if (!fixed) {
        cJSON *bs = cJSON_AddArrayToObject(j, "boards");
        for (int i = 0; i < DD_N_BOARDS; i++) {
            if (!DD_BOARDS[i].relay_count) continue;
            cJSON *o = cJSON_CreateObject();
            cJSON_AddStringToObject(o, "id", DD_BOARDS[i].id);
            cJSON_AddStringToObject(o, "name", DD_BOARDS[i].name);
            cJSON_AddItemToArray(bs, o);
        }
        cJSON_AddBoolToObject(j, "board_unknown", b == NULL);
    }
    if (!b || !b->relay_count) return;
    cJSON *rl = cJSON_AddArrayToObject(j, "relays");
    for (int i = 1; i <= b->relay_count; i++) cJSON_AddItemToArray(rl, cJSON_CreateBool(dd_relay_get(i)));
    cJSON *in = cJSON_AddArrayToObject(j, "inputs");
    for (int i = 1; i <= b->input_count; i++) cJSON_AddItemToArray(in, cJSON_CreateBool(dd_input(i)));
}

void dd_app_core_hooks(dd_core_config_t *cfg)
{
    cfg->register_routes = dd_web_routes;
    cfg->status_json = dd_web_status;
}
