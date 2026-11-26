/*
 * Pantalla 2 — Temperatura, humedad y VPD en la pantalla
 * Placa: ESP32-S3-Touch-LCD-4.3B · Sensor: SLICETEX TH-MB-02S (ID 1) en el RS485 de la placa
 */
#include "dd.h"

static dd_sensor_t *th;
static lv_obj_t *temp, *hum, *vpd;

static lv_obj_t *dato(lv_obj_t *root, const char *titulo)
{
    lv_obj_t *col = lv_obj_create(root);
    lv_obj_set_size(col, 220, 160);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_t *t = lv_label_create(col);
    lv_obj_set_style_text_font(t, dd_font(16), 0);
    lv_label_set_text(t, titulo);
    lv_obj_t *v = lv_label_create(col);
    lv_obj_set_style_text_font(v, dd_font(28), 0);
    lv_label_set_text(v, "--");
    return v;
}

void dd_setup(void)
{
    th = dd_sensor("slicetex-th-mb-02s", 1);
    if (dd_ui_lock()) {
        lv_obj_t *root = dd_ui_root();
        lv_obj_set_flex_flow(root, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(root, 20, 0);
        temp = dato(root, "Temperatura");
        hum = dato(root, "Humedad");
        vpd = dato(root, "VPD");
        dd_ui_show();
        dd_ui_unlock();
    }
    dd_every(3000);
}

void dd_loop(void)
{
    dd_reading_t v;
    bool ok = dd_sensor_read(th, &v);   /* fuera del lock: la pantalla no se traba */
    if (!dd_ui_lock()) return;
    if (ok) {
        lv_label_set_text_fmt(temp, "%.1f °C", v.temp);
        lv_label_set_text_fmt(hum, "%.0f %%", v.hum);
        lv_label_set_text_fmt(vpd, "%.2f kPa", dd_vpd(v.temp, v.hum, 2.0f));
    } else {
        lv_label_set_text(temp, "sin sensor");
        lv_label_set_text(hum, "--");
        lv_label_set_text(vpd, "--");
    }
    dd_ui_unlock();
}
