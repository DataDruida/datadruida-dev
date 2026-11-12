# Hola Mundo 5 — Alarma de CO₂
# Placa: Waveshare ESP32-S3-ETH-8DI-8RO · Sensor: Seeed S-CO2-03 (ID 45)
# Relay 4 = extractor / baliza: prende con 1500 ppm, apaga con 1000 ppm.
# El S-CO2-03 sale de fábrica a 9600: configurarlo una vez con
#   dd.sensors.seeed_setup("seeed-s-co2-03", 45)
import dd

co2 = dd.sensor("seeed-s-co2-03", 45)
r = dd.relays()
alarma = dd.Hysteresis(low=1000, high=1500, mode="down")

def loop():
    v = co2.read()
    ppm = v["co2"] if v else None
    r.set(4, alarma.update(ppm))
    print("CO2", ppm, "ppm", "ALARMA" if r.get(4) else "")

dd.run(loop, every_s=10)
