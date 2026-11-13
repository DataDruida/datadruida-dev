# dd/extras.py — buzzer y LED RGB de la placa (si los tiene)
# Licencia Apache-2.0 — DataDruida
#
# En una placa sin buzzer o sin LED, las funciones no hacen nada (avisan una
# vez por consola): así el mismo programa corre en cualquier placa.

import time

_warned = set()
_np = None


def _pin(kind):
    from dd import board
    g = board.current().get(kind)
    if g is None and kind not in _warned:
        _warned.add(kind)
        print("[dd] esta placa no tiene", "buzzer" if kind == "buzzer" else "LED RGB")
    return g


def beep(ms=100, times=1, gap_ms=100):
    """Hace sonar el buzzer `times` veces, `ms` milisegundos cada una."""
    g = _pin("buzzer")
    if g is None:
        return
    from machine import Pin
    p = Pin(g, Pin.OUT, value=0)
    for i in range(times):
        p.value(1)
        time.sleep_ms(ms)
        p.value(0)
        if i < times - 1:
            time.sleep_ms(gap_ms)


def led(r, g=None, b=None):
    """Prende el LED RGB: led(255, 0, 0) rojo, led(0) apagado. Valores 0..255."""
    global _np
    pin = _pin("rgb")
    if pin is None:
        return
    if g is None:
        g = b = r
    if _np is None:
        import neopixel
        from machine import Pin
        _np = neopixel.NeoPixel(Pin(pin), 1)
    _np[0] = (int(r) & 255, int(g) & 255, int(b) & 255)
    _np.write()


def battery():
    """Tensión de la batería en volts, o None si la placa no mide batería."""
    from dd import board
    c = board.current().get("battery")
    if not c:
        return None
    try:
        from machine import ADC, Pin
        a = ADC(Pin(c["adc"]))
        a.atten(ADC.ATTN_11DB)
        uv = sum(a.read_uv() for _ in range(8)) / 8
        return round(uv * c["ratio"] / 1e6, 2)
    except Exception:
        return None
