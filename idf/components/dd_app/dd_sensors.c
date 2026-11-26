/*
 * dd_sensors.c — sensores RS485 del catálogo DataDruida.
 * Mismos mapas de registros que runtime/dd/sensors.py (MicroPython),
 * verificados en hardware salvo donde se indica.
 */
#include <string.h>
#include <stdlib.h>
#include "dd_app_priv.h"

#define BAD(v) ((v) == -32768 || (v) == -32765)   /* Seeed: sensor roto / no soportado */

typedef bool (*read_fn)(int addr, dd_reading_t *o);

struct dd_sensor {
    read_fn fn;
    int addr;
    int fails;
};

static bool th(int a, dd_reading_t *o)            /* SLICETEX TH-MB-02S */
{
    int16_t r[2];
    if (!dd_modbus_read(a, 0, 2, 3, r)) return false;
    o->hum = r[0] / 10.0f; o->temp = r[1] / 10.0f;
    return true;
}

static bool soil_thc(int a, dd_reading_t *o)      /* SLICETEX SOIL-MB-THC-S */
{
    int16_t r[3];
    if (!dd_modbus_read(a, 0, 3, 3, r)) return false;
    o->soil_hum = r[0] / 10.0f; o->soil_temp = r[1] / 10.0f; o->ec = r[2];
    return true;
}

static bool soil_npk(int a, dd_reading_t *o)      /* SLICETEX SOIL-MB-NPKPHCTH-S (pH/NPK sin verificar) */
{
    int16_t r[7];
    if (!dd_modbus_read(a, 0, 7, 3, r)) return false;
    o->soil_hum = r[0] / 10.0f; o->soil_temp = r[1] / 10.0f; o->ec = r[2];
    o->ph = r[3] / 10.0f; o->n = r[4]; o->p = r[5]; o->k = r[6];
    return true;
}

static bool co2(int a, dd_reading_t *o)           /* Seeed S-CO2-03 */
{
    int16_t r[3];
    if (!dd_modbus_read(a, 0, 3, 3, r) || BAD(r[0]) || BAD(r[1]) || BAD(r[2])) return false;
    /* los primeros ~2 min devuelve CO2 = 0: sin dato */
    o->co2 = r[0] > 0 ? r[0] : NAN; o->temp = r[1] / 100.0f; o->hum = r[2] / 100.0f;
    return true;
}

static bool ec(int a, dd_reading_t *o)            /* Seeed S-EC-01 */
{
    int16_t r[5];
    if (!dd_modbus_read(a, 0, 5, 4, r)) return false;
    for (int i = 0; i < 5; i++) if (BAD(r[i])) return false;
    o->water_temp = r[0] / 100.0f; o->ec = r[2]; o->salinity = r[3]; o->tds = r[4];
    return true;
}

static bool ph_orp(int a, dd_reading_t *o)        /* Seeed S-pH&ORP-01A */
{
    int16_t r[3];
    if (!dd_modbus_read(a, 0, 3, 3, r) || BAD(r[0]) || BAD(r[1]) || BAD(r[2])) return false;
    o->water_temp = r[0] / 100.0f; o->ph = r[1] / 100.0f; o->orp = r[2] / 10.0f;
    return true;
}

static bool substrate(int a, dd_reading_t *o)     /* Seeed S-Temp&VWC&EC-04A */
{
    int16_t r[3];
    if (!dd_modbus_read(a, 0, 3, 3, r) && !dd_modbus_read(a, 0, 3, 4, r)) return false;
    if (BAD(r[0]) || BAD(r[1]) || BAD(r[2])) return false;
    o->soil_temp = r[0] / 100.0f; o->soil_hum = r[1] / 100.0f; o->ec = r[2];
    return true;
}

static const struct { const char *model; read_fn fn; } MODELS[] = {
    { "slicetex-th-mb-02s", th },
    { "slicetex-soil-mb-thc-s", soil_thc },
    { "slicetex-soil-mb-npkphcth-s", soil_npk },
    { "seeed-s-co2-03", co2 },
    { "seeed-s-ec-01", ec },
    { "seeed-s-ph-orp-01a", ph_orp },
    { "seeed-s-temp-vwc-ec-04a", substrate },
};

dd_sensor_t *dd_sensor(const char *model, int addr)
{
    if (!model || addr < 1 || addr > 247) return NULL;
    for (size_t i = 0; i < sizeof(MODELS) / sizeof(MODELS[0]); i++) {
        if (!strcmp(model, MODELS[i].model)) {
            dd_sensor_t *s = calloc(1, sizeof(*s));
            if (!s) return NULL;
            s->fn = MODELS[i].fn;
            s->addr = addr;
            return s;
        }
    }
    dd_log("sensor desconocido: \"%s\"", model);
    return NULL;
}

static void clear(dd_reading_t *o)
{
    float *f = (float *)o;
    for (size_t i = 0; i < sizeof(*o) / sizeof(float); i++) f[i] = NAN;
}

bool dd_sensor_read(dd_sensor_t *s, dd_reading_t *out)
{
    dd_reading_t tmp;
    if (!out) out = &tmp;
    clear(out);
    if (!s) return false;
    bool ok = s->fn(s->addr, out);
    if (!ok) { clear(out); s->fails++; }
    else s->fails = 0;
    return ok;
}

int dd_sensor_fails(const dd_sensor_t *s) { return s ? s->fails : 0; }
