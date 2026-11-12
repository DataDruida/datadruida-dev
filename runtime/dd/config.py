# dd/config.py — configuración del equipo, FUERA del código
# Licencia MIT — DataDruida
#
# El WiFi y cualquier dato privado viven en /dd_config.json, que la página
# de DataDruida escribe al cargar el programa. Nunca van en main.py: el
# código se comparte, la contraseña no.

import json

PATH = "/dd_config.json"


def load():
    try:
        with open(PATH) as f:
            return json.load(f)
    except (OSError, ValueError):
        return {}


def get(key, default=None):
    return load().get(key, default)


def wifi_connect(timeout_s=15):
    """Conecta al WiFi de /dd_config.json si hay uno. True si quedó conectado."""
    c = load()
    ssid = c.get("wifi_ssid")
    if not ssid:
        return False
    import network
    import time
    w = network.WLAN(network.STA_IF)
    w.active(True)
    if w.isconnected():
        return True
    w.connect(ssid, c.get("wifi_pass", ""))
    t0 = time.ticks_ms()
    while not w.isconnected() and time.ticks_diff(time.ticks_ms(), t0) < timeout_s * 1000:
        time.sleep_ms(250)
    ok = w.isconnected()
    print("[dd] WiFi", ssid, "conectado" if ok else "NO conectó (sigo sin red)")
    return ok
