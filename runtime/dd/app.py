# dd/app.py — el bucle principal con las redes de seguridad puestas
# Licencia Apache-2.0 — DataDruida
#
#   import dd
#   def loop():
#       ...            # tu lógica, se llama cada `every_s` segundos
#   dd.run(loop, every_s=5)
#
# Qué hace run() además de llamar a tu función:
#  - Arranca con los 8 relays apagados.
#  - Sincroniza la hora (RTC de la placa + NTP si hay WiFi configurado).
#  - Cada 1 s: re-asienta el chip de relays si se reseteó (heal) y aplica
#    los topes de tiempo encendido.
#  - Watchdog por software: si tu función se cuelga más de `watchdog_s`,
#    apaga todo y reinicia la placa.
#  - Si tu función tira un error: lo imprime, apaga los relays y sigue
#    (después de 3 errores seguidos, se queda con todo apagado).
#  - Ctrl-C (botón "Detener" de la web): apaga todo y vuelve al REPL.

import time

_last_feed = 0


def _wdt_check(_t):
    if time.ticks_diff(time.ticks_ms(), _last_feed) > _wdt_ms:
        try:
            from dd.relays import relays
            relays().all_off()
        except Exception:
            pass
        import machine
        print("[dd] watchdog: el programa se colgó, reinicio")
        machine.reset()


_wdt_ms = 60000


def run(loop, every_s=5, watchdog_s=60, wifi=True):
    global _last_feed, _wdt_ms
    from dd.relays import relays
    from dd import clock, config
    r = relays()
    r.all_off()
    if wifi:
        config.wifi_connect()
    clock.sync()
    print("[dd] hora", "OK" if clock.ok() else "NO confiable (los horarios quedan apagados)")

    _wdt_ms = int(watchdog_s * 1000)
    _last_feed = time.ticks_ms()
    timer = None
    try:
        from machine import Timer
        timer = Timer(0)
        timer.init(period=5000, mode=Timer.PERIODIC, callback=_wdt_check)
    except Exception:
        pass

    errors = 0
    next_loop = time.ticks_ms()
    next_sync = time.ticks_add(time.ticks_ms(), 6 * 3600 * 1000)
    try:
        while True:
            _last_feed = time.ticks_ms()
            if r.heal():
                print("[dd] chip de relays re-asentado (pico de tensión)")
            for n in r.enforce_limits():
                print("[dd] relay %d cortado por tope de tiempo" % n)
            if time.ticks_diff(time.ticks_ms(), next_loop) >= 0:
                next_loop = time.ticks_add(time.ticks_ms(), int(every_s * 1000))
                if errors < 3:
                    try:
                        loop()
                        errors = 0
                    except Exception as e:
                        errors += 1
                        r.all_off()
                        import sys
                        sys.print_exception(e)
                        print("[dd] error %d/3 en tu programa: relays apagados" % errors)
                        if errors >= 3:
                            print("[dd] 3 errores seguidos: me quedo con todo apagado")
            if time.ticks_diff(time.ticks_ms(), next_sync) >= 0:
                next_sync = time.ticks_add(time.ticks_ms(), 6 * 3600 * 1000)
                clock.sync()
            time.sleep_ms(200)
    except KeyboardInterrupt:
        print("[dd] detenido: relays apagados")
    finally:
        if timer:
            timer.deinit()
        r.all_off()
