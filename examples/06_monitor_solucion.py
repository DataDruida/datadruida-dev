# Hola Mundo 6 — Monitor de solución nutritiva
# Placa: Waveshare ESP32-S3-ETH-8DI-8RO
# Sensores: Seeed S-pH&ORP-01A (ID 3) y Seeed S-EC-01 (ID 4)
# Lee pH, ORP, EC y temperatura del agua y avisa si se van de rango.
# Sólo mide: no dosifica (eso lo agregás vos con un relay y una bomba).
import dd

ph = dd.sensor("seeed-s-ph-orp-01a", 3)
ec = dd.sensor("seeed-s-ec-01", 4)

PH_MIN, PH_MAX = 5.5, 6.5
EC_MIN, EC_MAX = 800, 2000   # µS/cm

def loop():
    a = ph.read() or {}
    b = ec.read() or {}
    print("pH", a.get("ph"), " ORP", a.get("orp"), "mV  EC", b.get("ec"), "µS/cm  T", a.get("water_temp"))
    if a.get("ph") is not None and not PH_MIN <= a["ph"] <= PH_MAX:
        print("  ⚠ pH fuera de rango")
    if b.get("ec") is not None and not EC_MIN <= b["ec"] <= EC_MAX:
        print("  ⚠ EC fuera de rango")

dd.run(loop, every_s=10, wifi=False)
