/*
 * dd_web.c — panel local del equipo (http://dd-xxxxxx.local o la IP).
 *
 * Una sola página (web/index.html, embebida) + una API JSON chica:
 *   GET  /api/status            estado (público, sin la clave)
 *   GET  /api/scan              redes WiFi cercanas
 *   POST /api/wifi              {"ssid","pass"}
 *   POST /api/ota/check | /api/ota/install
 *   POST /api/ota/upload        body = .bin
 *   POST /api/settings          {"auto_update":bool, "manifest":"https://..."}
 *   POST /api/restart
 *
 * Seguridad: desde el portal de configuración (red DataDruida-XXXX, hace
 * falta estar al lado del equipo) no se pide nada. Desde la red de la casa
 * todo lo que modifica exige el header X-DD-Key con la clave del equipo.
 */
#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "lwip/sockets.h"
#include "cJSON.h"

#include "dd_internal.h"

static const char *TAG = "dd_web";
static httpd_handle_t s_srv;

extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[]   asm("_binary_index_html_end");

/* ── helpers ─────────────────────────────────────────────────────────── */

static bool req_from_setup_ap(httpd_req_t *req)
{
    struct sockaddr_in6 a;
    socklen_t l = sizeof(a);
    if (getsockname(httpd_req_to_sockfd(req), (struct sockaddr *)&a, &l) != 0) return false;
    // lwIP entrega IPv4 mapeada en IPv6: los últimos 4 bytes son la IPv4 local.
    const uint8_t *ip = (const uint8_t *)&a.sin6_addr.un.u32_addr[3];
    if (a.sin6_family == AF_INET) ip = (const uint8_t *)&((struct sockaddr_in *)&a)->sin_addr.s_addr;
    return ip[0] == 192 && ip[1] == 168 && ip[2] == 4 && ip[3] == 1;
}

esp_err_t dd_web_auth(httpd_req_t *req)
{
    if (req_from_setup_ap(req)) return ESP_OK;
    char key[16] = "";
    if (httpd_req_get_hdr_value_str(req, "X-DD-Key", key, sizeof(key)) == ESP_OK &&
        strcmp(key, g_dd.admin_key) == 0) {
        return ESP_OK;
    }
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"error\":\"Falta la clave del equipo\"}");
    return ESP_FAIL;
}

int dd_web_read_body(httpd_req_t *req, char *buf, int max)
{
    int len = req->content_len < max - 1 ? req->content_len : max - 1, got = 0;
    while (got < len) {
        int r = httpd_req_recv(req, buf + got, len - got);
        if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (r <= 0) return -1;
        got += r;
    }
    buf[got] = '\0';
    return got;
}

static esp_err_t send_json(httpd_req_t *req, cJSON *j)
{
    char *s = cJSON_PrintUnformatted(j);
    cJSON_Delete(j);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t e = httpd_resp_sendstr(req, s ? s : "{}");
    free(s);
    return e;
}

static esp_err_t send_ok(httpd_req_t *req, const char *msg)
{
    cJSON *j = cJSON_CreateObject();
    cJSON_AddBoolToObject(j, "ok", true);
    if (msg) cJSON_AddStringToObject(j, "msg", msg);
    return send_json(req, j);
}

static esp_err_t send_err(httpd_req_t *req, const char *status, const char *msg)
{
    httpd_resp_set_status(req, status);
    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "error", msg);
    return send_json(req, j);
}

static void restart_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS((int)(intptr_t)arg));
    esp_restart();
}

static void restart_in(int ms) { xTaskCreate(restart_task, "dd_rst", 2048, (void *)(intptr_t)ms, 5, NULL); }

/* ── rutas ───────────────────────────────────────────────────────────── */

static esp_err_t h_index(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, index_html_start, index_html_end - index_html_start - 1);  // sin el NUL final que agrega EMBED_TXTFILES
}

static const char *net_str(dd_net_state_t n)
{
    switch (n) {
    case DD_NET_CONNECTING: return "connecting";
    case DD_NET_ONLINE:     return "online";
    case DD_NET_FAILED:     return "failed";
    default:                return "off";
    }
}

static const char *ota_str(dd_ota_state_t o)
{
    switch (o) {
    case DD_OTA_CHECKING:    return "checking";
    case DD_OTA_UP_TO_DATE:  return "up_to_date";
    case DD_OTA_AVAILABLE:   return "available";
    case DD_OTA_DOWNLOADING: return "downloading";
    case DD_OTA_DONE:        return "done";
    case DD_OTA_ERROR:       return "error";
    default:                 return "idle";
    }
}

static esp_err_t h_status(httpd_req_t *req)
{
    dd_status_t st;
    dd_status_get(&st);
    const bool local = req_from_setup_ap(req);
    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "app", st.app_name);
    cJSON_AddStringToObject(j, "version", st.fw_version);
    cJSON_AddStringToObject(j, "board", st.board_id);
    cJSON_AddStringToObject(j, "device", st.device_id);
    cJSON_AddStringToObject(j, "hostname", st.hostname);
    cJSON_AddStringToObject(j, "net", net_str(st.net));
    cJSON_AddStringToObject(j, "ssid", st.ssid);
    cJSON_AddStringToObject(j, "ip", st.ip);
    cJSON_AddNumberToObject(j, "rssi", st.rssi);
    cJSON_AddBoolToObject(j, "setup_ap", st.setup_ap);
    cJSON_AddBoolToObject(j, "via_setup_ap", local);
    cJSON_AddBoolToObject(j, "safe_mode", st.safe_mode);
    cJSON_AddBoolToObject(j, "pending_verify", st.pending_verify);
    cJSON_AddStringToObject(j, "ota", ota_str(st.ota));
    cJSON_AddNumberToObject(j, "ota_progress", st.ota_progress);
    cJSON_AddStringToObject(j, "ota_new_version", st.ota_new_version);
    cJSON_AddStringToObject(j, "ota_msg", st.ota_msg);
    cJSON_AddBoolToObject(j, "auto_update", st.auto_update);
    cJSON_AddBoolToObject(j, "has_screen", g_dd_cfg.has_screen);
    cJSON_AddStringToObject(j, "build", st.build);
    cJSON_AddBoolToObject(j, "paired", st.paired);
    cJSON_AddStringToObject(j, "owner", st.owner);
    cJSON_AddStringToObject(j, "pair_code", st.pair_code);
    cJSON_AddNumberToObject(j, "pair_left_s", st.pair_left_s);
    cJSON_AddStringToObject(j, "cloud_msg", st.cloud_msg);
    cJSON_AddNumberToObject(j, "uptime_s", (double)(esp_timer_get_time() / 1000000));
    cJSON_AddNumberToObject(j, "heap_kb", (double)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
    // La clave sólo se muestra a quien está al lado del equipo (portal).
    if (local) cJSON_AddStringToObject(j, "key", st.admin_key);
    if (g_dd_cfg.status_json) g_dd_cfg.status_json(j);
    return send_json(req, j);
}

static esp_err_t h_scan(httpd_req_t *req)
{
    if (dd_web_auth(req) != ESP_OK) return ESP_OK;
    dd_ap_t aps[20];
    int n = dd_wifi_scan(aps, 20);
    cJSON *j = cJSON_CreateObject();
    cJSON *arr = cJSON_AddArrayToObject(j, "networks");
    for (int i = 0; i < n; i++) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "ssid", aps[i].ssid);
        cJSON_AddNumberToObject(o, "rssi", aps[i].rssi);
        cJSON_AddBoolToObject(o, "secure", aps[i].secure);
        cJSON_AddItemToArray(arr, o);
    }
    return send_json(req, j);
}

static esp_err_t h_wifi(httpd_req_t *req)
{
    if (dd_web_auth(req) != ESP_OK) return ESP_OK;
    char body[256];
    if (dd_web_read_body(req, body, sizeof(body)) < 0) return send_err(req, "400 Bad Request", "Body inválido");
    cJSON *j = cJSON_Parse(body);
    const cJSON *s = cJSON_GetObjectItem(j, "ssid"), *p = cJSON_GetObjectItem(j, "pass");
    esp_err_t err = ESP_ERR_INVALID_ARG;
    if (cJSON_IsString(s)) err = dd_wifi_set(s->valuestring, cJSON_IsString(p) ? p->valuestring : "");
    cJSON_Delete(j);
    if (err != ESP_OK) return send_err(req, "400 Bad Request", "Nombre de red o contraseña inválidos");
    return send_ok(req, "Conectando...");
}

static esp_err_t h_ota_check(httpd_req_t *req)
{
    if (dd_web_auth(req) != ESP_OK) return ESP_OK;
    dd_ota_check();
    return send_ok(req, NULL);
}

static esp_err_t h_ota_install(httpd_req_t *req)
{
    if (dd_web_auth(req) != ESP_OK) return ESP_OK;
    dd_ota_install();
    return send_ok(req, NULL);
}

static esp_err_t h_ota_upload(httpd_req_t *req)
{
    if (dd_web_auth(req) != ESP_OK) return ESP_OK;
    esp_err_t err = dd_ota_from_upload(req);
    if (err != ESP_OK) {
        dd_status_t st;
        dd_status_get(&st);
        return send_err(req, "400 Bad Request", st.ota_msg[0] ? st.ota_msg : "No se pudo instalar");
    }
    send_ok(req, "Instalado. Reiniciando...");
    restart_in(1500);
    return ESP_OK;
}

static esp_err_t h_settings(httpd_req_t *req)
{
    if (dd_web_auth(req) != ESP_OK) return ESP_OK;
    char body[320];
    if (dd_web_read_body(req, body, sizeof(body)) < 0) return send_err(req, "400 Bad Request", "Body inválido");
    cJSON *j = cJSON_Parse(body);
    const cJSON *a = cJSON_GetObjectItem(j, "auto_update");
    const cJSON *m = cJSON_GetObjectItem(j, "manifest");
    if (cJSON_IsBool(a)) dd_ota_set_auto(cJSON_IsTrue(a));
    esp_err_t err = ESP_OK;
    if (cJSON_IsString(m)) err = dd_ota_set_manifest_url(m->valuestring);
    cJSON_Delete(j);
    if (err != ESP_OK) return send_err(req, "400 Bad Request", "La URL del manifest tiene que ser https://");
    return send_ok(req, "Guardado");
}

/* {"action":"start"|"cancel"}: muestra o cancela el código para vincular. */
static esp_err_t h_pair(httpd_req_t *req)
{
    if (dd_web_auth(req) != ESP_OK) return ESP_OK;
    char body[64];
    if (dd_web_read_body(req, body, sizeof(body)) < 0) return send_err(req, "400 Bad Request", "Body inválido");
    const bool cancel = strstr(body, "cancel") != NULL;
    if (cancel) dd_pair_cancel();
    else dd_pair_start();
    return send_ok(req, NULL);
}

static esp_err_t h_restart(httpd_req_t *req)
{
    if (dd_web_auth(req) != ESP_OK) return ESP_OK;
    send_ok(req, "Reiniciando...");
    restart_in(800);
    return ESP_OK;
}

/* Portal cautivo: cualquier URL desconocida vuelve a la página principal
 * (Android pide /generate_204, iOS /hotspot-detect.html, Windows /connecttest.txt). */
static esp_err_t h_404(httpd_req_t *req, httpd_err_code_t err)
{
    (void)err;
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", req_from_setup_ap(req) ? "http://192.168.4.1/" : "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

esp_err_t dd_web_start(void)
{
    httpd_config_t c = HTTPD_DEFAULT_CONFIG();
    c.max_uri_handlers = 24;
    c.stack_size = 8192;
    c.lru_purge_enable = true;
    c.recv_wait_timeout = 20;
    esp_err_t err = httpd_start(&s_srv, &c);
    if (err != ESP_OK) { ESP_LOGE(TAG, "httpd_start: %s", esp_err_to_name(err)); return err; }

    const httpd_uri_t routes[] = {
        { .uri = "/",               .method = HTTP_GET,  .handler = h_index },
        { .uri = "/api/status",     .method = HTTP_GET,  .handler = h_status },
        { .uri = "/api/scan",       .method = HTTP_GET,  .handler = h_scan },
        { .uri = "/api/wifi",       .method = HTTP_POST, .handler = h_wifi },
        { .uri = "/api/ota/check",  .method = HTTP_POST, .handler = h_ota_check },
        { .uri = "/api/ota/install",.method = HTTP_POST, .handler = h_ota_install },
        { .uri = "/api/ota/upload", .method = HTTP_POST, .handler = h_ota_upload },
        { .uri = "/api/settings",   .method = HTTP_POST, .handler = h_settings },
        { .uri = "/api/restart",    .method = HTTP_POST, .handler = h_restart },
        { .uri = "/api/pair",       .method = HTTP_POST, .handler = h_pair },
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) httpd_register_uri_handler(s_srv, &routes[i]);
    httpd_register_err_handler(s_srv, HTTPD_404_NOT_FOUND, h_404);
    if (g_dd_cfg.register_routes) g_dd_cfg.register_routes(s_srv);
    ESP_LOGI(TAG, "Panel local en el puerto 80");
    return ESP_OK;
}
