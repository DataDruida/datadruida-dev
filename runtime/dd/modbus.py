# dd/modbus.py — Modbus RTU maestro sobre RS485 (MicroPython)
# Licencia Apache-2.0 — DataDruida
#
# Un solo bus por placa. Todos los sensores del bus a la MISMA velocidad
# (en DataDruida usamos 4800 8N1: SLICETEX viene así de fábrica y los Seeed
# se pasan a 4800 una vez).

import time

try:
    from machine import UART
except ImportError:  # tests en PC
    UART = None


def crc16(data):
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return crc


def _frame(addr, fc, reg, val):
    f = bytes([addr, fc, (reg >> 8) & 0xFF, reg & 0xFF, (val >> 8) & 0xFF, val & 0xFF])
    c = crc16(f)
    return f + bytes([c & 0xFF, c >> 8])


class ModbusError(Exception):
    pass


class Bus:
    def __init__(self, uart_id=1, tx=17, rx=18, baud=4800, timeout_ms=300, uart=None, de=None):
        self.baud = baud
        # GPIO de habilitación del transmisor (placas sin dirección automática)
        self._de = None
        if de is not None:
            from machine import Pin
            self._de = Pin(de, Pin.OUT, value=0)
        self.timeout_ms = timeout_ms
        if uart is not None:
            self.uart = uart
        else:
            self.uart = UART(uart_id, baudrate=baud, bits=8, parity=None, stop=1,
                             tx=tx, rx=rx, timeout=20)
        # silencio mínimo entre tramas (3,5 caracteres); a 4800 ~8 ms
        self._gap_ms = max(2, int(3.5 * 11 * 1000 / baud) + 1)
        self._char_us = int(11 * 1000000 / baud) + 1
        self._last = 0

    def _send(self, req):
        if self._de is None:
            self.uart.write(req)
            return
        self._de.value(1)
        self.uart.write(req)
        try:
            self.uart.flush()                  # espera que salga el último byte
        except AttributeError:
            time.sleep_us(len(req) * self._char_us)
        self._de.value(0)

    def _drain(self):
        while self.uart.any():
            self.uart.read()

    def _xfer(self, req, expect_len, timeout_ms=None):
        wait = self._gap_ms - time.ticks_diff(time.ticks_ms(), self._last)
        if wait > 0:
            time.sleep_ms(wait)
        self._drain()
        self._send(req)
        t0 = time.ticks_ms()
        to = timeout_ms or self.timeout_ms
        buf = b""
        while time.ticks_diff(time.ticks_ms(), t0) < to:
            n = self.uart.any()
            if n:
                buf += self.uart.read(n)
                # respuesta de excepción: addr, fc|0x80, code, crc(2) = 5 bytes
                if len(buf) >= 5 and buf[1] & 0x80:
                    break
                if len(buf) >= expect_len:
                    break
            else:
                time.sleep_ms(2)
        self._last = time.ticks_ms()
        if not buf:
            raise ModbusError("sin respuesta")
        if len(buf) >= 5 and buf[1] & 0x80:
            raise ModbusError("excepcion modbus %d" % buf[2])
        if len(buf) < expect_len:
            raise ModbusError("respuesta corta")
        buf = buf[:expect_len]
        c = crc16(buf[:-2])
        if buf[-2] != (c & 0xFF) or buf[-1] != (c >> 8):
            raise ModbusError("CRC invalido")
        if buf[0] != req[0]:
            raise ModbusError("contesto otra direccion")
        return buf

    def read(self, addr, reg, count=1, fc=3, signed=False, timeout_ms=None):
        """Lee `count` registros. fc=3 (holding) o fc=4 (input).
        Devuelve una lista de enteros (con signo si signed=True)."""
        resp = self._xfer(_frame(addr, fc, reg, count), 5 + 2 * count, timeout_ms)
        if resp[2] != 2 * count:
            raise ModbusError("largo inesperado")
        out = []
        for i in range(count):
            v = (resp[3 + 2 * i] << 8) | resp[4 + 2 * i]
            if signed and v >= 0x8000:
                v -= 0x10000
            out.append(v)
        return out

    def write(self, addr, reg, value, timeout_ms=None):
        """FC06: escribe un registro. addr=0 es broadcast (no contesta)."""
        req = _frame(addr, 6, reg, value & 0xFFFF)
        if addr == 0:
            self._drain()
            self._send(req)
            self._last = time.ticks_ms()
            return True
        resp = self._xfer(req, 8, timeout_ms)
        return resp == req

    def scan(self, start=1, end=247, reg=0, fc=3, timeout_ms=90):
        """Busca direcciones que contesten. Lento: usar sólo para diagnóstico."""
        found = []
        for a in range(start, end + 1):
            try:
                self.read(a, reg, 1, fc=fc, timeout_ms=timeout_ms)
                found.append(a)
            except ModbusError:
                pass
        return found


_bus = None


def bus(**kw):
    """Bus compartido de la placa (se crea la primera vez)."""
    global _bus
    if _bus is None:
        from dd import board
        cfg = board.current().get("rs485", {})
        args = dict(uart_id=cfg.get("uart", 1), tx=cfg.get("tx", 17), rx=cfg.get("rx", 18),
                    baud=cfg.get("baud", 4800), de=cfg.get("de"))
        args.update(kw)
        _bus = Bus(**args)
    return _bus
