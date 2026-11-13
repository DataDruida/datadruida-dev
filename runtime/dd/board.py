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
# rs485.external: la placa no trae RS485; va un módulo TTL-RS485 con dirección
#   automática en esos pines.
# screen: pantalla ST7789 por SPI (dd.screen); touch CST816 en el bus i2c.
# ui: "lvgl" si la placa se maneja con dd.ui() (firmware DataDruida con
#   pantalla: MicroPython + LVGL + ddhw). El panel y el touch los arma ddhw en C.

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
    # Probado en DruidaVision2 (misma placa): LCD 2" ST7789 240x320 por SPI
    # SCLK 39 / MOSI 38 / CS 45 / DC 42 / BL 1, invertida; touch CST816D 0x15
    # en SDA 48 / SCL 47. Batería por divisor 1/3 en GPIO5. Sin relays, sin RTC
    # y sin RS485 propio: 17/18 quedan libres para un módulo externo.
    "waveshare-esp32-s3-touch-lcd-2": {
        "name": "Waveshare ESP32-S3-Touch-LCD-2",
        "i2c": {"id": 0, "sda": 48, "scl": 47, "freq": 400000},
        "relays": {"driver": "gpio", "pins": []},
        "rtc": None,
        "rs485": {"uart": 1, "tx": 17, "rx": 18, "baud": 4800, "external": True},
        "inputs": [],
        "screen": {"driver": "st7789", "width": 240, "height": 320, "spi": 2,
                   "sck": 39, "mosi": 38, "cs": 45, "dc": 42, "bl": 1,
                   "invert": True, "touch": {"driver": "cst816", "addr": 0x15}},
        "battery": {"adc": 5, "ratio": 3.0},
        "ui": "lvgl",
    },
    # Familia ESP32-S3-Touch-LCD con panel RGB. Panel, touch GT911 y expansor
    # los maneja ddhw (firmware con pantalla); el I2C 8/9 queda tomado por él,
    # por eso acá no hay "i2c" ni RTC (la hora sale de NTP). RS485 a bordo con
    # dirección automática en RX 43 / TX 44 (por eso el REPL va sólo por USB).
    "waveshare-esp32-s3-touch-lcd-7b": {
        "name": "Waveshare ESP32-S3-Touch-LCD-7B",
        "relays": {"driver": "gpio", "pins": []},
        "rtc": None,
        "rs485": {"uart": 1, "tx": 44, "rx": 43, "baud": 4800},
        "inputs": [],
        "ui": "lvgl",
    },
    "waveshare-esp32-s3-touch-lcd-800x480": {
        "name": "Waveshare ESP32-S3-Touch-LCD 4,3 / 5 / 7 (800x480)",
        "relays": {"driver": "gpio", "pins": []},
        "rtc": None,
        "rs485": {"uart": 1, "tx": 44, "rx": 43, "baud": 4800},
        "inputs": [],
        "ui": "lvgl",
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
