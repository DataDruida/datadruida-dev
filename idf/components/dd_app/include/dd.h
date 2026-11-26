/*
 * dd.h — API DataDruida para programas en placas ESP32 (ESP-IDF).
 * Licencia Apache-2.0 — DataDruida (https://app.datadruida.com.ar/developers)
 *
 * Un programa es UN archivo, app.c, con dos funciones:
 *
 *     #include "dd.h"
 *
 *     static dd_sensor_t *th;
 *     static dd_hyst_t extractor = DD_HYST(25.0, 28.0, DD_BAJA);
 *
 *     void dd_setup(void) {             // una vez, al arrancar
 *         th = dd_sensor("slicetex-th-mb-02s", 1);
 *         dd_every(5000);               // dd_loop cada 5 s
 *     }
 *
 *     void dd_loop(void) {              // una y otra vez
 *         dd_reading_t v;
 *         float t = dd_sensor_read(th, &v) ? v.temp : NAN;
 *         dd_relay_set(2, dd_hyst_update(&extractor, t));
 *         dd_log("T %.1f extractor %s", t, dd_relay_get(2) ? "ON" : "OFF");
 *     }
 *
 * Lo que el sistema hace solo (no hace falta programarlo):
 *   - WiFi, actualizaciones por aire y vuelta atrás si el programa no arranca.
 *   - Arranca con todos los relés apagados.
 *   - Hora por internet (Argentina, UTC-3). Sin hora confiable,
 *     dd_in_window() devuelve false: lo que va por horario queda apagado.
 *   - Si dd_loop() se cuelga más de 60 s: apaga los relés y reinicia.
 *   - Re-asienta el chip de relés si un pico de tensión lo resetea.
 *   - Topes de tiempo encendido por relé (dd_relay_max_on).
 *   - Un sensor que no contesta devuelve false; los controles reciben NAN y
 *     apagan. Apagar es siempre lo seguro.
 *
 * Reglas para el programa:
 *   - No bloquear: nada de while(1) ni esperas largas en dd_loop().
 *     Para "cada X tiempo" usar dd_every() o dd_cycle_t.
 *   - Valores que faltan = NAN (usar isnan()).
 *   - Relés y entradas se numeran desde 1, como en la bornera.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <math.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── El programa ─────────────────────────────────────────────────────── */

void dd_setup(void);          /* la escribe el usuario (opcional) */
void dd_loop(void);           /* la escribe el usuario (opcional) */
void dd_every(uint32_t ms);   /* cada cuánto corre dd_loop (por defecto 1000 ms, mínimo 50) */
uint32_t dd_millis(void);     /* ms desde que arrancó la placa */
void dd_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
/* Se ve en el puerto serie y en el panel de la placa (http://dd-xxxxxx.local) */

/* ── Relés (1..dd_relay_count()) ─────────────────────────────────────── */

int  dd_relay_count(void);              /* 0 si la placa no tiene relés */
void dd_relay_set(int ch, bool on);
bool dd_relay_get(int ch);
void dd_relay_all_off(void);
void dd_relay_max_on(int ch, uint32_t seconds);  /* 0 = sin tope */
uint32_t dd_relay_on_for(int ch);       /* segundos que lleva encendido (0 si está apagado) */

/* ── Entradas digitales (1..dd_input_count()) ────────────────────────── */

int  dd_input_count(void);
bool dd_input(int n);         /* true = contacto cerrado */

/* ── Hora ────────────────────────────────────────────────────────────── */

bool dd_time_ok(void);                        /* hay hora confiable */
bool dd_now(int *hour, int *min, int *sec);   /* false si no hay hora */
/* true si la hora actual está entre on y off ("HH:MM"). Cruza medianoche
 * ("20:00"-"08:00" vale). Sin hora confiable: false. */
bool dd_in_window(const char *on, const char *off);

/* ── Sensores RS485 (catálogo DataDruida) ────────────────────────────── */

typedef struct dd_sensor dd_sensor_t;

/* Lo que no mide el sensor queda en NAN. */
typedef struct {
    float temp, hum;                 /* ambiente: °C, % */
    float soil_temp, soil_hum, ec;   /* suelo/sustrato: °C, %, µS/cm */
    float water_temp, ph, orp;       /* agua: °C, pH, mV */
    float tds, salinity;             /* agua: ppm */
    float co2;                       /* ppm */
    float n, p, k;                   /* mg/kg (estimados por el sensor) */
} dd_reading_t;

/* Modelos: "slicetex-th-mb-02s", "slicetex-soil-mb-thc-s",
 * "slicetex-soil-mb-npkphcth-s", "seeed-s-co2-03", "seeed-s-ec-01",
 * "seeed-s-ph-orp-01a", "seeed-s-temp-vwc-ec-04a". addr = ID Modbus.
 * Devuelve NULL si el modelo no existe o la placa no tiene RS485. */
dd_sensor_t *dd_sensor(const char *model, int addr);
bool dd_sensor_read(dd_sensor_t *s, dd_reading_t *out);   /* false = no contestó */
int  dd_sensor_fails(const dd_sensor_t *s);               /* fallas seguidas */

/* Modbus RTU crudo, para sensores fuera del catálogo (bus a 4800 8N1). */
bool dd_modbus_read(int addr, int reg, int count, int fc, int16_t *out);
bool dd_modbus_write(int addr, int reg, uint16_t value);

/* ── Control ─────────────────────────────────────────────────────────── */

typedef enum {
    DD_SUBE = 0,   /* el equipo SUBE la variable (calefactor, humidificador): prende con <= low */
    DD_BAJA = 1,   /* el equipo BAJA la variable (aire, extractor): prende con >= high */
} dd_dir_t;

typedef struct { float low, high; dd_dir_t dir; bool on; } dd_hyst_t;
#define DD_HYST(lo, hi, dir) { (lo), (hi), (dir), false }
/* Termostato/humidistato con banda. Con NAN (sensor caído) devuelve false. */
bool dd_hyst_update(dd_hyst_t *h, float value);

typedef struct { uint32_t on_s, off_s, max_cycles, done; bool phase_on, started; uint32_t since; } dd_cycle_t;
#define DD_CYCLE(on_s, off_s, max) { (on_s), (off_s), (max), 0, true, false, 0 }
/* Pulso/pausa: true durante on_s, false durante off_s. active=false lo reinicia. */
bool dd_cycle_update(dd_cycle_t *c, bool active);

/* Déficit de presión de vapor (kPa), hoja leaf_offset °C bajo el aire. NAN si falta un dato. */
float dd_vpd(float temp, float hum, float leaf_offset);

/* ── Pantalla (sólo placas con pantalla; LVGL 9) ─────────────────────── */

#include "dd_ui.h"

#ifdef __cplusplus
}
#endif
