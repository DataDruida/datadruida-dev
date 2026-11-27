/*
 * dd_core.c — arranque, NVS, modo seguro y validación de la imagen (rollback).
 */
#include <string.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_app_desc.h"
#include "esp_ota_ops.h"
#include "esp_attr.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "driver/gpio.h"

#include "dd_internal.h"

static const char *TAG = "dd_core";

dd_status_t      g_dd;
char g_dd_secret[33];
dd_core_config_t g_dd_cfg;
static SemaphoreHandle_t s_mutex;

void dd_lock(void)   { xSemaphoreTake(s_mutex, portMAX_DELAY); }
void dd_unlock(void) { xSemaphoreGive(s_mutex); }

void dd_status_get(dd_status_t *out)
{
    dd_lock();
    *out = g_dd;
    dd_unlock();
}

bool dd_safe_mode(void) { return g_dd.safe_mode; }

/* ── NVS ─────────────────────────────────────────────────────────────── */

esp_err_t dd_nvs_get_str(const char *key, char *out, size_t len)
{
    nvs_handle_t h;
    out[0] = '\0';
    esp_err_t err = nvs_open("dd", NVS_READONLY, &h);
    if (err != ESP_OK) return err;
    err = nvs_get_str(h, key, out, &len);
    nvs_close(h);
    if (err != ESP_OK) out[0] = '\0';
    return err;
}

esp_err_t dd_nvs_set_str(const char *key, const char *val)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open("dd", NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = (val && val[0]) ? nvs_set_str(h, key, val) : nvs_erase_key(h, key);
    if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

uint8_t dd_nvs_get_u8(const char *key, uint8_t def)
{
    nvs_handle_t h;
    uint8_t v = def;
    if (nvs_open("dd", NVS_READONLY, &h) == ESP_OK) {
        if (nvs_get_u8(h, key, &v) != ESP_OK) v = def;
        nvs_close(h);
    }
    return v;
}

esp_err_t dd_nvs_set_u8(const char *key, uint8_t val)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open("dd", NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_u8(h, key, val);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t dd_board_set(const char *board_id)
{
    esp_err_t err = dd_nvs_set_str("board", board_id);
    if (err == ESP_OK) {
        dd_lock();
        strlcpy(g_dd.board_id, board_id ? board_id : "", sizeof(g_dd.board_id));
        dd_unlock();
    }
    return err;
}

/* ── Modo seguro por reinicios en cadena ─────────────────────────────────
 * Un contador en RTC (sobrevive a los reinicios por software/panic, se borra
 * al cortar la corriente). Si el equipo se cae 3 veces seguidas antes de
 * cumplir 60 s andando, el 4º arranque es en modo seguro: sin la app.
 */
#define DD_CRASH_MAGIC  0xDD5AFE01u
#define DD_CRASH_LIMIT  3
static RTC_NOINIT_ATTR uint32_t s_crash_magic;
static RTC_NOINIT_ATTR uint32_t s_crash_count;

static bool reset_is_crash(esp_reset_reason_t r)
{
    return r == ESP_RST_PANIC || r == ESP_RST_INT_WDT || r == ESP_RST_TASK_WDT ||
           r == ESP_RST_WDT || r == ESP_RST_BROWNOUT;
}

static void stable_timer_cb(void *arg)
{
    (void)arg;
    s_crash_count = 0;   // 60 s sin caerse: se limpia el contador
}

static bool boot_button_held(int gpio)
{
    if (gpio < 0) return false;
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << gpio,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&io);
    vTaskDelay(pdMS_TO_TICKS(20));
    if (gpio_get_level(gpio) != 0) return false;
    ESP_LOGW(TAG, "BOOT apretado: mantener 3 s para modo seguro");
    for (int i = 0; i < 30; i++) {
        vTaskDelay(pdMS_TO_TICKS(100));
        if (gpio_get_level(gpio) != 0) return false;
    }
    return true;
}

/* ── Validación de la imagen nueva (rollback) ────────────────────────────
 * Con CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE una imagen recién instalada por
 * OTA arranca en estado PENDING_VERIFY. Si se reinicia sin marcarse válida,
 * el bootloader vuelve a la anterior. Criterio para darla por buena:
 *   - conectó al WiFi y anduvo 20 s más, o
 *   - no hay red guardada (no hay con qué probarla) y anduvo 60 s.
 * Si hay red guardada y en 3 minutos no conecta, se reinicia → rollback: una
 * imagen que rompe el WiFi rompería también la única vía de arreglarla.
 */
static void verify_task(void *arg)
{
    (void)arg;
    const int64_t t0 = esp_timer_get_time();
    int64_t online_since = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        dd_status_t st;
        dd_status_get(&st);
        const int64_t el = (esp_timer_get_time() - t0) / 1000000;

        bool ok = false;
        if (st.net == DD_NET_ONLINE) {
            if (!online_since) online_since = esp_timer_get_time();
            if ((esp_timer_get_time() - online_since) / 1000000 >= 20) ok = true;
        } else {
            online_since = 0;
            if (!st.ssid[0] && el >= 60) ok = true;
        }
        if (ok) {
            esp_ota_mark_app_valid_cancel_rollback();
            ESP_LOGI(TAG, "Imagen nueva verificada: queda instalada");
            dd_lock(); g_dd.pending_verify = false; dd_unlock();
            vTaskDelete(NULL);
        }
        if (st.ssid[0] && el >= 180) {
            ESP_LOGE(TAG, "La imagen nueva no logró conectar en 3 min: vuelvo a la anterior");
            esp_ota_mark_app_invalid_rollback_and_reboot();
        }
    }
}

/* ── Arranque ────────────────────────────────────────────────────────── */

static void make_admin_key(char *out, size_t len)
{
    // Sin letras ambiguas (0/O, 1/l/I): se lee en una pantalla y se tipea.
    static const char abc[] = "abcdefghjkmnpqrstuvwxyz23456789";
    size_t n = 8 < len - 1 ? 8 : len - 1;
    for (size_t i = 0; i < n; i++) out[i] = abc[esp_random() % (sizeof(abc) - 1)];
    out[n] = '\0';
}

esp_err_t dd_core_start(const dd_core_config_t *cfg)
{
    s_mutex = xSemaphoreCreateMutex();
    g_dd_cfg = *cfg;
    memset(&g_dd, 0, sizeof(g_dd));

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS sin formato: se borra");
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    const esp_app_desc_t *app = esp_app_get_description();
    strlcpy(g_dd.fw_version, app->version, sizeof(g_dd.fw_version));
    strlcpy(g_dd.app_name, cfg->app_name, sizeof(g_dd.app_name));

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(g_dd.device_id, sizeof(g_dd.device_id), "%02x%02x%02x", mac[3], mac[4], mac[5]);
    snprintf(g_dd.hostname, sizeof(g_dd.hostname), "dd-%s", g_dd.device_id);
    snprintf(g_dd.ap_ssid, sizeof(g_dd.ap_ssid), "DataDruida-%02X%02X", mac[4], mac[5]);
    snprintf(g_dd.uid, sizeof(g_dd.uid), "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    // Cada compilación tiene un SHA-256 de ELF distinto: es lo que la nube usa
    // para saber qué programa tiene la placa (la versión es la de la base y se
    // repite en todos los programas).
    esp_app_get_elf_sha256(g_dd.build, 17);

    // Secreto del equipo para la nube: se genera una vez y no sale de la placa
    // salvo hacia app.datadruida.com.ar (que guarda sólo su hash).
    if (dd_nvs_get_str("secret", g_dd_secret, sizeof(g_dd_secret)) != ESP_OK || strlen(g_dd_secret) != 32) {
        for (int i = 0; i < 16; i++) snprintf(g_dd_secret + i * 2, 3, "%02x", (unsigned)(esp_random() & 0xff));
        dd_nvs_set_str("secret", g_dd_secret);
    }
    g_dd.paired = dd_nvs_get_u8("paired", 0) != 0;
    dd_nvs_get_str("owner", g_dd.owner, sizeof(g_dd.owner));

    if (dd_nvs_get_str("key", g_dd.admin_key, sizeof(g_dd.admin_key)) != ESP_OK ||
        strlen(g_dd.admin_key) < 6) {
        make_admin_key(g_dd.admin_key, sizeof(g_dd.admin_key));
        dd_nvs_set_str("key", g_dd.admin_key);
        ESP_LOGI(TAG, "Clave del equipo generada");
    }

    if (cfg->board_id) {
        strlcpy(g_dd.board_id, cfg->board_id, sizeof(g_dd.board_id));
    } else {
        dd_nvs_get_str("board", g_dd.board_id, sizeof(g_dd.board_id));
    }
    g_dd.auto_update = dd_nvs_get_u8("autoupd", cfg->auto_update_default ? 1 : 0) != 0;

    // Modo seguro
    const esp_reset_reason_t rr = esp_reset_reason();
    if (s_crash_magic != DD_CRASH_MAGIC) { s_crash_magic = DD_CRASH_MAGIC; s_crash_count = 0; }
    if (reset_is_crash(rr)) s_crash_count++;
    else if (rr == ESP_RST_POWERON) s_crash_count = 0;
    bool safe = s_crash_count >= DD_CRASH_LIMIT;
    if (safe) ESP_LOGE(TAG, "%u reinicios por error seguidos: MODO SEGURO", (unsigned)s_crash_count);
    if (!safe && boot_button_held(cfg->boot_button_gpio)) safe = true;
    g_dd.safe_mode = safe;
    if (safe) s_crash_count = 0;   // el próximo arranque normal vuelve a intentar la app

    const esp_timer_create_args_t targs = { .callback = stable_timer_cb, .name = "dd_stable" };
    esp_timer_handle_t t;
    if (esp_timer_create(&targs, &t) == ESP_OK) esp_timer_start_once(t, 60 * 1000000ULL);

    // ¿Imagen a prueba?
    esp_ota_img_states_t ost;
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (esp_ota_get_state_partition(running, &ost) == ESP_OK && ost == ESP_OTA_IMG_PENDING_VERIFY) {
        g_dd.pending_verify = true;
        ESP_LOGW(TAG, "Imagen nueva a prueba (%s)", g_dd.fw_version);
        xTaskCreate(verify_task, "dd_verify", 3072, NULL, 3, NULL);
    }

    ESP_LOGI(TAG, "%s %s | placa %s | equipo %s | modo seguro %s",
             g_dd.app_name, g_dd.fw_version, g_dd.board_id[0] ? g_dd.board_id : "(sin elegir)",
             g_dd.hostname, safe ? "SI" : "no");

    ESP_ERROR_CHECK(dd_ota_init());
    dd_cloud_init();
    ESP_ERROR_CHECK(dd_wifi_init());
    ESP_ERROR_CHECK(dd_web_start());

    // En modo seguro siempre queda el portal abierto: es la vía de rescate.
    if (safe) dd_setup_ap(true);
    return ESP_OK;
}
