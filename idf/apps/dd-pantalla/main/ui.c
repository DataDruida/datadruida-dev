/*
 * ui.c — interfaz del firmware de prueba (800x480).
 *
 *   ┌ DataDruida ─────────────────────────── ● Conectado · v0.1.0 ┐
 *   │ Inicio │ WiFi │ Actualizar │ Probar                         │
 *   ├─────────────────────────────────────────────────────────────┤
 *   │  contenido de la pestaña                                    │
 *
 * Regla de hilos: todo lo de acá corre en el task de LVGL. Lo que bloquea
 * (escanear redes) va a un task aparte y deja el resultado en una variable
 * que el timer de refresco levanta. El estado de red/OTA se lee con
 * dd_status_get() cada 500 ms, nunca se empuja desde otros tasks.
 */
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_lv_adapter.h"
#include "lvgl.h"
#include "dd_core.h"
#include "dd.h"
#include "ui.h"

#define C_BG     0x0F1411
#define C_CARD   0x1A211C
#define C_LINE   0x2C352E
#define C_TEXT   0xE8EBE6
#define C_MUTED  0x9AA396
#define C_ACC    0x5FBF83
#define C_WARN   0xE0A04A
#define C_ERR    0xFF8A80

/* Montserrat con acentos y ñ, compilada en flash (idf/tools/gen_fonts.py).
 * ⚠ No volver a TinyTTF: rasterizar letras en el equipo hacía reiniciar la
 * 4.3B la primera vez que aparecían muchas juntas (lista de redes, teclado). */
LV_FONT_DECLARE(dd_font_16)
LV_FONT_DECLARE(dd_font_20)
LV_FONT_DECLARE(dd_font_28)

static const lv_font_t *F16, *F20, *F28;

static lv_obj_t *s_tv;
static lv_obj_t *s_hdr_dot, *s_hdr_txt;
/* Inicio */
static lv_obj_t *s_step2_ico, *s_step2_txt, *s_step2_btn, *s_code_lbl;
static lv_obj_t *s_step1_ico, *s_step1_txt, *s_step1_btn, *s_qr, *s_qr_cap, *s_key_lbl, *s_safe_lbl;
/* WiFi */
static lv_obj_t *s_net_list, *s_scan_btn, *s_wifi_state, *s_ap_btn, *s_ap_info;
static lv_obj_t *s_pw_layer, *s_pw_title, *s_pw_ta;
static char s_pw_ssid[33];
/* Actualizar */
static lv_obj_t *s_ver_lbl, *s_ota_msg, *s_ota_bar, *s_inst_btn, *s_auto_sw, *s_local_lbl;
/* Probar */
static lv_obj_t *s_sys_lbl;
/* Mi programa */
static lv_obj_t *s_prog_tab;
static bool s_prog_claimed;

/* escaneo en segundo plano */
static dd_ap_t s_aps[20];
static volatile int s_scan_n = -1;     // -1 = nada nuevo, >=0 = resultado listo
static volatile bool s_scanning;
static char s_last_qr[64];

/* ── estilos/helpers ─────────────────────────────────────────────────── */

static lv_obj_t *label(lv_obj_t *p, const lv_font_t *f, uint32_t color, const char *txt)
{
    lv_obj_t *l = lv_label_create(p);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_label_set_text(l, txt);
    return l;
}

static lv_obj_t *button(lv_obj_t *p, const char *txt, bool primary, lv_event_cb_t cb, void *ud)
{
    lv_obj_t *b = lv_button_create(p);
    lv_obj_set_style_bg_color(b, lv_color_hex(primary ? C_ACC : C_LINE), 0);
    lv_obj_set_style_radius(b, 10, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_pad_hor(b, 20, 0);
    lv_obj_set_style_pad_ver(b, 12, 0);
    lv_obj_t *l = label(b, F20, primary ? 0x0F1411 : C_TEXT, txt);
    lv_obj_center(l);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
    return b;
}

static void btn_set_text(lv_obj_t *b, const char *t) { lv_label_set_text(lv_obj_get_child(b, 0), t); }

static lv_obj_t *card(lv_obj_t *p)
{
    lv_obj_t *c = lv_obj_create(p);
    lv_obj_set_style_bg_color(c, lv_color_hex(C_CARD), 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_radius(c, 14, 0);
    lv_obj_set_style_pad_all(c, 18, 0);
    return c;
}

static lv_obj_t *column(lv_obj_t *p, int gap)
{
    lv_obj_t *c = lv_obj_create(p);
    lv_obj_remove_style_all(c);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(c, gap, 0);
    return c;
}

static lv_obj_t *row(lv_obj_t *p, int gap)
{
    lv_obj_t *r = lv_obj_create(p);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(r, gap, 0);
    return r;
}

static void goto_tab_cb(lv_event_t *e) { lv_tabview_set_active(s_tv, (uint32_t)(uintptr_t)lv_event_get_user_data(e), LV_ANIM_ON); }

/* ── Inicio ──────────────────────────────────────────────────────────── */

static void step(lv_obj_t *p, const char *num, const char *txt, lv_obj_t **ico_out, lv_obj_t **txt_out)
{
    lv_obj_t *r = row(p, 14);
    lv_obj_t *c = lv_obj_create(r);
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, 36, 36);
    lv_obj_set_style_radius(c, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(c, lv_color_hex(C_LINE), 0);
    lv_obj_t *n = label(c, F20, C_TEXT, num);
    lv_obj_center(n);
    lv_obj_t *t = label(r, F20, C_TEXT, txt);
    lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
    lv_obj_set_flex_grow(t, 1);
    if (ico_out) *ico_out = c;
    if (txt_out) *txt_out = t;
}

static void pair_cb(lv_event_t *e)
{
    (void)e;
    dd_status_t st;
    dd_status_get(&st);
    if (st.pair_code[0]) dd_pair_cancel();
    else dd_pair_start();
}

static void build_home(lv_obj_t *tab)
{
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(tab, 20, 0);

    lv_obj_t *left = column(tab, 18);
    lv_obj_set_size(left, 450, LV_PCT(100));
    // La placa es de Waveshare; lo de DataDruida es el firmware.
    lv_obj_t *ttl = label(left, F28, C_TEXT, "¡Hola! Tu placa ya tiene DataDruida");
    lv_label_set_long_mode(ttl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(ttl, LV_PCT(100));
    lv_obj_t *sub = label(left, F16, C_MUTED,
        "Es el firmware que le permite recibir programas por WiFi: después de este paso no vas a necesitar el cable USB.");
    lv_label_set_long_mode(sub, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(sub, LV_PCT(100));

    step(left, "1", "Conectala a tu red WiFi", &s_step1_ico, &s_step1_txt);
    s_step1_btn = button(left, LV_SYMBOL_WIFI "  Elegir mi WiFi", true, goto_tab_cb, (void *)1);
    step(left, "2", "Vinculala a tu cuenta de app.datadruida.com.ar", &s_step2_ico, &s_step2_txt);
    s_code_lbl = label(left, F28, C_ACC, "");
    lv_obj_set_style_text_letter_space(s_code_lbl, 6, 0);
    lv_obj_add_flag(s_code_lbl, LV_OBJ_FLAG_HIDDEN);
    s_step2_btn = button(left, LV_SYMBOL_LOOP "  Vincular a mi cuenta", true, pair_cb, NULL);
    step(left, "3", "Contale a la IA qué querés que haga: el programa llega solo, por WiFi", NULL, NULL);
    s_safe_lbl = label(left, F16, C_WARN,
        "Modo seguro: la placa se reinició por error varias veces y arrancó sólo con lo básico. Instalá una versión corregida.");
    lv_label_set_long_mode(s_safe_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_safe_lbl, LV_PCT(100));
    lv_obj_add_flag(s_safe_lbl, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *right = card(tab);
    lv_obj_set_size(right, 270, LV_PCT(100));
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(right, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(right, 10, 0);
    lv_obj_remove_flag(right, LV_OBJ_FLAG_SCROLLABLE);
    s_qr = lv_qrcode_create(right);
    lv_qrcode_set_size(s_qr, 170);
    lv_qrcode_set_dark_color(s_qr, lv_color_hex(0x0F1411));
    lv_qrcode_set_light_color(s_qr, lv_color_hex(0xFFFFFF));
    lv_obj_set_style_border_color(s_qr, lv_color_white(), 0);
    lv_obj_set_style_border_width(s_qr, 6, 0);
    s_qr_cap = label(right, F16, C_MUTED, "");
    lv_label_set_long_mode(s_qr_cap, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_qr_cap, LV_PCT(100));
    lv_obj_set_style_text_align(s_qr_cap, LV_TEXT_ALIGN_CENTER, 0);
    label(right, F16, C_MUTED, "Clave del equipo");
    s_key_lbl = label(right, F28, C_ACC, "");
}

/* ── WiFi ────────────────────────────────────────────────────────────── */

static void scan_task(void *arg)
{
    (void)arg;
    int n = dd_wifi_scan(s_aps, 20);
    s_scan_n = n;
    s_scanning = false;
    vTaskDelete(NULL);
}

static void scan_cb(lv_event_t *e)
{
    (void)e;
    if (s_scanning) return;
    s_scanning = true;
    btn_set_text(s_scan_btn, LV_SYMBOL_REFRESH "  Buscando...");
    xTaskCreate(scan_task, "ui_scan", 4096, NULL, 3, NULL);
}

static void pw_close(void) { lv_obj_add_flag(s_pw_layer, LV_OBJ_FLAG_HIDDEN); }
static void pw_cancel_cb(lv_event_t *e) { (void)e; pw_close(); }

static void pw_connect_cb(lv_event_t *e)
{
    (void)e;
    dd_wifi_set(s_pw_ssid, lv_textarea_get_text(s_pw_ta));
    pw_close();
}

static void kb_event_cb(lv_event_t *e)
{
    // La tecla ✓ del teclado conecta; la ✕ cancela.
    if (lv_event_get_code(e) == LV_EVENT_READY) pw_connect_cb(e);
    else if (lv_event_get_code(e) == LV_EVENT_CANCEL) pw_close();
}

static void net_click_cb(lv_event_t *e)
{
    int i = (int)(uintptr_t)lv_event_get_user_data(e);
    strlcpy(s_pw_ssid, s_aps[i].ssid, sizeof(s_pw_ssid));
    if (!s_aps[i].secure) { dd_wifi_set(s_pw_ssid, ""); return; }
    char t[80];
    snprintf(t, sizeof(t), "Contraseña de \"%s\"", s_pw_ssid);
    lv_label_set_text(s_pw_title, t);
    lv_textarea_set_text(s_pw_ta, "");
    lv_obj_remove_flag(s_pw_layer, LV_OBJ_FLAG_HIDDEN);
}

static void ap_cb(lv_event_t *e)
{
    (void)e;
    dd_status_t st;
    dd_status_get(&st);
    dd_setup_ap(!st.setup_ap);
}

static void build_password_layer(void)
{
    s_pw_layer = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_pw_layer);
    lv_obj_set_size(s_pw_layer, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_pw_layer, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_opa(s_pw_layer, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_pw_layer, LV_OBJ_FLAG_CLICKABLE);   // tapa lo de atrás

    s_pw_title = label(s_pw_layer, F20, C_TEXT, "");
    lv_obj_set_pos(s_pw_title, 24, 20);
    s_pw_ta = lv_textarea_create(s_pw_layer);
    lv_textarea_set_one_line(s_pw_ta, true);
    lv_textarea_set_placeholder_text(s_pw_ta, "Escribí la contraseña");
    lv_obj_set_style_text_font(s_pw_ta, F28, 0);
    lv_obj_set_style_bg_color(s_pw_ta, lv_color_hex(C_CARD), 0);
    lv_obj_set_style_text_color(s_pw_ta, lv_color_hex(C_TEXT), 0);
    lv_obj_set_style_border_color(s_pw_ta, lv_color_hex(C_ACC), 0);
    lv_obj_set_size(s_pw_ta, 500, 60);
    lv_obj_set_pos(s_pw_ta, 24, 60);

    lv_obj_t *ok = button(s_pw_layer, "Conectar", true, pw_connect_cb, NULL);
    lv_obj_set_pos(ok, 540, 62);
    lv_obj_t *no = button(s_pw_layer, "Cancelar", false, pw_cancel_cb, NULL);
    lv_obj_set_pos(no, 670, 62);

    lv_obj_t *kb = lv_keyboard_create(s_pw_layer);
    lv_obj_set_size(kb, 800, 320);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_text_font(kb, F20, 0);
    lv_keyboard_set_textarea(kb, s_pw_ta);
    lv_obj_add_event_cb(kb, kb_event_cb, LV_EVENT_ALL, NULL);
    lv_obj_add_flag(s_pw_layer, LV_OBJ_FLAG_HIDDEN);
}

static void build_wifi(lv_obj_t *tab)
{
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(tab, 20, 0);

    lv_obj_t *left = column(tab, 10);
    lv_obj_set_size(left, 400, LV_PCT(100));
    s_scan_btn = button(left, LV_SYMBOL_REFRESH "  Buscar redes", true, scan_cb, NULL);
    s_net_list = lv_list_create(left);
    lv_obj_set_size(s_net_list, LV_PCT(100), 0);
    lv_obj_set_flex_grow(s_net_list, 1);
    lv_obj_set_style_bg_color(s_net_list, lv_color_hex(C_CARD), 0);
    lv_obj_set_style_border_width(s_net_list, 0, 0);
    lv_obj_set_style_radius(s_net_list, 14, 0);

    lv_obj_t *right = column(tab, 14);
    lv_obj_set_size(right, 320, LV_PCT(100));
    label(right, F16, C_MUTED, "Estado");
    s_wifi_state = label(right, F20, C_TEXT, "");
    lv_label_set_long_mode(s_wifi_state, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_wifi_state, LV_PCT(100));
    lv_obj_t *sep = label(right, F16, C_MUTED, "¿Preferís configurarla desde el celular?");
    lv_label_set_long_mode(sep, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(sep, LV_PCT(100));
    s_ap_btn = button(right, "Usar el celular", false, ap_cb, NULL);
    s_ap_info = label(right, F16, C_TEXT, "");
    lv_label_set_long_mode(s_ap_info, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_ap_info, LV_PCT(100));
}

/* ── Actualizar ──────────────────────────────────────────────────────── */

static void ota_check_cb(lv_event_t *e)   { (void)e; dd_ota_check(); }
static void ota_install_cb(lv_event_t *e) { (void)e; dd_ota_install(); }
static void auto_cb(lv_event_t *e)        { dd_ota_set_auto(lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED)); }

static void build_update(lv_obj_t *tab)
{
    lv_obj_t *c = column(tab, 16);
    lv_obj_set_size(c, LV_PCT(100), LV_PCT(100));
    s_ver_lbl = label(c, F28, C_TEXT, "");
    s_ota_msg = label(c, F20, C_MUTED, "");
    s_ota_bar = lv_bar_create(c);
    lv_obj_set_size(s_ota_bar, 600, 14);
    lv_obj_set_style_bg_color(s_ota_bar, lv_color_hex(C_LINE), 0);
    lv_obj_set_style_bg_color(s_ota_bar, lv_color_hex(C_ACC), LV_PART_INDICATOR);
    lv_obj_add_flag(s_ota_bar, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *r = row(c, 12);
    button(r, LV_SYMBOL_REFRESH "  Buscar actualización", false, ota_check_cb, NULL);
    s_inst_btn = button(r, LV_SYMBOL_DOWNLOAD "  Instalar", true, ota_install_cb, NULL);
    lv_obj_add_flag(s_inst_btn, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *r2 = row(c, 12);
    s_auto_sw = lv_switch_create(r2);
    lv_obj_set_style_bg_color(s_auto_sw, lv_color_hex(C_ACC), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_event_cb(s_auto_sw, auto_cb, LV_EVENT_VALUE_CHANGED, NULL);
    label(r2, F20, C_TEXT, "Instalar las actualizaciones solas");

    s_local_lbl = label(c, F16, C_MUTED, "");
    lv_label_set_long_mode(s_local_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_local_lbl, LV_PCT(100));
    lv_obj_t *safe = label(c, F16, C_MUTED,
        "Si una versión nueva no arranca bien, la placa vuelve sola a la anterior.");
    lv_label_set_long_mode(safe, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(safe, LV_PCT(100));
}

/* ── Probar ──────────────────────────────────────────────────────────── */

static lv_obj_t *s_test_layer;
static int s_color_idx;
static const uint32_t TEST_COLORS[] = { 0xFF0000, 0x00FF00, 0x0000FF, 0xFFFFFF, 0x000000 };

static void test_close(void)
{
    if (s_test_layer) { lv_obj_delete(s_test_layer); s_test_layer = NULL; }
}

static lv_obj_t *test_layer(void)
{
    test_close();
    s_test_layer = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_test_layer);
    lv_obj_set_size(s_test_layer, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(s_test_layer, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_test_layer, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s_test_layer, LV_OBJ_FLAG_SCROLLABLE);
    return s_test_layer;
}

static void color_next_cb(lv_event_t *e)
{
    (void)e;
    if (++s_color_idx >= (int)(sizeof(TEST_COLORS) / sizeof(TEST_COLORS[0]))) { test_close(); return; }
    lv_obj_set_style_bg_color(s_test_layer, lv_color_hex(TEST_COLORS[s_color_idx]), 0);
}

static void color_test_cb(lv_event_t *e)
{
    (void)e;
    lv_obj_t *l = test_layer();
    s_color_idx = 0;
    lv_obj_set_style_bg_color(l, lv_color_hex(TEST_COLORS[0]), 0);
    lv_obj_t *h = label(l, F16, 0x808080, "Tocá para pasar al siguiente color");
    lv_obj_align(h, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_add_event_cb(l, color_next_cb, LV_EVENT_CLICKED, NULL);
}

static void touch_draw_cb(lv_event_t *e)
{
    lv_obj_t *l = lv_event_get_target_obj(e);
    if (lv_obj_get_child_count(l) > 400) return;
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    lv_obj_t *d = lv_obj_create(l);
    lv_obj_remove_style_all(d);
    lv_obj_set_size(d, 14, 14);
    lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(d, lv_color_hex(C_ACC), 0);
    lv_obj_set_pos(d, p.x - 7, p.y - 7);
    lv_obj_remove_flag(d, LV_OBJ_FLAG_CLICKABLE);
}

static void test_close_cb(lv_event_t *e) { (void)e; test_close(); }

static void touch_test_cb(lv_event_t *e)
{
    (void)e;
    lv_obj_t *l = test_layer();
    lv_obj_set_style_bg_color(l, lv_color_hex(C_BG), 0);
    lv_obj_t *h = label(l, F20, C_MUTED, "Dibujá con el dedo por toda la pantalla");
    lv_obj_align(h, LV_ALIGN_TOP_MID, 0, 16);
    lv_obj_remove_flag(h, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *b = button(l, "Terminar", true, test_close_cb, NULL);
    lv_obj_align(b, LV_ALIGN_BOTTOM_RIGHT, -16, -16);
    lv_obj_add_event_cb(l, touch_draw_cb, LV_EVENT_PRESSING, NULL);
}

static void build_test(lv_obj_t *tab)
{
    lv_obj_t *c = column(tab, 16);
    lv_obj_set_size(c, LV_PCT(100), LV_PCT(100));
    label(c, F20, C_TEXT, "Probá la pantalla");
    lv_obj_t *r = row(c, 12);
    button(r, "Colores", false, color_test_cb, NULL);
    button(r, "Táctil", false, touch_test_cb, NULL);
    label(c, F16, C_MUTED, "Sistema");
    s_sys_lbl = label(c, F16, C_TEXT, "");
}

/* ── refresco ────────────────────────────────────────────────────────── */

static const char *net_txt(const dd_status_t *s)
{
    switch (s->net) {
    case DD_NET_ONLINE:     return "Conectado";
    case DD_NET_CONNECTING: return "Conectando...";
    case DD_NET_FAILED:     return "No logra conectar";
    default:                return "Sin WiFi";
    }
}

static void refresh_cb(lv_timer_t *t)
{
    (void)t;
    dd_status_t s;
    dd_status_get(&s);
    char b[1024];
    const bool online = s.net == DD_NET_ONLINE;

    // encabezado
    lv_obj_set_style_bg_color(s_hdr_dot, lv_color_hex(online ? C_ACC : s.net == DD_NET_FAILED ? C_ERR : C_MUTED), 0);
    snprintf(b, sizeof(b), "%s%s%s  ·  v%s", net_txt(&s), online ? " a " : "", online ? s.ssid : "", s.fw_version);
    lv_label_set_text(s_hdr_txt, b);

    // Inicio
    lv_obj_set_style_bg_color(s_step1_ico, lv_color_hex(online ? C_ACC : C_LINE), 0);
    lv_label_set_text(lv_obj_get_child(s_step1_ico, 0), online ? LV_SYMBOL_OK : "1");
    if (online) snprintf(b, sizeof(b), "Conectada a %s", s.ssid);
    else snprintf(b, sizeof(b), "Conectala a tu red WiFi");
    lv_label_set_text(s_step1_txt, b);
    if (online) lv_obj_add_flag(s_step1_btn, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(s_step1_btn, LV_OBJ_FLAG_HIDDEN);
    if (s.safe_mode) lv_obj_remove_flag(s_safe_lbl, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s_key_lbl, s.admin_key);

    // Paso 2: vincular a la cuenta
    const bool pairing = s.pair_code[0] != 0;
    lv_obj_set_style_bg_color(s_step2_ico, lv_color_hex(s.paired && !pairing ? C_ACC : C_LINE), 0);
    lv_label_set_text(lv_obj_get_child(s_step2_ico, 0), s.paired && !pairing ? LV_SYMBOL_OK : "2");
    if (pairing) snprintf(b, sizeof(b), "Entrá a app.datadruida.com.ar/developers, \"Mis placas\", y escribí este código (vence en %d min):", (s.pair_left_s + 59) / 60);
    else if (s.paired) snprintf(b, sizeof(b), "Vinculada a %s", s.owner[0] ? s.owner : "tu cuenta");
    else if (s.cloud_msg[0]) snprintf(b, sizeof(b), "%s", s.cloud_msg);
    else snprintf(b, sizeof(b), "Vinculala a tu cuenta de app.datadruida.com.ar");
    lv_label_set_text(s_step2_txt, b);
    lv_label_set_text(s_code_lbl, s.pair_code);
    if (pairing) lv_obj_remove_flag(s_code_lbl, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_code_lbl, LV_OBJ_FLAG_HIDDEN);
    // Sin WiFi no tiene sentido; ya vinculada, el botón no hace falta.
    if (online && !s.paired) lv_obj_remove_flag(s_step2_btn, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_step2_btn, LV_OBJ_FLAG_HIDDEN);
    btn_set_text(s_step2_btn, pairing ? LV_SYMBOL_CLOSE "  Cancelar" : LV_SYMBOL_LOOP "  Vincular a mi cuenta");

    char qr[64];
    if (online) snprintf(qr, sizeof(qr), "http://%s/", s.ip);
    else strlcpy(qr, "https://app.datadruida.com.ar/developers", sizeof(qr));
    if (strcmp(qr, s_last_qr)) {
        strlcpy(s_last_qr, qr, sizeof(s_last_qr));
        lv_qrcode_update(s_qr, qr, strlen(qr));
        if (online) snprintf(b, sizeof(b), "Panel de la placa:\nhttp://%s.local", s.hostname);
        else snprintf(b, sizeof(b), "Escaneá para conocer DataDruida Developers");
        lv_label_set_text(s_qr_cap, b);
    }

    // WiFi
    switch (s.net) {
    case DD_NET_ONLINE:     snprintf(b, sizeof(b), "Conectada a %s\nIP %s · señal %d dBm", s.ssid, s.ip, s.rssi); break;
    case DD_NET_CONNECTING: snprintf(b, sizeof(b), "Conectando a %s...", s.ssid); break;
    case DD_NET_FAILED:     snprintf(b, sizeof(b), "No logra conectar a %s. Revisá la contraseña o elegí otra red.", s.ssid); break;
    default:                snprintf(b, sizeof(b), "Todavía no elegiste una red. Tocá \"Buscar redes\"."); break;
    }
    lv_label_set_text(s_wifi_state, b);
    btn_set_text(s_ap_btn, s.setup_ap ? "Cerrar la red de configuración" : "Usar el celular");
    if (s.setup_ap) snprintf(b, sizeof(b), "Con el celular, conectate a la red \"%s\". Se abre sola una página para elegir tu WiFi.", s.ap_ssid);
    else b[0] = '\0';
    lv_label_set_text(s_ap_info, b);

    if (s_scan_n >= 0) {
        lv_obj_clean(s_net_list);
        for (int i = 0; i < s_scan_n; i++) {
            snprintf(b, sizeof(b), "%s%s", s_aps[i].ssid, s_aps[i].secure ? "" : "  (abierta)");
            lv_obj_t *it = lv_list_add_button(s_net_list, LV_SYMBOL_WIFI, b);
            lv_obj_set_style_text_font(it, F20, 0);
            lv_obj_set_style_bg_color(it, lv_color_hex(C_CARD), 0);
            lv_obj_set_style_text_color(it, lv_color_hex(C_TEXT), 0);
            lv_obj_set_style_pad_ver(it, 14, 0);
            lv_obj_add_event_cb(it, net_click_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
        }
        if (s_scan_n == 0) lv_list_add_text(s_net_list, "No encontré redes. Probá de nuevo.");
        s_scan_n = -1;
        btn_set_text(s_scan_btn, LV_SYMBOL_REFRESH "  Buscar redes");
    }

    // Actualizar
    snprintf(b, sizeof(b), "Versión instalada: %s%s", s.fw_version, s.pending_verify ? " (a prueba)" : "");
    lv_label_set_text(s_ver_lbl, b);
    lv_label_set_text(s_ota_msg, s.ota_msg[0] ? s.ota_msg : (online ? "" : "Conectá la placa al WiFi para buscar actualizaciones."));
    lv_obj_set_style_text_color(s_ota_msg, lv_color_hex(s.ota == DD_OTA_ERROR ? C_ERR : C_MUTED), 0);
    if (s.ota == DD_OTA_AVAILABLE) lv_obj_remove_flag(s_inst_btn, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_inst_btn, LV_OBJ_FLAG_HIDDEN);
    if (s.ota == DD_OTA_DOWNLOADING || s.ota == DD_OTA_DONE) {
        lv_obj_remove_flag(s_ota_bar, LV_OBJ_FLAG_HIDDEN);
        lv_bar_set_value(s_ota_bar, s.ota_progress, LV_ANIM_OFF);
    } else {
        lv_obj_add_flag(s_ota_bar, LV_OBJ_FLAG_HIDDEN);
    }
    if (s.auto_update != lv_obj_has_state(s_auto_sw, LV_STATE_CHECKED)) {
        if (s.auto_update) lv_obj_add_state(s_auto_sw, LV_STATE_CHECKED);
        else lv_obj_remove_state(s_auto_sw, LV_STATE_CHECKED);
    }
    if (online) snprintf(b, sizeof(b), "También podés subir un firmware (.bin) desde la compu en http://%s.local (clave: %s).", s.hostname, s.admin_key);
    else b[0] = '\0';
    lv_label_set_text(s_local_lbl, b);

    // Probar
    const uint32_t up = (uint32_t)(esp_timer_get_time() / 1000000);
    snprintf(b, sizeof(b),
             "Equipo %s · placa %s\nEncendida hace %02u:%02u:%02u\nRAM libre %u KB · PSRAM libre %u KB\n%s",
             s.device_id, s.board_id, (unsigned)(up / 3600), (unsigned)(up / 60 % 60), (unsigned)(up % 60),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
             online ? s.ip : "sin red");
    lv_label_set_text(s_sys_lbl, b);
}

/* ── API para el programa del usuario (dd_ui.h) ─────────────────────── */

lv_obj_t *dd_ui_root(void)
{
    // La primera vez que el programa pide la pantalla, se saca el texto de
    // "todavía no hay programa" y la pestaña queda en blanco para él.
    if (s_prog_tab && !s_prog_claimed) {
        s_prog_claimed = true;
        lv_obj_clean(s_prog_tab);
    }
    return s_prog_tab;
}

bool dd_ui_lock(void)   { return esp_lv_adapter_lock(1000) == ESP_OK; }
void dd_ui_unlock(void) { esp_lv_adapter_unlock(); }
const lv_font_t *dd_font(int px) { return px <= 16 ? F16 : px <= 20 ? F20 : F28; }
void dd_ui_show(void)   { if (s_tv) lv_tabview_set_active(s_tv, 4, LV_ANIM_OFF); }

static void build_program(lv_obj_t *tab)
{
    lv_obj_t *c = column(tab, 12);
    lv_obj_set_size(c, LV_PCT(100), LV_PCT(100));
    label(c, F20, C_TEXT, "Todavía no hay un programa en esta placa");
    lv_obj_t *t = label(c, F16, C_MUTED,
        "Cuando le pidas algo a la IA en app.datadruida.com.ar/developers, el programa llega por WiFi "
        "y se muestra en esta pestaña.");
    lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(t, LV_PCT(100));
}

/* ── armado ──────────────────────────────────────────────────────────── */

void ui_build(void)
{
    F16 = &dd_font_16;
    F20 = &dd_font_20;
    F28 = &dd_font_28;

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(C_BG), 0);
    lv_obj_set_style_text_color(scr, lv_color_hex(C_TEXT), 0);
    lv_obj_set_style_text_font(scr, F16, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    // encabezado
    lv_obj_t *hdr = lv_obj_create(scr);
    lv_obj_remove_style_all(hdr);
    lv_obj_set_size(hdr, 800, 48);
    lv_obj_set_style_pad_hor(hdr, 20, 0);
    lv_obj_t *brand = label(hdr, F20, C_ACC, "DataDruida");
    lv_obj_align(brand, LV_ALIGN_LEFT_MID, 0, 0);
    // Punto + texto en una fila flex anclada a la derecha: el texto cambia de
    // largo y el punto tiene que correrse con él (alineado una sola vez al
    // arrancar quedaba encima del número de versión).
    lv_obj_t *st = lv_obj_create(hdr);
    lv_obj_remove_style_all(st);
    lv_obj_set_size(st, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(st, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(st, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(st, 8, 0);
    lv_obj_align(st, LV_ALIGN_RIGHT_MID, 0, 0);
    s_hdr_dot = lv_obj_create(st);
    lv_obj_remove_style_all(s_hdr_dot);
    lv_obj_set_size(s_hdr_dot, 10, 10);
    lv_obj_set_style_radius(s_hdr_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(s_hdr_dot, LV_OPA_COVER, 0);
    s_hdr_txt = label(st, F16, C_MUTED, "");

    // pestañas
    s_tv = lv_tabview_create(scr);
    lv_obj_set_size(s_tv, 800, 432);
    lv_obj_set_pos(s_tv, 0, 48);
    lv_tabview_set_tab_bar_size(s_tv, 52);
    lv_obj_set_style_bg_color(s_tv, lv_color_hex(C_BG), 0);
    lv_obj_t *bar = lv_tabview_get_tab_bar(s_tv);
    lv_obj_set_style_bg_color(bar, lv_color_hex(C_CARD), 0);
    lv_obj_set_style_text_font(bar, F20, 0);
    lv_obj_set_style_text_color(bar, lv_color_hex(C_MUTED), 0);

    const char *names[] = { "Inicio", "WiFi", "Actualizar", "Probar", "Mi programa" };
    lv_obj_t *tabs[5];
    for (int i = 0; i < 5; i++) {
        tabs[i] = lv_tabview_add_tab(s_tv, names[i]);
        lv_obj_set_style_pad_all(tabs[i], 20, 0);
        lv_obj_t *tb = lv_obj_get_child(bar, i);
        lv_obj_set_style_text_color(tb, lv_color_hex(C_ACC), LV_STATE_CHECKED);
        lv_obj_set_style_border_color(tb, lv_color_hex(C_ACC), LV_STATE_CHECKED);
        lv_obj_set_style_bg_opa(tb, LV_OPA_TRANSP, LV_STATE_CHECKED);
    }
    build_home(tabs[0]);
    build_wifi(tabs[1]);
    build_update(tabs[2]);
    build_test(tabs[3]);
    s_prog_tab = tabs[4];
    build_program(tabs[4]);
    build_password_layer();

    refresh_cb(NULL);
    lv_timer_create(refresh_cb, 500, NULL);
}
