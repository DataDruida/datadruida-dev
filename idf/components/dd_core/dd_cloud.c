/*
 * dd_cloud.c — vinculación con la cuenta de app.datadruida.com.ar y
 * programas mandados desde /developers.
 *
 * Vincular: la placa muestra un código de 6 letras (10 min) y lo manda a la
 * nube; el usuario lo escribe en /developers → "Mis placas". Desde ese momento
 * la nube conoce la placa por su MAC (uid) y un secreto que se generó en el
 * primer arranque (sólo viaja a la nube, que guarda su hash).
 *
 *   POST DD_SYNC_URL
 *     { id, secret, app, board, version, build, bad_build, code? }
 *   → 200 { paired, owner, update: { url, version, build? } | null }
 *   → 403 { error }   la placa figura vinculada a otra cuenta / con otro secreto
 *   → 409             el código ya lo está usando otra placa: se genera otro
 *
 * Vinculada, consulta cada 1 min: cuando el usuario manda un programa, la
 * placa lo instala en menos de un minuto (con la misma vuelta atrás de
 * siempre si el programa no arranca).
 */
#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"

#include "dd_internal.h"

static const char *TAG = "dd_cloud";

#define PAIR_WINDOW_S   600
#define POLL_PAIRED_MS  60000
#define POLL_PAIRING_MS 4000

static int64_t s_pair_until_us;

static void set_msg(const char *m)
{
    dd_lock(); strlcpy(g_dd.cloud_msg, m ? m : "", sizeof(g_dd.cloud_msg)); dd_unlock();
}

static void new_code(void)
{
    // Sin ambiguos (0/O, 1/I): se lee en una pantalla y se tipea.
    static const char abc[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    char c[7];
    for (int i = 0; i < 6; i++) c[i] = abc[esp_random() % (sizeof(abc) - 1)];
    c[6] = '\0';
    dd_lock(); strlcpy(g_dd.pair_code, c, sizeof(g_dd.pair_code)); dd_unlock();
}

void dd_pair_start(void)
{
    new_code();
    s_pair_until_us = esp_timer_get_time() + (int64_t)PAIR_WINDOW_S * 1000000;
    dd_lock(); g_dd.pair_left_s = PAIR_WINDOW_S; dd_unlock();
    set_msg(g_dd.net == DD_NET_ONLINE ? "Escribí el código en app.datadruida.com.ar/developers"
                                      : "Conectá la placa al WiFi para vincularla");
    ESP_LOGI(TAG, "Código para vincular: %s", g_dd.pair_code);
    dd_ota_check();   // despierta la tarea: el código llega a la nube enseguida
}

void dd_pair_cancel(void)
{
    s_pair_until_us = 0;
    dd_lock(); g_dd.pair_code[0] = '\0'; g_dd.pair_left_s = 0; dd_unlock();
}

static void set_paired(bool paired, const char *owner)
{
    bool changed;
    dd_lock();
    changed = g_dd.paired != paired || strcmp(g_dd.owner, owner ? owner : "");
    g_dd.paired = paired;
    strlcpy(g_dd.owner, owner ? owner : "", sizeof(g_dd.owner));
    dd_unlock();
    if (changed) {
        dd_nvs_set_u8("paired", paired ? 1 : 0);
        dd_nvs_set_str("owner", owner ? owner : "");
        if (paired) ESP_LOGI(TAG, "Vinculada a %s", owner ? owner : "");
        else ESP_LOGI(TAG, "Desvinculada");
    }
}

/* POST JSON → status HTTP y body en buf (o -1 si no hubo respuesta). */
static int post_json(const char *body, char *buf, int max)
{
    esp_http_client_config_t hc = {
        .url = DD_SYNC_URL,
        .method = HTTP_METHOD_POST,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 10000,
    };
    esp_http_client_handle_t c = esp_http_client_init(&hc);
    if (!c) return -1;
    esp_http_client_set_header(c, "Content-Type", "application/json");
    int code = -1, n = 0;
    const int len = strlen(body);
    if (esp_http_client_open(c, len) == ESP_OK && esp_http_client_write(c, body, len) == len) {
        esp_http_client_fetch_headers(c);
        code = esp_http_client_get_status_code(c);
        int r;
        while (n < max - 1 && (r = esp_http_client_read(c, buf + n, max - 1 - n)) > 0) n += r;
    }
    buf[n] = '\0';
    esp_http_client_cleanup(c);
    return code;
}

static void do_sync(bool pairing)
{
    dd_status_t st;
    dd_status_get(&st);
    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "id", st.uid);
    cJSON_AddStringToObject(j, "secret", g_dd_secret);
    cJSON_AddStringToObject(j, "app", st.app_name);
    cJSON_AddStringToObject(j, "board", st.board_id);
    cJSON_AddStringToObject(j, "version", st.fw_version);
    cJSON_AddStringToObject(j, "build", st.build);
    cJSON_AddStringToObject(j, "bad_build", dd_ota_bad_build());
    cJSON_AddBoolToObject(j, "pending_verify", st.pending_verify);
    cJSON_AddBoolToObject(j, "safe_mode", st.safe_mode);
    cJSON_AddStringToObject(j, "ip", st.ip);
    if (pairing) cJSON_AddStringToObject(j, "code", st.pair_code);
    char *body = cJSON_PrintUnformatted(j);
    cJSON_Delete(j);
    if (!body) return;

    char *buf = malloc(2048);
    int code = buf ? post_json(body, buf, 2048) : -1;
    cJSON_free(body);
    if (code < 0) {
        free(buf);
        set_msg("No pude hablar con app.datadruida.com.ar");
        return;
    }
    cJSON *r = cJSON_Parse(buf);
    free(buf);
    const cJSON *err = cJSON_GetObjectItem(r, "error");

    if (code == 409 && pairing) {
        new_code();   // otro equipo mostró el mismo código: cambiar y seguir
    } else if (code == 403) {
        // La nube no reconoce a esta placa como la vinculada (otro secreto):
        // queda desvinculada acá; el dueño la libera desde la web.
        set_paired(false, "");
        dd_pair_cancel();
        set_msg(cJSON_IsString(err) ? err->valuestring : "Esta placa figura vinculada a otra cuenta");
    } else if (code == 200) {
        const cJSON *p = cJSON_GetObjectItem(r, "paired");
        const cJSON *o = cJSON_GetObjectItem(r, "owner");
        const bool paired = cJSON_IsTrue(p);
        set_paired(paired, cJSON_IsString(o) ? o->valuestring : "");
        if (paired && pairing) {
            dd_pair_cancel();
            char m[96];
            snprintf(m, sizeof(m), "Vinculada a %s", cJSON_IsString(o) ? o->valuestring : "tu cuenta");
            set_msg(m);
        } else if (!paired && !pairing) {
            set_msg("La placa se desvinculó de la cuenta");
        } else if (paired) {
            set_msg("");
        }
        const cJSON *u = cJSON_GetObjectItem(r, "update");
        if (paired && cJSON_IsObject(u)) {
            const cJSON *url = cJSON_GetObjectItem(u, "url");
            const cJSON *ver = cJSON_GetObjectItem(u, "version");
            const cJSON *bld = cJSON_GetObjectItem(u, "build");
            if (cJSON_IsString(url) && cJSON_IsString(ver)) {
                dd_ota_offer(url->valuestring, ver->valuestring, cJSON_IsString(bld) ? bld->valuestring : NULL);
            }
        }
    } else {
        ESP_LOGW(TAG, "sync: HTTP %d", code);
        set_msg(cJSON_IsString(err) ? err->valuestring : "La nube respondió con un error");
    }
    cJSON_Delete(r);
}

TickType_t dd_cloud_tick(void)
{
    bool pairing = false;
    if (s_pair_until_us) {
        const int64_t left = (s_pair_until_us - esp_timer_get_time()) / 1000000;
        if (left <= 0) {
            dd_pair_cancel();
            set_msg("El código venció: generá otro para vincular");
        } else {
            dd_lock(); g_dd.pair_left_s = (int)left; dd_unlock();
            pairing = true;
        }
    }
    if (!g_dd.paired && !pairing) return 0;
    if (g_dd.net != DD_NET_ONLINE) return pdMS_TO_TICKS(10000);
    do_sync(pairing);
    if (s_pair_until_us) return pdMS_TO_TICKS(POLL_PAIRING_MS);
    return g_dd.paired ? pdMS_TO_TICKS(POLL_PAIRED_MS) : 0;
}

void dd_cloud_init(void)
{
    if (g_dd.paired) ESP_LOGI(TAG, "Vinculada a %s", g_dd.owner);
}
