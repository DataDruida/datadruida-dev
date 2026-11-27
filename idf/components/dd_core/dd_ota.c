/*
 * dd_ota.c — actualizaciones sin USB.
 *
 * Nube: GET <manifest_url> →
 *   { "version": "0.2.0", "url": "dd-reles-0.2.0.bin", "notes": "..." }
 * "url" puede ser absoluta o relativa al manifest. Se busca al conectar y
 * cada 6 h; con auto_update se instala sola, si no queda DD_OTA_AVAILABLE y
 * la instala el usuario (pantalla o panel).
 *
 * Local: POST /api/ota/upload con el .bin como body (panel del equipo).
 *
 * Cuenta (dd_cloud.c): una placa vinculada no usa el manifest público. La
 * nube le dice qué imagen tiene que tener (dd_ota_offer) y se decide por id
 * de compilación, no por versión: todos los programas salen con la versión de
 * la base, así que "más nueva" no sirve para saber si hay que instalar.
 *
 * Una imagen que ya falló (el bootloader volvió atrás) no se reintenta: si
 * no, el equipo bajaría la misma imagen rota en un bucle.
 */
#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"

#include "dd_internal.h"

static const char *TAG = "dd_ota";

enum { CMD_CHECK = 1, CMD_INSTALL = 2 };
static QueueHandle_t s_q;
static char s_manifest[200];
static char s_bin_url[768];   // las URLs firmadas del Storage son largas
static char s_bad_version[32];
static char s_bad_build[20];
static bool s_periodic_started;

static void set_state(dd_ota_state_t st, int prog, const char *msg)
{
    dd_lock();
    g_dd.ota = st;
    g_dd.ota_progress = prog;
    if (msg) strlcpy(g_dd.ota_msg, msg, sizeof(g_dd.ota_msg));
    dd_unlock();
}

/* "1.2.3-algo" → compara numéricamente; >0 si a es más nueva que b. */
static int ver_cmp(const char *a, const char *b)
{
    for (int i = 0; i < 3; i++) {
        long x = strtol(a, (char **)&a, 10), y = strtol(b, (char **)&b, 10);
        if (x != y) return x > y ? 1 : -1;
        if (*a == '.') a++;
        if (*b == '.') b++;
    }
    return 0;
}

static esp_err_t fetch_manifest(char *buf, int max)
{
    esp_http_client_config_t hc = {
        .url = s_manifest,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 10000,
    };
    esp_http_client_handle_t c = esp_http_client_init(&hc);
    if (!c) return ESP_FAIL;
    esp_err_t err = esp_http_client_open(c, 0);
    int n = 0;
    if (err == ESP_OK) {
        esp_http_client_fetch_headers(c);
        int code = esp_http_client_get_status_code(c);
        if (code != 200) {
            ESP_LOGW(TAG, "manifest: HTTP %d", code);
            err = (code == 404) ? ESP_ERR_NOT_FOUND : ESP_FAIL;
        } else {
            int r;
            while (n < max - 1 && (r = esp_http_client_read(c, buf + n, max - 1 - n)) > 0) n += r;
        }
    }
    buf[n] = '\0';
    esp_http_client_cleanup(c);
    return err;
}

static void do_check(void)
{
    set_state(DD_OTA_CHECKING, 0, "Buscando actualizaciones...");
    char *buf = malloc(2048);
    if (!buf) { set_state(DD_OTA_ERROR, 0, "Sin memoria"); return; }
    esp_err_t ferr = fetch_manifest(buf, 2048);
    if (ferr != ESP_OK) {
        free(buf);
        // 404 = el servidor respondió bien pero no hay nada publicado para esta
        // placa: no es una falla, y decir "no pude consultar" asusta.
        if (ferr == ESP_ERR_NOT_FOUND) set_state(DD_OTA_UP_TO_DATE, 0, "Todavía no hay actualizaciones publicadas para esta placa");
        else set_state(DD_OTA_ERROR, 0, "No pude consultar el servidor de actualizaciones");
        return;
    }
    cJSON *j = cJSON_Parse(buf);
    free(buf);
    const cJSON *v = cJSON_GetObjectItem(j, "version");
    const cJSON *u = cJSON_GetObjectItem(j, "url");
    if (!cJSON_IsString(v) || !cJSON_IsString(u)) {
        cJSON_Delete(j);
        set_state(DD_OTA_ERROR, 0, "El servidor devolvió un manifest inválido");
        return;
    }
    // url relativa → misma carpeta que el manifest
    if (strncmp(u->valuestring, "http", 4) == 0) {
        strlcpy(s_bin_url, u->valuestring, sizeof(s_bin_url));
    } else {
        strlcpy(s_bin_url, s_manifest, sizeof(s_bin_url));
        char *slash = strrchr(s_bin_url, '/');
        if (slash) slash[1] = '\0';
        strlcat(s_bin_url, u->valuestring, sizeof(s_bin_url));
    }
    char newv[32];
    strlcpy(newv, v->valuestring, sizeof(newv));
    cJSON_Delete(j);

    dd_lock();
    strlcpy(g_dd.ota_new_version, newv, sizeof(g_dd.ota_new_version));
    const bool newer = ver_cmp(newv, g_dd.fw_version) > 0;
    dd_unlock();

    if (!newer) { set_state(DD_OTA_UP_TO_DATE, 0, "Tenés la última versión"); return; }
    if (s_bad_version[0] && !strcmp(newv, s_bad_version)) {
        set_state(DD_OTA_UP_TO_DATE, 0, "La versión nueva falló en este equipo: se queda en la actual");
        return;
    }
    char msg[64];
    snprintf(msg, sizeof(msg), "Versión %s disponible", newv);
    set_state(DD_OTA_AVAILABLE, 0, msg);
    ESP_LOGI(TAG, "%s (%s)", msg, s_bin_url);
}

static void do_install(void)
{
    if (!s_bin_url[0]) { do_check(); if (g_dd.ota != DD_OTA_AVAILABLE) return; }
    set_state(DD_OTA_DOWNLOADING, 0, "Descargando...");
    ESP_LOGI(TAG, "Instalando desde %s", s_bin_url);

    esp_http_client_config_t hc = {
        .url = s_bin_url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 15000,
        .keep_alive_enable = true,
        .buffer_size = 4096,
    };
    esp_https_ota_config_t oc = { .http_config = &hc };
    esp_https_ota_handle_t h = NULL;
    esp_err_t err = esp_https_ota_begin(&oc, &h);
    if (err != ESP_OK) { set_state(DD_OTA_ERROR, 0, "No pude empezar la descarga"); return; }

    const int total = esp_https_ota_get_image_size(h);
    while ((err = esp_https_ota_perform(h)) == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
        int got = esp_https_ota_get_image_len_read(h);
        if (total > 0) {
            dd_lock(); g_dd.ota_progress = (int)((int64_t)got * 100 / total); dd_unlock();
        }
    }
    if (err != ESP_OK || !esp_https_ota_is_complete_data_received(h)) {
        esp_https_ota_abort(h);
        set_state(DD_OTA_ERROR, 0, "La descarga se cortó: probá de nuevo");
        return;
    }
    err = esp_https_ota_finish(h);
    if (err != ESP_OK) {
        set_state(DD_OTA_ERROR, 0, err == ESP_ERR_OTA_VALIDATE_FAILED ? "El archivo descargado está dañado"
                                                                      : "No pude instalar la versión nueva");
        return;
    }
    set_state(DD_OTA_DONE, 100, "Instalada. Reiniciando...");
    ESP_LOGI(TAG, "OTA ok, reinicio");
    vTaskDelay(pdMS_TO_TICKS(1500));
    esp_restart();
}

bool dd_ota_offer(const char *url, const char *version, const char *build)
{
    if (!url || !url[0] || !version) return false;
    char cur_build[20], cur_ver[32];
    dd_lock();
    strlcpy(cur_build, g_dd.build, sizeof(cur_build));
    strlcpy(cur_ver, g_dd.fw_version, sizeof(cur_ver));
    dd_unlock();
    if (build && build[0]) {
        // Programa de la cuenta: se instala si es otra compilación.
        if (!strncmp(build, cur_build, 16)) return false;
        if (s_bad_build[0] && !strncmp(build, s_bad_build, 16)) {
            set_state(DD_OTA_UP_TO_DATE, 0, "El programa nuevo falló al arrancar: la placa volvió al anterior");
            return false;
        }
    } else {
        // Base pública: sólo si es más nueva.
        if (ver_cmp(version, cur_ver) <= 0) return false;
        if (s_bad_version[0] && !strcmp(version, s_bad_version)) return false;
    }
    if (g_dd.ota == DD_OTA_AVAILABLE && !strcmp(s_bin_url, url)) return true;
    strlcpy(s_bin_url, url, sizeof(s_bin_url));
    char msg[64];
    if (build && build[0]) snprintf(msg, sizeof(msg), "Llegó un programa nuevo desde tu cuenta");
    else snprintf(msg, sizeof(msg), "Versión %s disponible", version);
    dd_lock(); strlcpy(g_dd.ota_new_version, version, sizeof(g_dd.ota_new_version)); dd_unlock();
    set_state(DD_OTA_AVAILABLE, 0, msg);
    ESP_LOGI(TAG, "%s (build %s)", msg, build && build[0] ? build : "-");
    return true;
}

static void ota_task(void *arg)
{
    (void)arg;
    int cmd;
    // ⚠ No usar pdMS_TO_TICKS(6 h): con FREERTOS_HZ=1000 la cuenta se pasa
    // de 32 bits y da ~125 s (visto en la 4.3B: consultaba cada 2 min).
    const TickType_t six_h = (TickType_t)6 * 3600 * configTICK_RATE_HZ;
    TickType_t wait = six_h;
    for (;;) {
        if (xQueueReceive(s_q, &cmd, wait) != pdTRUE) cmd = CMD_CHECK;
        wait = six_h;
        if (g_dd.ota == DD_OTA_DOWNLOADING) continue;
        if (cmd == CMD_INSTALL) {
            if (g_dd.net == DD_NET_ONLINE) do_install();
            else set_state(DD_OTA_ERROR, 0, "Sin conexión a internet");
            continue;
        }
        // Vinculada (o vinculándose): manda la nube, cada 1 min (4 s mientras
        // se muestra el código). Un programa de la cuenta se instala siempre:
        // el usuario lo mandó a propósito.
        TickType_t w = dd_cloud_tick();
        if (w) {
            wait = w;
            if (g_dd.ota == DD_OTA_AVAILABLE && !g_dd.pending_verify && g_dd.net == DD_NET_ONLINE) do_install();
            continue;
        }
        if (g_dd.net != DD_NET_ONLINE) {
            set_state(DD_OTA_ERROR, 0, "Sin conexión a internet");
            continue;
        }
        do_check();
        // Una imagen a prueba no se reemplaza por otra hasta verificarse.
        if (g_dd.ota == DD_OTA_AVAILABLE && g_dd.auto_update && !g_dd.pending_verify) do_install();
    }
}

esp_err_t dd_ota_init(void)
{
    if (dd_nvs_get_str("manifest", s_manifest, sizeof(s_manifest)) != ESP_OK || !s_manifest[0]) {
        snprintf(s_manifest, sizeof(s_manifest), "%s%s/manifest.json", DD_MANIFEST_BASE, g_dd.app_name);
    }
    const esp_partition_t *bad = esp_ota_get_last_invalid_partition();
    esp_app_desc_t d;
    if (bad && esp_ota_get_partition_description(bad, &d) == ESP_OK) {
        strlcpy(s_bad_version, d.version, sizeof(s_bad_version));
        for (int i = 0; i < 8; i++) snprintf(s_bad_build + i * 2, 3, "%02x", d.app_elf_sha256[i]);
        ESP_LOGW(TAG, "La versión %s falló y se volvió atrás", s_bad_version);
        snprintf(g_dd.ota_msg, sizeof(g_dd.ota_msg), "La versión %s falló: se volvió a la anterior", s_bad_version);
        g_dd.ota = DD_OTA_ERROR;
    }
    s_q = xQueueCreate(4, sizeof(int));
    xTaskCreate(ota_task, "dd_ota", 8192, NULL, 4, NULL);
    return ESP_OK;
}

void dd_ota_on_online(void)
{
    if (s_periodic_started) return;
    s_periodic_started = true;
    dd_ota_check();
}

void dd_ota_check(void)   { int c = CMD_CHECK;   xQueueSend(s_q, &c, 0); }
void dd_ota_install(void) { int c = CMD_INSTALL; xQueueSend(s_q, &c, 0); }

void dd_ota_set_auto(bool on)
{
    dd_nvs_set_u8("autoupd", on ? 1 : 0);
    dd_lock(); g_dd.auto_update = on; dd_unlock();
}

esp_err_t dd_ota_set_manifest_url(const char *url)
{
    if (url && url[0] && strncmp(url, "https://", 8) != 0) return ESP_ERR_INVALID_ARG;
    esp_err_t err = dd_nvs_set_str("manifest", url);
    if (url && url[0]) strlcpy(s_manifest, url, sizeof(s_manifest));
    else snprintf(s_manifest, sizeof(s_manifest), "%s%s/manifest.json", DD_MANIFEST_BASE, g_dd.app_name);
    s_bin_url[0] = '\0';
    return err;
}

/* ── Subida local ─────────────────────────────────────────────────────── */

esp_err_t dd_ota_from_upload(httpd_req_t *req)
{
    if (g_dd.ota == DD_OTA_DOWNLOADING) return ESP_ERR_INVALID_STATE;
    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    if (!part || req->content_len <= 0 || req->content_len > part->size) return ESP_ERR_INVALID_SIZE;

    esp_ota_handle_t h;
    esp_err_t err = esp_ota_begin(part, OTA_WITH_SEQUENTIAL_WRITES, &h);
    if (err != ESP_OK) return err;
    set_state(DD_OTA_DOWNLOADING, 0, "Recibiendo firmware...");

    char *buf = malloc(4096);
    int left = req->content_len, got = 0;
    bool first = true;
    while (buf && left > 0) {
        int r = httpd_req_recv(req, buf, left < 4096 ? left : 4096);
        if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (r <= 0) { err = ESP_FAIL; break; }
        if (first) {
            first = false;
            const esp_image_header_t *ih = (const esp_image_header_t *)buf;
            // Otro chip (ej. un .bin de C5 en una S3) no arrancaría: se corta antes de escribir.
            if (r < (int)sizeof(*ih) || ih->magic != ESP_IMAGE_HEADER_MAGIC ||
                ih->chip_id != CONFIG_IDF_FIRMWARE_CHIP_ID) {
                err = ESP_ERR_OTA_VALIDATE_FAILED;
                break;
            }
        }
        err = esp_ota_write(h, buf, r);
        if (err != ESP_OK) break;
        left -= r; got += r;
        dd_lock(); g_dd.ota_progress = (int)((int64_t)got * 100 / req->content_len); dd_unlock();
    }
    free(buf);
    if (!buf) err = ESP_ERR_NO_MEM;
    if (err != ESP_OK) {
        esp_ota_abort(h);
        set_state(DD_OTA_ERROR, 0, err == ESP_ERR_OTA_VALIDATE_FAILED ? "Ese archivo no es un firmware para esta placa"
                                                                : "La subida se cortó");
        return err;
    }
    err = esp_ota_end(h);
    if (err == ESP_OK) err = esp_ota_set_boot_partition(part);
    if (err != ESP_OK) {
        set_state(DD_OTA_ERROR, 0, "El firmware subido está dañado");
        return err;
    }
    set_state(DD_OTA_DONE, 100, "Firmware instalado. Reiniciando...");
    return ESP_OK;
}

const char *dd_ota_bad_build(void) { return s_bad_build; }
