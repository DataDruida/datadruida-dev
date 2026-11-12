# dd/sensors.py — drivers de sensores RS485 Modbus
# Licencia Apache-2.0 — DataDruida
#
# Cada sensor tiene read() -> dict con sus magnitudes, o None si no contestó
# o la lectura es inválida. Nunca lanza excepción: un sensor caído no tiene
# que tumbar el programa (el programa decide qué hacer con None, y lo seguro
# casi siempre es apagar lo que ese sensor gobierna).
#
# Bus: todos a 4800 8N1. Los SLICETEX vienen así de fábrica (dirección 1).
# Los Seeed vienen a 9600: se pasan a 4800 con seeed_setup() UNA vez,
# con ese único sensor conectado al bus.

from dd.modbus import bus as _shared_bus, ModbusError

INVALID = (-32768, -32765)   # Seeed: sensor roto / no soportado


class _Sensor:
    model = "?"

    def __init__(self, addr=1, bus=None, name=None):
        self.addr = addr
        self._bus = bus
        self.name = name or self.model
        self.last = None
        self.fails = 0

    @property
    def bus(self):
        if self._bus is None:
            self._bus = _shared_bus()
        return self._bus

    def _read(self):
        raise NotImplementedError

    def read(self):
        try:
            v = self._read()
        except (ModbusError, OSError):
            v = None
        if v is None:
            self.fails += 1
        else:
            self.fails = 0
            self.last = v
        return v

    def __repr__(self):
        return "<%s @%d>" % (self.model, self.addr)


# ---------------------------------------------------------------- SLICETEX
class SlicetexTH(_Sensor):
    """SLICETEX TH-MB-02S — temperatura y humedad ambiente."""
    model = "TH-MB-02S"

    def _read(self):
        hum, temp = self.bus.read(self.addr, 0, 2, fc=3, signed=True)
        return {"temp": temp / 10, "hum": hum / 10}


class SlicetexSoil(_Sensor):
    """SLICETEX SOIL-MB-THC-S — suelo: humedad, temperatura y EC (uS/cm)."""
    model = "SOIL-MB-THC-S"

    def _read(self):
        hum, temp, ec = self.bus.read(self.addr, 0, 3, fc=3, signed=True)
        return {"soil_hum": hum / 10, "soil_temp": temp / 10, "ec": ec}


class SlicetexNPK(_Sensor):
    """SLICETEX SOIL-MB-NPKPHCTH-S — suelo + pH + N/P/K (mg/kg).
    ATENCION: el mapa de pH/N/P/K (regs 3..6) NO está verificado todavía.
    El fabricante avisa que el NPK se estima a partir de la EC."""
    model = "SOIL-MB-NPKPHCTH-S"
    verified = False

    def _read(self):
        r = self.bus.read(self.addr, 0, 7, fc=3, signed=True)
        return {"soil_hum": r[0] / 10, "soil_temp": r[1] / 10, "ec": r[2],
                "ph": r[3] / 10, "n": r[4], "p": r[5], "k": r[6]}


# ------------------------------------------------------------------- Seeed
def _ok(*vals):
    return all(v not in INVALID for v in vals)


class SeeedCO2(_Sensor):
    """Seeed SenseCAP S-CO2-03 — CO2 (ppm) + temperatura + humedad.
    Durante ~2 min después de alimentarlo devuelve CO2 = 0: lo tratamos
    como 'sin dato' (None en co2)."""
    model = "S-CO2-03"

    def _read(self):
        co2, temp, hum = self.bus.read(self.addr, 0, 3, fc=3, signed=True)
        if not _ok(co2, temp, hum):
            return None
        return {"co2": co2 if co2 > 0 else None, "temp": temp / 100, "hum": hum / 100}


class SeeedEC(_Sensor):
    """Seeed SenseCAP S-EC-01 — EC (uS/cm), TDS, salinidad y temp del agua.
    ATENCION: sin probar en hardware todavía."""
    model = "S-EC-01"
    verified = False

    def _read(self):
        r = self.bus.read(self.addr, 0, 5, fc=4, signed=True)
        if not _ok(*r):
            return None
        return {"water_temp": r[0] / 100, "ec": r[2], "salinity": r[3], "tds": r[4]}


class SeeedPhOrp(_Sensor):
    """Seeed SenseCAP S-pH&ORP-01A — pH, ORP (mV) y temperatura del agua."""
    model = "S-pH&ORP-01A"

    def _read(self):
        temp, ph, orp = self.bus.read(self.addr, 0, 3, fc=3, signed=True)
        if not _ok(temp, ph, orp):
            return None
        return {"water_temp": temp / 100, "ph": ph / 100, "orp": orp / 10}


class SeeedSoil(_Sensor):
    """Seeed SenseCAP S-Temp&VWC&EC-04A — sustrato (coco/tierra):
    temperatura, humedad volumétrica (%) y EC bulk."""
    model = "S-Temp&VWC&EC-04A"

    def _read(self):
        try:
            r = self.bus.read(self.addr, 0, 3, fc=3, signed=True)
        except ModbusError:
            r = self.bus.read(self.addr, 0, 3, fc=4, signed=True)
        if not _ok(*r):
            return None
        return {"soil_temp": r[0] / 100, "soil_hum": r[1] / 100, "ec": r[2]}


MODELS = {
    "slicetex-th-mb-02s": SlicetexTH,
    "slicetex-soil-mb-thc-s": SlicetexSoil,
    "slicetex-soil-mb-npkphcth-s": SlicetexNPK,
    "seeed-s-co2-03": SeeedCO2,
    "seeed-s-ec-01": SeeedEC,
    "seeed-s-ph-orp-01a": SeeedPhOrp,
    "seeed-s-temp-vwc-ec-04a": SeeedSoil,
}


def sensor(model_id, addr, name=None):
    """Crea un sensor por id de catálogo: sensor('slicetex-th-mb-02s', 1)."""
    return MODELS[model_id](addr, name=name)


# ------------------------------------------------- alta de sensores (setup)
SLICETEX_REG_ADDR = 0x07D0
SEEED_REG = {  # modelo -> (registro de dirección, registro de baud)
    "seeed-s-co2-03": (0x0010, 0x0011),
    "seeed-s-ec-01": (0x0200, 0x0201),
    "seeed-s-ph-orp-01a": (0x0200, 0x0201),
    "seeed-s-temp-vwc-ec-04a": (0x0200, 0x0201),
}
SEEED_BAUD_4800 = 2


def slicetex_set_address(new_addr, old_addr=1):
    """Cambia la dirección de un SLICETEX. Conectar SOLO ese sensor."""
    return _shared_bus().write(old_addr, SLICETEX_REG_ADDR, new_addr)


def seeed_setup(model_id, new_addr, old_addr=None, scan=True):
    """Pasa un Seeed de fábrica (9600) a 4800 con dirección new_addr.
    Conectar SOLO ese sensor al bus. Después hay que desenchufarlo y volver
    a enchufarlo: los Seeed aplican dirección y velocidad al reiniciar."""
    from dd.modbus import Bus
    reg_id, reg_baud = SEEED_REG[model_id]
    main = _shared_bus()
    b = Bus(uart=main.uart, baud=9600)
    main.uart.init(baudrate=9600)
    try:
        cands = [old_addr] if old_addr else [1, 30, 45]
        found = None
        for a in cands:
            try:
                b.read(a, reg_id, 1, fc=3, timeout_ms=200)
                found = a
                break
            except ModbusError:
                pass
        if found is None and scan:
            hits = b.scan(1, 247, reg=reg_id, timeout_ms=80)
            found = hits[0] if hits else None
        if found is None:
            return "no encontrado a 9600"
        b.write(found, reg_id, new_addr)
        b.write(found, reg_baud, SEEED_BAUD_4800)
        return "ok: estaba en %d, quedó en %d @4800. Desenchufalo y volvé a enchufarlo." % (found, new_addr)
    finally:
        main.uart.init(baudrate=main.baud)
