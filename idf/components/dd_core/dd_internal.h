#pragma once
#include "dd_core.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

extern dd_status_t       g_dd;        // proteger con dd_lock()/dd_unlock()
extern dd_core_config_t  g_dd_cfg;
extern char              g_dd_secret[33];   // secreto del equipo para la nube (NVS "secret")

void dd_lock(void);
void dd_unlock(void);

/* NVS (namespace "dd") */
esp_err_t dd_nvs_get_str(const char *key, char *out, size_t len);
esp_err_t dd_nvs_set_str(const char *key, const char *val);
uint8_t   dd_nvs_get_u8(const char *key, uint8_t def);
esp_err_t dd_nvs_set_u8(const char *key, uint8_t val);

/* módulos */
esp_err_t dd_wifi_init(void);
esp_err_t dd_web_start(void);
void      dd_dns_start(void);
void      dd_dns_stop(void);
esp_err_t dd_ota_init(void);
void      dd_ota_on_online(void);
/* OTA desde un .bin subido al panel local (lo llama dd_web). */
esp_err_t dd_ota_from_upload(httpd_req_t *req);

/* Nube (dd_cloud.c) */
void      dd_cloud_init(void);
/* dd_ota.c: la usa dd_cloud para instalar un programa de la cuenta. */
/* Ofrece una imagen: con build (programa de la cuenta) se instala si es otra
 * compilación; sin build (base pública) sólo si la versión es más nueva.
 * Devuelve true si quedó DD_OTA_AVAILABLE. */
bool      dd_ota_offer(const char *url, const char *version, const char *build);
const char *dd_ota_bad_build(void);   // id de la imagen que falló ("" si ninguna)
/* Llamada desde la tarea de OTA en cada vuelta; devuelve cuánto esperar (ticks)
 * hasta la próxima, o 0 si no está vinculada ni vinculándose (usa el manifest
 * público). */
TickType_t dd_cloud_tick(void);
