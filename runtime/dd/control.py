# dd/control.py — piezas de control reusables
# Licencia Apache-2.0 — DataDruida


class Hysteresis:
    """Termostato / humidistato con banda.
    mode='up'  : el equipo SUBE la variable (calefactor, humidificador):
                 prende con valor <= low, apaga con valor >= high.
    mode='down': el equipo BAJA la variable (aire, extractor, deshumidificador):
                 prende con valor >= high, apaga con valor <= low.
    Con valor None (sensor caído) devuelve False: apagar es lo seguro."""

    def __init__(self, low, high, mode="down"):
        if low >= high:
            raise ValueError("low tiene que ser menor que high")
        if mode not in ("up", "down"):
            raise ValueError("mode: 'up' o 'down'")
        self.low, self.high, self.mode = low, high, mode
        self.on = False

    def update(self, value):
        if value is None:
            self.on = False
            return False
        if self.mode == "up":
            if value <= self.low:
                self.on = True
            elif value >= self.high:
                self.on = False
        else:
            if value >= self.high:
                self.on = True
            elif value <= self.low:
                self.on = False
        return self.on


class Cycle:
    """Pulso / pausa: prende `on_s` segundos y apaga `off_s` segundos.
    Llamar update(active) en cada vuelta; active=False reinicia el ciclo."""

    def __init__(self, on_s, off_s, max_cycles=0):
        import time
        self._t = time
        self.on_s, self.off_s, self.max_cycles = on_s, off_s, max_cycles
        self.reset()

    def reset(self):
        self.done = 0
        self._phase_on = True
        self._since = self._t.ticks_ms()
        self._started = False

    def update(self, active=True):
        if not active:
            self.reset()
            return False
        t = self._t
        if not self._started:
            self._started = True
            self._since = t.ticks_ms()
            self._phase_on = True
        if self.max_cycles and self.done >= self.max_cycles:
            return False
        el = t.ticks_diff(t.ticks_ms(), self._since) // 1000
        if self._phase_on and el >= self.on_s:
            self._phase_on = False
            self._since = t.ticks_ms()
            self.done += 1
        elif not self._phase_on and el >= self.off_s:
            self._phase_on = True
            self._since = t.ticks_ms()
        if self.max_cycles and self.done >= self.max_cycles:
            return False
        return self._phase_on


def average(values):
    """Promedio ignorando None. None si no hay ningún valor."""
    v = [x for x in values if x is not None]
    return sum(v) / len(v) if v else None


def vpd(temp, hum, leaf_offset=2.0):
    """Déficit de presión de vapor (kPa) con la hoja leaf_offset °C bajo el aire."""
    if temp is None or hum is None:
        return None
    import math
    tl = temp - leaf_offset
    es_leaf = 0.6108 * math.exp(17.27 * tl / (tl + 237.3))
    es_air = 0.6108 * math.exp(17.27 * temp / (temp + 237.3))
    return max(0.0, es_leaf - es_air * hum / 100)
