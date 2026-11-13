# Pantalla 1 — Temperatura y humedad en la pantalla
# Placa: Waveshare ESP32-S3-Touch-LCD-2 · Sensor: SLICETEX TH-MB-02S (ID 1)
# El sensor va con un módulo TTL-RS485 (dirección automática) en TX 17 / RX 18.
# Tocá la pantalla para alternar entre brillo alto y bajo.
import dd

th = dd.sensor("slicetex-th-mb-02s", 1)
scr = dd.screen()
alto = [True]

def loop():
    if scr.touch():
        alto[0] = not alto[0]
        scr.brightness(100 if alto[0] else 20)

    v = th.read()
    scr.fill(dd.BLACK)
    scr.center("DataDruida", 12, dd.GRAY, size=2)
    if v is None:
        scr.center("sin sensor", 140, dd.RED, size=3)
        scr.center("revisa A/B y el modulo", 180, dd.GRAY)
    else:
        scr.text("Temperatura", 16, 60, dd.GRAY)
        scr.text("%.1f C" % v["temp"], 16, 76, dd.ORANGE, size=5)
        scr.bar(16, 124, 208, 8, v["temp"], 0, 40, dd.ORANGE)
        scr.text("Humedad", 16, 150, dd.GRAY)
        scr.text("%.0f %%" % v["hum"], 16, 166, dd.CYAN, size=5)
        scr.bar(16, 214, 208, 8, v["hum"], 0, 100, dd.CYAN)
        vpd = dd.vpd(v["temp"], v["hum"])
        scr.text("VPD %.2f kPa" % vpd, 16, 240, dd.GREEN, size=2)
    b = dd.battery()
    if b is not None:
        scr.text("bat %.2f V" % b, 16, 296, dd.GRAY)
    scr.show()

dd.run(loop, every_s=1)
