"""Placa simulada para correr el runtime dd en la PC (CPython).
Instala un modulo `machine` falso y agrega a `time` las funciones de MicroPython."""
import sys
import time
import types

# ---- reloj virtual (los tests avanzan el tiempo a mano) ----
_now = [0]


def ticks_ms():
    return _now[0]


def ticks_diff(a, b):
    return a - b


def ticks_add(a, b):
    return a + b


def sleep_ms(ms):
    _now[0] += ms


def advance(seconds):
    _now[0] += int(seconds * 1000)


time.ticks_ms = ticks_ms
time.ticks_diff = ticks_diff
time.ticks_add = ticks_add
time.sleep_ms = sleep_ms
time.sleep_us = lambda us: None
sys.print_exception = lambda e: print("EXC", repr(e))


# ---- I2C con TCA9554 y PCF85063 simulados ----
class TCA9554:
    def __init__(self):
        self.reset()

    def reset(self):  # estado de encendido / glitch
        self.regs = {0x01: 0xFF, 0x02: 0x00, 0x03: 0xFF}
        self.log = []

    def driven(self):
        """Mascara de relays realmente energizados (pin en salida y latch en 1)."""
        return self.regs[0x01] & ~self.regs[0x03] & 0xFF


class I2C:
    def __init__(self, *a, **k):
        self.tca = TCA9554()
        self.rtc = bytearray(7)
        self.fail = False

    def writeto_mem(self, addr, reg, buf):
        if self.fail:
            raise OSError(19)
        if addr == 0x20:
            self.tca.regs[reg] = buf[0]
            self.tca.log.append((reg, buf[0], self.tca.driven()))
        elif addr == 0x51:
            self.rtc[reg - 4:reg - 4 + len(buf)] = buf

    def readfrom_mem(self, addr, reg, n):
        if self.fail:
            raise OSError(19)
        if addr == 0x20:
            return bytes([self.tca.regs[reg]])
        if addr == 0x51:
            return bytes(self.rtc[reg - 4:reg - 4 + n])
        raise OSError(19)


# ---- UART con esclavos Modbus simulados ----
class UART:
    def __init__(self, *a, **k):
        self.baudrate = k.get("baudrate", 9600)
        self.slaves = {}      # addr -> {"baud": int, "regs": {(fc, reg): val}}
        self.rx = b""
        self.sent = []

    def init(self, baudrate=None, **k):
        if baudrate:
            self.baudrate = baudrate

    def any(self):
        return len(self.rx)

    def flush(self):
        pass

    def read(self, n=None):
        n = len(self.rx) if n is None else n
        out, self.rx = self.rx[:n], self.rx[n:]
        return out

    def write(self, req):
        from dd.modbus import crc16
        self.sent.append(bytes(req))
        addr, fc = req[0], req[1]
        reg, val = (req[2] << 8) | req[3], (req[4] << 8) | req[5]
        targets = list(self.slaves) if addr == 0 else [addr]
        for a in targets:
            s = self.slaves.get(a)
            if not s or s["baud"] != self.baudrate:
                continue
            if fc in (3, 4):
                body = bytes([a, fc, 2 * val])
                for i in range(val):
                    v = s["regs"].get((fc, reg + i))
                    if v is None:
                        body = bytes([a, fc | 0x80, 2])
                        break
                    body += bytes([(v >> 8) & 0xFF, v & 0xFF])
            elif fc == 6:
                s["regs"][(3, reg)] = val
                s.setdefault("writes", []).append((reg, val))
                body = bytes(req[:6])
            else:
                continue
            if addr == 0:
                continue  # broadcast: nadie contesta
            c = crc16(body)
            self.rx += body + bytes([c & 0xFF, c >> 8])
        return len(req)


class Pin:
    IN, OUT, PULL_UP = 1, 2, 3
    levels = {}      # entradas simuladas
    out = {}         # salidas: gpio -> nivel
    log = []         # (gpio, nivel) en orden

    def __init__(self, g, mode=None, pull=None, value=None):
        self.g = g
        self.mode = mode
        if mode == Pin.OUT:
            # como en MicroPython: el nivel se fija ANTES de pasar a salida
            lvl = 0 if value is None else value
            Pin.out[g] = lvl
            Pin.log.append((g, lvl))

    def value(self, v=None):
        if v is None:
            if self.mode == Pin.OUT:
                return Pin.out.get(self.g, 0)
            return Pin.levels.get(self.g, 1)
        Pin.out[self.g] = 1 if v else 0
        Pin.log.append((self.g, Pin.out[self.g]))


class Timer:
    PERIODIC = 1

    def __init__(self, *a):
        self.cb = None

    def init(self, period=0, mode=0, callback=None):
        self.cb = callback

    def deinit(self):
        self.cb = None


class RTC:
    dt = None

    def datetime(self, t=None):
        RTC.dt = t


def _reset():
    raise SystemExit("reset")


machine = types.ModuleType("machine")
machine.I2C, machine.UART, machine.Pin, machine.Timer, machine.RTC = I2C, UART, Pin, Timer, RTC
machine.reset = _reset
sys.modules["machine"] = machine


def s16(v):
    return v & 0xFFFF


def fresh():
    """Reinicia los singletons del runtime para un test limpio."""
    # ojo: `dd.relays` es la funcion (dd/__init__ la re-exporta), el modulo
    # se busca en sys.modules
    import dd  # noqa: F401
    sys.modules["dd.board"]._i2c = None
    sys.modules["dd.board"]._current = None
    Pin.out.clear()
    del Pin.log[:]
    sys.modules["dd.relays"]._relays = None
    sys.modules["dd.modbus"]._bus = None
    _now[0] = 0


class SleepBudget:
    """Corta dd.run() con KeyboardInterrupt (como el boton Detener) tras n esperas."""

    def __init__(self, n):
        self.n = n

    def __enter__(self):
        def guard(ms):
            self.n -= 1
            if self.n <= 0:
                raise KeyboardInterrupt
            sleep_ms(ms)
        time.sleep_ms = guard
        return self

    def __exit__(self, *a):
        time.sleep_ms = sleep_ms
        return False
