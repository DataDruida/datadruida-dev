/*
 * dd_misc.c — hora (SNTP, Argentina) y piezas de control.
 */
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_netif_sntp.h"
#include "dd_app_priv.h"

/* ── hora ────────────────────────────────────────────────────────────── */

void dd_clock_start(void)
{
    setenv("TZ", "<-03>3", 1);   /* Argentina, sin horario de verano */
    tzset();
    esp_sntp_config_t c = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    c.start = true;   /* reintenta solo cuando haya red */
    esp_netif_sntp_init(&c);
}

bool dd_time_ok(void)
{
    time_t t = time(NULL);
    return t > 1735689600;   /* > 1-ene-2025: la hora vino de internet */
}

bool dd_now(int *h, int *m, int *s)
{
    if (!dd_time_ok()) return false;
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    if (h) *h = tm.tm_hour;
    if (m) *m = tm.tm_min;
    if (s) *s = tm.tm_sec;
    return true;
}

static int hhmm(const char *s)
{
    if (!s) return -1;
    int h = atoi(s);
    const char *c = strchr(s, ':');
    int m = c ? atoi(c + 1) : 0;
    if (h < 0 || h > 24 || m < 0 || m > 59) return -1;
    return h * 60 + m;
}

bool dd_in_window(const char *on, const char *off)
{
    int h, m;
    int a = hhmm(on), b = hhmm(off);
    if (a < 0 || b < 0 || a == b || !dd_now(&h, &m, NULL)) return false;
    int now = h * 60 + m;
    return a < b ? (now >= a && now < b) : (now >= a || now < b);
}

/* ── control ─────────────────────────────────────────────────────────── */

bool dd_hyst_update(dd_hyst_t *h, float v)
{
    if (!h || isnan(v)) { if (h) h->on = false; return false; }
    if (h->dir == DD_SUBE) {
        if (v <= h->low) h->on = true;
        else if (v >= h->high) h->on = false;
    } else {
        if (v >= h->high) h->on = true;
        else if (v <= h->low) h->on = false;
    }
    return h->on;
}

bool dd_cycle_update(dd_cycle_t *c, bool active)
{
    uint32_t now = dd_millis();
    if (!active) { c->done = 0; c->phase_on = true; c->started = false; return false; }
    if (!c->started) { c->started = true; c->since = now; c->phase_on = true; }
    if (c->max_cycles && c->done >= c->max_cycles) return false;
    uint32_t el = (now - c->since) / 1000;
    if (c->phase_on && el >= c->on_s) { c->phase_on = false; c->since = now; c->done++; }
    else if (!c->phase_on && el >= c->off_s) { c->phase_on = true; c->since = now; }
    if (c->max_cycles && c->done >= c->max_cycles) return false;
    return c->phase_on;
}

float dd_vpd(float t, float hum, float leaf)
{
    if (isnan(t) || isnan(hum)) return NAN;
    float tl = t - leaf;
    float es_leaf = 0.6108f * expf(17.27f * tl / (tl + 237.3f));
    float es_air  = 0.6108f * expf(17.27f * t / (t + 237.3f));
    float v = es_leaf - es_air * hum / 100.0f;
    return v > 0 ? v : 0;
}

uint32_t dd_millis(void) { return (uint32_t)(esp_timer_get_time() / 1000); }
