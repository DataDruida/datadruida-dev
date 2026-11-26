/*
 * Pantalla 1 — Hola desde la pantalla (sin sensores)
 * Placa: ESP32-S3-Touch-LCD-4.3B
 * Muestra la hora en grande y un botón que cuenta los toques.
 */
#include "dd.h"

static lv_obj_t *hora;
static lv_obj_t *cuenta;
static int toques;

static void al_tocar(lv_event_t *e)
{
    (void)e;
    toques++;
    lv_label_set_text_fmt(cuenta, "Tocaste %d %s", toques, toques == 1 ? "vez" : "veces");
    dd_log("toque %d", toques);
}

void dd_setup(void)
{
    if (!dd_ui_lock()) return;
    lv_obj_t *root = dd_ui_root();
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(root, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(root, 24, 0);

    hora = lv_label_create(root);
    lv_obj_set_style_text_font(hora, dd_font(28), 0);
    lv_label_set_text(hora, "--:--:--");

    lv_obj_t *btn = lv_button_create(root);
    lv_obj_set_style_pad_all(btn, 20, 0);
    lv_obj_add_event_cb(btn, al_tocar, LV_EVENT_CLICKED, NULL);
    lv_obj_t *t = lv_label_create(btn);
    lv_obj_set_style_text_font(t, dd_font(20), 0);
    lv_label_set_text(t, "Tocame");

    cuenta = lv_label_create(root);
    lv_obj_set_style_text_font(cuenta, dd_font(20), 0);
    lv_label_set_text(cuenta, "Todavía no tocaste");

    dd_ui_show();
    dd_ui_unlock();
    dd_every(1000);
}

void dd_loop(void)
{
    int h, m, s;
    bool ok = dd_now(&h, &m, &s);
    if (!dd_ui_lock()) return;
    if (ok) lv_label_set_text_fmt(hora, "%02d:%02d:%02d", h, m, s);
    else lv_label_set_text(hora, "Esperando la hora...");
    dd_ui_unlock();
}
