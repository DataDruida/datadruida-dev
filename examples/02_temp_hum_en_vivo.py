# Hola Mundo 2 — Temperatura y humedad en vivo
# Placa: Waveshare ESP32-S3-ETH-8DI-8RO · Sensor: SLICETEX TH-MB-02S (ID 1)
# Imprime la lectura cada 5 s en la consola de la página. No toca relays.
import dd

th = dd.sensor("slicetex-th-mb-02s", 1)

def loop():
    v = th.read()
    if v is None:
        print("sin lectura del sensor (revisá A/B y alimentación)")
    else:
        print("T %.1f °C  HR %.1f %%  VPD %.2f kPa" % (v["temp"], v["hum"], dd.vpd(v["temp"], v["hum"])))

dd.run(loop, every_s=5)
