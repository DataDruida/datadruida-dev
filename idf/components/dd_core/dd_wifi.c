/*
 * dd_wifi.c — STA con reconexión, portal de configuración (AP) y mDNS.
 *
 * El equipo siempre queda en modo STA. El AP "DataDruida-XXXX" (abierto, en
 * 192.168.4.1) se suma sólo cuando hace falta configurar:
 *   - placa nueva sin red guardada (si la app lo pide: open_ap_if_unset),
 *   - modo seguro,
 *   - a pedido (botón en la pantalla).
 * Con la red guardada caída NO se abre solo: un AP abierto permanente dejaría
 * a cualquiera que pase cerca cambiar la red o subir firmware.
 */
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "mdns.h"

#include "dd_internal.h"

static const char *TAG = "dd_wifi";

static esp_netif_t *s_sta, *s_ap;
static esp_timer_handle_t s_retry_timer;
static int  s_retries;
static bool s_scanning;
static SemaphoreHandle_t s_scan_mutex;

static void retry_cb(void *arg) { (void)arg; esp_wifi_connect(); }

static void schedule_retry(void)
{
    // Rápido al principio, después cada 15 s. Nunca deja de intentar.
    int ms = s_retries < 5 ? 2000 : 15000;
    if (s_retries < 1000) s_retries++;
    esp_timer_stop(s_retry_timer);
    esp_timer_start_once(s_retry_timer, (uint64_t)ms * 1000);
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        if (g_dd.ssid[0]) esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *d = data;
        dd_lock();
        bool had = g_dd.ssid[0] != 0;
        g_dd.net = had ? (s_retries >= 3 ? DD_NET_FAILED : DD_NET_CONNECTING) : DD_NET_OFF;
        g_dd.ip[0] = '\0';
        dd_unlock();
        ESP_LOGW(TAG, "Desconectado (motivo %d)", d ? d->reason : -1);
        // Durante un escaneo el driver desconecta: no pelear con él.
        if (had && !s_scanning) schedule_retry();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = data;
        s_retries = 0;
        wifi_ap_record_t ap;
        dd_lock();
        g_dd.net = DD_NET_ONLINE;
        snprintf(g_dd.ip, sizeof(g_dd.ip), IPSTR, IP2STR(&e->ip_info.ip));
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) g_dd.rssi = ap.rssi;
        dd_unlock();
        ESP_LOGI(TAG, "Conectado: http://%s.local  (%s)", g_dd.hostname, g_dd.ip);
        dd_ota_on_online();
    }
}

esp_err_t dd_wifi_init(void)
{
    s_scan_mutex = xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(esp_netif_init());
    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;

    s_sta = esp_netif_create_default_wifi_sta();
    s_ap  = esp_netif_create_default_wifi_ap();
    esp_netif_set_hostname(s_sta, g_dd.hostname);

    wifi_init_config_t wcfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wcfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));  // las credenciales las guarda dd_core
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL));

    const esp_timer_create_args_t targs = { .callback = retry_cb, .name = "dd_wifi_retry" };
    ESP_ERROR_CHECK(esp_timer_create(&targs, &s_retry_timer));

    char pass[65];
    dd_nvs_get_str("ssid", g_dd.ssid, sizeof(g_dd.ssid));
    dd_nvs_get_str("pass", pass, sizeof(pass));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    if (g_dd.ssid[0]) {
        wifi_config_t sc = { 0 };
        strlcpy((char *)sc.sta.ssid, g_dd.ssid, sizeof(sc.sta.ssid));
        strlcpy((char *)sc.sta.password, pass, sizeof(sc.sta.password));
        sc.sta.threshold.authmode = pass[0] ? WIFI_AUTH_WEP : WIFI_AUTH_OPEN;
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sc));
        g_dd.net = DD_NET_CONNECTING;
    }
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_wifi_set_ps(WIFI_PS_NONE);   // panel local y OTA más ágiles; no son equipos a batería

    if (mdns_init() == ESP_OK) {
        mdns_hostname_set(g_dd.hostname);
        mdns_instance_name_set("DataDruida");
        mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    }

    if (!g_dd.ssid[0] && g_dd_cfg.open_ap_if_unset) dd_setup_ap(true);
    return ESP_OK;
}

void dd_setup_ap(bool on)
{
    if (on == g_dd.setup_ap) return;
    if (on) {
        wifi_config_t ac = { 0 };
        strlcpy((char *)ac.ap.ssid, g_dd.ap_ssid, sizeof(ac.ap.ssid));
        ac.ap.ssid_len = strlen(g_dd.ap_ssid);
        ac.ap.channel = 1;
        ac.ap.max_connection = 4;
        ac.ap.authmode = WIFI_AUTH_OPEN;
        esp_wifi_set_mode(WIFI_MODE_APSTA);
        esp_wifi_set_config(WIFI_IF_AP, &ac);
        dd_dns_start();
        ESP_LOGI(TAG, "Portal de configuración abierto: red \"%s\" → http://192.168.4.1", g_dd.ap_ssid);
    } else {
        dd_dns_stop();
        esp_wifi_set_mode(WIFI_MODE_STA);
        ESP_LOGI(TAG, "Portal de configuración cerrado");
    }
    dd_lock(); g_dd.setup_ap = on; dd_unlock();
}

int dd_wifi_scan(dd_ap_t *out, int max)
{
    xSemaphoreTake(s_scan_mutex, portMAX_DELAY);
    s_scanning = true;
    // Mientras está intentando conectar, el driver rechaza el escaneo.
    if (g_dd.net != DD_NET_ONLINE) { esp_timer_stop(s_retry_timer); esp_wifi_disconnect(); }

    wifi_scan_config_t sc = { .show_hidden = false };
    int n = 0;
    if (esp_wifi_scan_start(&sc, true) == ESP_OK) {
        uint16_t cnt = 24;
        wifi_ap_record_t *recs = calloc(cnt, sizeof(*recs));
        if (recs && esp_wifi_scan_get_ap_records(&cnt, recs) == ESP_OK) {
            for (int i = 0; i < cnt && n < max; i++) {
                if (!recs[i].ssid[0]) continue;
                bool dup = false;   // la misma red en varios APs: una sola vez
                for (int j = 0; j < n; j++) if (!strcmp(out[j].ssid, (char *)recs[i].ssid)) dup = true;
                if (dup) continue;
                strlcpy(out[n].ssid, (char *)recs[i].ssid, sizeof(out[n].ssid));
                out[n].rssi = recs[i].rssi;
                out[n].secure = recs[i].authmode != WIFI_AUTH_OPEN;
                n++;
            }
        }
        free(recs);
    }
    s_scanning = false;
    if (g_dd.ssid[0] && g_dd.net != DD_NET_ONLINE) { s_retries = 0; esp_wifi_connect(); }
    xSemaphoreGive(s_scan_mutex);
    return n;
}

static void close_ap_later(void *arg)
{
    (void)arg;
    // Le da tiempo al celular a leer la respuesta "conectado" antes de cerrar el AP.
    for (int i = 0; i < 40; i++) {
        vTaskDelay(pdMS_TO_TICKS(500));
        if (g_dd.net == DD_NET_ONLINE) break;
    }
    if (g_dd.net == DD_NET_ONLINE && !g_dd.safe_mode) {
        vTaskDelay(pdMS_TO_TICKS(15000));
        dd_setup_ap(false);
    }
    vTaskDelete(NULL);
}

esp_err_t dd_wifi_set(const char *ssid, const char *pass)
{
    if (!ssid || !ssid[0] || strlen(ssid) > 32) return ESP_ERR_INVALID_ARG;
    if (!pass) pass = "";
    if (strlen(pass) > 63) return ESP_ERR_INVALID_ARG;

    dd_nvs_set_str("ssid", ssid);
    dd_nvs_set_str("pass", pass);

    wifi_config_t sc = { 0 };
    strlcpy((char *)sc.sta.ssid, ssid, sizeof(sc.sta.ssid));
    strlcpy((char *)sc.sta.password, pass, sizeof(sc.sta.password));
    sc.sta.threshold.authmode = pass[0] ? WIFI_AUTH_WEP : WIFI_AUTH_OPEN;

    dd_lock();
    strlcpy(g_dd.ssid, ssid, sizeof(g_dd.ssid));
    g_dd.net = DD_NET_CONNECTING;
    g_dd.ip[0] = '\0';
    dd_unlock();

    esp_timer_stop(s_retry_timer);
    s_retries = 0;
    esp_wifi_disconnect();
    esp_wifi_set_config(WIFI_IF_STA, &sc);
    esp_wifi_connect();
    ESP_LOGI(TAG, "Red guardada: \"%s\"", ssid);
    if (g_dd.setup_ap) xTaskCreate(close_ap_later, "dd_ap_close", 2560, NULL, 2, NULL);
    return ESP_OK;
}
