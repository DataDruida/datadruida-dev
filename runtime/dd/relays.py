# dd/relays.py — relays de la placa, con seguridad incluida
#   - por expansor I2C TCA9554 (ETH-8DI-8RO, 8 relays)
#   - por GPIO directo (Relay-6CH, Relay-1CH)
# Licencia Apache-2.0 — DataDruida
#
# Reglas de seguridad (aprendidas en campo):
#  - Al iniciar se escribe el LATCH (todo apagado) ANTES de pasar los pines a
#    salida. Al revés, después de un corte de luz los 8 relays prenden un
#    instante (el latch de fábrica es 0xFF). Con GPIO directo, lo mismo: el
#    pin se crea ya en 0 (Pin(g, OUT, value=0)).
#  - Un pico de tensión puede resetear el TCA9554: vuelve a CONFIG=0xFF (todo
#    entrada) y los relays quedan sin drive aunque el programa crea que están
#    prendidos. heal() lo detecta y re-asienta el estado. dd.run() lo llama
#    solo cada 1 s.
#  - Tope de tiempo encendido por relay (max_on_s): si se cumple, se apaga y
#    queda bloqueado hasta reset(n). Sirve de red si el programa se equivoca.
#  - Nunca conectar una carga en el contacto NC: ante cualquier falla, NC se
#    energiza.

import time

REG_OUTPUT = 0x01
REG_POL = 0x02
REG_CONFIG = 0x03


class Relays:
    def __init__(self, i2c=None, addr=0x20, count=8, pins=None):
        if i2c is None and pins is None:
            from dd import board
            cfg = board.current()["relays"]
            if cfg.get("driver") == "gpio":
                pins = cfg["pins"]
            else:
                i2c = board.i2c()
                addr = cfg["addr"]
                count = cfg["count"]
        self.i2c = i2c
        self.addr = addr
        self._pins = None
        if pins is not None:
            from machine import Pin
            self._pins = [Pin(g, Pin.OUT, value=0) for g in pins]
            count = len(pins)
        self.count = count
        self._mask = 0
        self._on_since = [None] * (count + 1)
        self.max_on_s = [0] * (count + 1)   # 0 = sin tope
        self.locked = [False] * (count + 1)
        self.heals = 0
        self._init_chip()

    # --- bajo nivel ---
    def _w(self, reg, val):
        self.i2c.writeto_mem(self.addr, reg, bytes([val & 0xFF]))

    def _r(self, reg):
        return self.i2c.readfrom_mem(self.addr, reg, 1)[0]

    def _out(self, mask):
        if self._pins is not None:
            for i, p in enumerate(self._pins):
                p.value(1 if mask & (1 << i) else 0)
        else:
            self._w(REG_OUTPUT, mask)

    def _init_chip(self):
        if self._pins is not None:
            self._out(self._mask)
            return
        self._w(REG_OUTPUT, self._mask)  # 1) latch primero
        self._w(REG_POL, 0x00)
        self._w(REG_CONFIG, 0x00)        # 2) recién ahora, pines como salida

    # --- API ---
    def _check(self, n):
        if not 1 <= n <= self.count:
            raise ValueError("relay %d no existe (1..%d)" % (n, self.count))

    def set(self, n, on):
        """Prende (True) o apaga (False) el relay n (1..count)."""
        self._check(n)
        if on and self.locked[n]:
            return False
        bit = 1 << (n - 1)
        new = (self._mask | bit) if on else (self._mask & ~bit)
        if new != self._mask:
            self._mask = new
            self._out(self._mask)
            self._on_since[n] = time.ticks_ms() if on else None
        return True

    def on(self, n):
        return self.set(n, True)

    def off(self, n):
        return self.set(n, False)

    def get(self, n):
        self._check(n)
        return bool(self._mask & (1 << (n - 1)))

    def all_off(self):
        self._mask = 0
        try:
            self._out(0)
        except OSError:
            pass
        self._on_since = [None] * (self.count + 1)

    def set_max_on(self, n, seconds):
        """Tope de encendido continuo del relay n, en segundos (0 = sin tope)."""
        self._check(n)
        self.max_on_s[n] = int(seconds)

    def reset(self, n):
        """Desbloquea un relay que se cortó por el tope."""
        self._check(n)
        self.locked[n] = False

    def on_for(self, n):
        """Segundos que lleva prendido el relay n (0 si está apagado)."""
        t = self._on_since[n]
        return 0 if t is None else time.ticks_diff(time.ticks_ms(), t) // 1000

    def state(self):
        return [self.get(n) for n in range(1, self.count + 1)]

    # --- tareas periódicas (las llama dd.run) ---
    def heal(self):
        """Re-asienta el chip si se reseteó por un pico de tensión."""
        if self._pins is not None:
            self._out(self._mask)   # GPIO: re-afirma el estado, no hay chip que resetear
            return False
        try:
            cfg = self._r(REG_CONFIG)
        except OSError:
            return False
        if cfg != 0x00:
            self._init_chip()
            self.heals += 1
            return True
        return False

    def enforce_limits(self):
        cut = []
        for n in range(1, self.count + 1):
            lim = self.max_on_s[n]
            if lim and self.get(n) and self.on_for(n) >= lim:
                self.set(n, False)
                self.locked[n] = True
                cut.append(n)
        return cut


_relays = None


def relays():
    """Relays de la placa (se inician apagados la primera vez)."""
    global _relays
    if _relays is None:
        _relays = Relays()
    return _relays
