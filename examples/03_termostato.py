# Hola Mundo 3 — Termostato con banda
# Placa: Waveshare ESP32-S3-ETH-8DI-8RO · Sensor: SLICETEX TH-MB-02S (ID 1)
# Relay 2 = extractor: prende con 28 °C o más, apaga con 25 °C o menos.
# Si el sensor deja de responder, apaga.
import dd

th = dd.sensor("slicetex-th-mb-02s", 1)
r = dd.relays()
extractor = dd.Hysteresis(low=25.0, high=28.0, mode="down")

def loop():
    v = th.read()
    temp = v["temp"] if v else None
    r.set(2, extractor.update(temp))
    print("T", temp, "extractor", "ON" if r.get(2) else "OFF")

dd.run(loop, every_s=5)
