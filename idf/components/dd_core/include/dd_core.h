/*
 * dd_core — base común de todos los firmwares DataDruida en ESP-IDF.
 *
 * Lo que resuelve, para que ninguna app tenga que volver a pensarlo:
 *
 *   - WiFi: se conecta a la red guardada y reintenta solo. Si no hay red
 *     guardada (placa nueva) abre la red "DataDruida-XXXX" con un portal
 *     cautivo para elegir la red desde el celular.
 *   - Actualizaciones por OTA, nunca por USB:
 *       1. desde la nube: un manifest.json con la última versión publicada;
 *       2. desde la red local: subir un .bin en http://dd-xxxx.local
 *   - Protección contra un firmware roto: rollback del bootloader. Una imagen
 *     nueva arranca "a prueba" y sólo queda instalada cuando se conecta al
 *     WiFi y anda 20 s sin colgarse; si no, el equipo vuelve solo a la
 *     anterior.
 *   - Modo seguro: si el equipo se reinicia por error 3 veces seguidas al
 *     arrancar (o se mantiene BOOT apretado en las placas que lo permiten),
 *     arranca sin la app del usuario: sólo WiFi + portal + OTA.
 *   - Clave del equipo: las acciones del panel local (subir firmware, cambiar
 *     la red, mover relés) piden la clave. Se genera al primer arranque y se
 *     muestra en la pantalla o en el portal de configuración.
 *
 * Una app usa dd_core así:
 *
 *     dd_core_config_t cfg = DD_CORE_DEFAULT_CONFIG("dd-reles");
 *     dd_core_start(&cfg);
 *     if (!dd_safe_mode()) { ... la app ... }
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DD_MANIFEST_BASE "https://app.datadruida.com.ar/firmware/dd-idf/"

typedef enum {
    DD_NET_OFF = 0,      // sin red guardada y sin portal abierto
    DD_NET_CONNECTING,   // intentando conectar a la red guardada
    DD_NET_ONLINE,       // conectado, con IP
    DD_NET_FAILED,       // la red guardada no responde (sigue reintentando)
} dd_net_state_t;

typedef enum {
    DD_OTA_IDLE = 0,
    DD_OTA_CHECKING,
    DD_OTA_UP_TO_DATE,
    DD_OTA_AVAILABLE,    // hay una versión nueva (ota_new_version)
    DD_OTA_DOWNLOADING,  // ota_progress 0..100
    DD_OTA_DONE,         // instalada, reiniciando
    DD_OTA_ERROR,        // ota_msg dice por qué
} dd_ota_state_t;

typedef struct {
    dd_net_state_t net;
    bool     setup_ap;          // portal "DataDruida-XXXX" abierto
    char     ssid[33];          // red guardada
    char     ip[16];
    int      rssi;
    char     ap_ssid[33];       // nombre de la red de configuración
    char     hostname[24];      // dd-xxxxxx  →  http://dd-xxxxxx.local
    char     device_id[16];     // xxxxxx (MAC)
    char     admin_key[12];     // clave del equipo
    char     app_name[24];
    char     board_id[48];
    char     fw_version[32];
    bool     safe_mode;
    bool     pending_verify;    // esta imagen todavía está "a prueba"
    dd_ota_state_t ota;
    int      ota_progress;
    char     ota_new_version[32];
    char     ota_msg[96];
    bool     auto_update;
} dd_status_t;

typedef struct {
    const char *app_name;       // nombre del firmware: arma la URL del manifest
    const char *board_id;       // placa; NULL = la que esté guardada en NVS
    bool  has_screen;           // con pantalla: el WiFi se configura en ella
    bool  open_ap_if_unset;     // abrir el portal si no hay red guardada
    int   boot_button_gpio;     // -1 si la placa no tiene BOOT libre
    bool  auto_update_default;  // instalar solo las versiones nuevas de la nube
    void (*register_routes)(httpd_handle_t server);  // rutas extra del panel
    void (*status_json)(void *cjson_obj);            // campos extra en /api/status
} dd_core_config_t;

#define DD_CORE_DEFAULT_CONFIG(name) {      \
    .app_name = (name),                     \
    .board_id = NULL,                       \
    .has_screen = false,                    \
    .open_ap_if_unset = true,               \
    .boot_button_gpio = 0,                  \
    .auto_update_default = true,            \
    .register_routes = NULL,                \
    .status_json = NULL,                    \
}

esp_err_t dd_core_start(const dd_core_config_t *cfg);

/* Copia del estado actual (thread-safe). */
void dd_status_get(dd_status_t *out);
bool dd_safe_mode(void);

/* WiFi */
typedef struct { char ssid[33]; int rssi; bool secure; } dd_ap_t;
int       dd_wifi_scan(dd_ap_t *out, int max);          // bloquea ~2-3 s
esp_err_t dd_wifi_set(const char *ssid, const char *pass);  // guarda y conecta
void      dd_setup_ap(bool on);                          // abre/cierra el portal

/* OTA */
void dd_ota_check(void);                  // async: busca versión nueva
void dd_ota_install(void);                // async: instala la de la nube
void dd_ota_set_auto(bool on);
esp_err_t dd_ota_set_manifest_url(const char *url);  // "" = la de fábrica

/* Placa elegida (NVS "board"); las apps headless la usan para el pinout. */
esp_err_t dd_board_set(const char *board_id);

/* Para rutas extra del panel: exige la clave del equipo si hace falta.
 * Devuelve ESP_OK si se puede seguir; si no, ya respondió 401. */
esp_err_t dd_web_auth(httpd_req_t *req);
/* Lee el body completo (máx. max-1 bytes) y lo termina en '\0'. */
int dd_web_read_body(httpd_req_t *req, char *buf, int max);

#ifdef __cplusplus
}
#endif
