# Hola Mundo 4 — Riego por humedad de suelo
# Placa: Waveshare ESP32-S3-ETH-8DI-8RO · Sensor: SLICETEX SOIL-MB-THC-S (ID 2)
# Relay 3 = bomba. Riega en pulsos de 20 s con 2 min de pausa mientras la
# humedad esté por debajo de 35 %, y sólo de día. Tope de seguridad: la bomba
# nunca queda prendida más de 60 s seguidos.
import dd

suelo = dd.sensor("slicetex-soil-mb-thc-s", 2)
r = dd.relays()
r.set_max_on(3, 60)
seco = dd.Hysteresis(low=35.0, high=45.0, mode="up")
pulso = dd.Cycle(on_s=20, off_s=120)

def loop():
    v = suelo.read()
    hum = v["soil_hum"] if v else None
    regar = seco.update(hum) and dd.in_window("07:00", "19:00")
    r.set(3, pulso.update(regar))
    print("suelo", hum, "%  bomba", "ON" if r.get(3) else "OFF")

dd.run(loop, every_s=2)
