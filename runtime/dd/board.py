# dd/board.py — perfiles de placa
# Licencia MIT — DataDruida

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
}

DEFAULT = "waveshare-esp32-s3-eth-8di-8ro"
_current = DEFAULT


def select(board_id):
    global _current
    if board_id not in BOARDS:
        raise ValueError("placa desconocida: %s" % board_id)
    _current = board_id


def current():
    return BOARDS[_current]


_i2c = None


def i2c():
    """Bus I2C compartido (relays + RTC)."""
    global _i2c
    if _i2c is None:
        from machine import I2C, Pin
        c = current()["i2c"]
        _i2c = I2C(c["id"], sda=Pin(c["sda"]), scl=Pin(c["scl"]), freq=c["freq"])
    return _i2c
