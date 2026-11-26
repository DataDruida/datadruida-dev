#pragma once
#include "dd_core.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

extern dd_status_t       g_dd;        // proteger con dd_lock()/dd_unlock()
extern dd_core_config_t  g_dd_cfg;

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
