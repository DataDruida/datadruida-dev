# dd/board.py — perfiles de placa
# Licencia Apache-2.0 — DataDruida
#
# La placa se elige en /dd_config.json ("board": "<id>"), que escribe la página
# al cargar el programa. Sin ese dato se asume la ETH-8DI-8RO.
#
# relays.driver:
#   "tca9554" → expansor I2C (addr), relays 1..count
#   "gpio"    → un GPIO por relay (pins), activo en alto
# rtc: None si la placa no tiene reloj propio (la hora sale sólo de NTP).
# rs485.de: GPIO de habilitación del transmisor, si la placa no lo maneja sola.

BOARDS = {
    "waveshare-esp32-s3-eth-8di-8ro": {
        "name": "Waveshare ESP32-S3-ETH-8DI-8RO",
        "i2c": {"id": 0, "sda": 42, "scl": 41, "freq": 100000},
        "relays": {"driver": "tca9554", "addr": 0x20, "count": 8},
        "rtc": {"driver": "pcf85063", "addr": 0x51},
        "rs485": {"uart": 1, "tx": 17, "rx": 18, "baud": 4800},
        # DI1..DI8 = GPIO4..GPIO11, contacto seco contra DGND
        "inputs": [4, 5, 6, 7, 8, 9, 10, 11],
    },
    # Esquemático oficial: CH1..CH6 = GPIO1, 2, 41, 42, 45, 46 (transistor +
    # optoacoplador, activo en alto). RS485 con dirección automática (74HC04
    # sobre TXD). Buzzer GPIO21, LED RGB WS2812 GPIO38. Sin RTC.
    "waveshare-esp32-s3-relay-6ch": {
        "name": "Waveshare ESP32-S3-Relay-6CH",
        "relays": {"driver": "gpio", "pins": [1, 2, 41, 42, 45, 46]},
        "rtc": None,
        "rs485": {"uart": 1, "tx": 17, "rx": 18, "baud": 4800},
        "inputs": [],
        "buzzer": 21,
        "rgb": 38,
    },
    # Esquemático oficial: CH1 = GPIO47. RS485 TXD1 17 / RXD1 18 / RS485_EN 21.
    # RTC PCF85063 en SDA 39 / SCL 38.
    "waveshare-esp32-s3-relay-1ch": {
        "name": "Waveshare ESP32-S3-Relay-1CH",
        "i2c": {"id": 0, "sda": 39, "scl": 38, "freq": 100000},
        "relays": {"driver": "gpio", "pins": [47]},
        "rtc": {"driver": "pcf85063", "addr": 0x51},
        "rs485": {"uart": 1, "tx": 17, "rx": 18, "baud": 4800, "de": 21},
        "inputs": [],
    },
}

DEFAULT = "waveshare-esp32-s3-eth-8di-8ro"
_current = None


def select(board_id):
    global _current, _i2c
    if board_id not in BOARDS:
        raise ValueError("placa desconocida: %s" % board_id)
    _current = board_id
    _i2c = None


def current_id():
    global _current
    if _current is None:
        from dd import config
        bid = config.get("board") or DEFAULT
        if bid not in BOARDS:
            print("[dd] placa desconocida en /dd_config.json:", bid, "- uso", DEFAULT)
            bid = DEFAULT
        _current = bid
    return _current


def current():
    return BOARDS[current_id()]


_i2c = None


def i2c():
    """Bus I2C compartido (relays + RTC). None si la placa no tiene."""
    global _i2c
    c = current().get("i2c")
    if not c:
        return None
    if _i2c is None:
        from machine import I2C, Pin
        _i2c = I2C(c["id"], sda=Pin(c["sda"]), scl=Pin(c["scl"]), freq=c["freq"])
    return _i2c
