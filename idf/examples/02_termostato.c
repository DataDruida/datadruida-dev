/*
 * Hola Mundo 2 — Termostato con banda
 * Placa: cualquiera con relés · Sensor: SLICETEX TH-MB-02S (ID 1)
 * Relé 2 = extractor: prende con 28 °C o más, apaga con 25 °C o menos.
 * Si el sensor deja de responder, apaga.
 */
#include "dd.h"

static dd_sensor_t *th;
static dd_hyst_t extractor = DD_HYST(25.0, 28.0, DD_BAJA);

void dd_setup(void)
{
    th = dd_sensor("slicetex-th-mb-02s", 1);
    dd_every(5000);
}

void dd_loop(void)
{
    dd_reading_t v;
    float t = dd_sensor_read(th, &v) ? v.temp : NAN;
    dd_relay_set(2, dd_hyst_update(&extractor, t));
    if (isnan(t)) dd_log("sensor sin respuesta: extractor apagado");
    else dd_log("T %.1f °C · extractor %s", t, dd_relay_get(2) ? "ON" : "OFF");
}
