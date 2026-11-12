# dd/inputs.py — entradas digitales DI1..DIn (contacto seco contra DGND)
# Sólo en placas que las tienen (ETH-8DI-8RO: 8). En las demás, closed() da error.
# Licencia Apache-2.0 — DataDruida
#
# Contacto cerrado = True. Un flotante de tanque, un pulsador o un fin de
# carrera se conectan entre DIx y DGND, sin fuente.

_pins = None


def _init():
    global _pins
    if _pins is None:
        from machine import Pin
        from dd import board
        _pins = [Pin(g, Pin.IN, Pin.PULL_UP) for g in board.current().get("inputs", [])]
    return _pins


def closed(n):
    """True si el contacto de DIn está cerrado."""
    p = _init()
    if not p:
        raise ValueError("esta placa no tiene entradas digitales")
    if not 1 <= n <= len(p):
        raise ValueError("entrada %d no existe (1..%d)" % (n, len(p)))
    return p[n - 1].value() == 0


def all_closed():
    return [closed(n) for n in range(1, len(_init()) + 1)]
