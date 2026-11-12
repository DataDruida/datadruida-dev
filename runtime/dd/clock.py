# dd/clock.py — hora local: RTC PCF85063 de la placa + NTP si hay WiFi
# Licencia Apache-2.0 — DataDruida
#
# El PCF85063 guarda hora LOCAL. Sin pila, después de un corte de luz queda
# con basura o con la hora del corte: por eso se valida antes de usarla y se
# corrige por NTP apenas hay red.

import time

TZ_OFFSET_H = -3   # Argentina
_ok = False


def _bcd(b):
    return (b >> 4) * 10 + (b & 0x0F)


def _tobcd(v):
    return ((v // 10) << 4) | (v % 10)


def _rtc_addr():
    from dd import board
    return board.current().get("rtc", {}).get("addr", 0x51)


def read_rtc():
    """(año, mes, día, hora, min, seg) del PCF85063, o None si no es creíble."""
    from dd import board
    try:
        r = board.i2c().readfrom_mem(_rtc_addr(), 0x04, 7)
    except OSError:
        return None
    if r[0] & 0x80:        # bit OS: el oscilador se detuvo, hora no confiable
        return None
    s, m, h = _bcd(r[0] & 0x7F), _bcd(r[1] & 0x7F), _bcd(r[2] & 0x3F)
    d, mo, y = _bcd(r[3] & 0x3F), _bcd(r[5] & 0x1F), 2000 + _bcd(r[6])
    if not (2025 <= y <= 2099 and 1 <= mo <= 12 and 1 <= d <= 31 and h < 24 and m < 60):
        return None
    return (y, mo, d, h, m, s)


def write_rtc(t):
    from dd import board
    y, mo, d, h, m, s = t[:6]
    buf = bytes([_tobcd(s), _tobcd(m), _tobcd(h), _tobcd(d), 0, _tobcd(mo), _tobcd(y - 2000)])
    board.i2c().writeto_mem(_rtc_addr(), 0x04, buf)


def _set_system(t):
    import machine
    y, mo, d, h, m, s = t[:6]
    machine.RTC().datetime((y, mo, d, 0, h, m, s, 0))


def sync():
    """Toma la hora del RTC de la placa y, si hay WiFi, la corrige por NTP.
    Devuelve True si la hora del sistema es confiable."""
    global _ok
    t = read_rtc()
    if t:
        _set_system(t)
        _ok = True
    try:
        import network
        if network.WLAN(network.STA_IF).isconnected():
            import ntptime
            ntptime.settime()  # UTC
            lt = time.localtime(time.time() + TZ_OFFSET_H * 3600)
            _set_system(lt)
            write_rtc(lt)
            _ok = True
    except Exception:
        pass
    return _ok


def ok():
    """True si la hora es confiable (RTC válido o NTP)."""
    return _ok


def now():
    """(hora, minuto) local."""
    t = time.localtime()
    return t[3], t[4]


def minutes(hhmm):
    """'08:30' -> 510"""
    h, m = hhmm.split(":")
    return int(h) * 60 + int(m)


def in_window(on, off, at=None):
    """True si la hora actual está entre on y off ('HH:MM'). Cruza medianoche.
    Si on == off, la ventana está vacía (lo seguro). Sin hora confiable: False."""
    if at is None:
        if not _ok:
            return False
        h, m = now()
        at = h * 60 + m
    a, b = minutes(on), minutes(off)
    if a == b:
        return False
    if a < b:
        return a <= at < b
    return at >= a or at < b
