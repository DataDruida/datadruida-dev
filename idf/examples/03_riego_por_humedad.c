/*
 * Hola Mundo 3 — Riego por humedad de suelo, con pulsos
 * Placa: cualquiera con relés · Sensor: SLICETEX SOIL-MB-THC-S (ID 2)
 * Relé 3 = bomba. Con el suelo por debajo de 35 % riega de a pulsos de
 * 20 s con 2 min de pausa (para que el agua baje) hasta superar 45 %.
 * Tope de seguridad: la bomba nunca queda prendida más de 60 s seguidos.
 */
#include "dd.h"

static dd_sensor_t *suelo;
static dd_hyst_t seco = DD_HYST(35.0, 45.0, DD_SUBE);
static dd_cycle_t pulso = DD_CYCLE(20, 120, 0);

void dd_setup(void)
{
    suelo = dd_sensor("slicetex-soil-mb-thc-s", 2);
    dd_relay_max_on(3, 60);
    dd_every(1000);
}

void dd_loop(void)
{
    static uint32_t ultima = 0;
    static float hum = NAN;
    if (dd_millis() - ultima > 10000 || ultima == 0) {   /* el sensor, cada 10 s */
        ultima = dd_millis();
        dd_reading_t v;
        hum = dd_sensor_read(suelo, &v) ? v.soil_hum : NAN;
        dd_log("suelo %.1f %% · bomba %s", hum, dd_relay_get(3) ? "ON" : "OFF");
    }
    bool hay_que_regar = dd_hyst_update(&seco, hum);
    dd_relay_set(3, dd_cycle_update(&pulso, hay_que_regar));
}
