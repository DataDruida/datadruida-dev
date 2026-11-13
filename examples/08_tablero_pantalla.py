# Pantalla 2 — Tablero táctil de clima
# Placa: Waveshare ESP32-S3-Touch-LCD-7B · Sensor: SLICETEX TH-MB-02S (ID 1)
# Necesita el firmware DataDruida con pantalla (LVGL). El sensor va al RS485 de la placa.
# Tres tarjetas (temperatura, humedad, VPD) y un botón para bajar el brillo.
import dd

th = dd.sensor("slicetex-th-mb-02s", 1)
ui = dd.ui()
ui.title("Clima de la sala")
temp = ui.card("Temperatura", "°C")
hum = ui.card("Humedad", "%")
vpd = ui.card("VPD", "kPa")
brillo = [100]

def cambiar_brillo():
    brillo[0] = 30 if brillo[0] == 100 else 100
    ui.brightness(brillo[0])

ui.button("Brillo", cambiar_brillo)

def loop():
    v = th.read()
    if v is None:
        temp.set(None)
        hum.set(None)
        vpd.set(None)
        return
    temp.set(v["temp"], color=0xF58C1E if v["temp"] > 30 else None)
    hum.set(v["hum"], fmt="%.0f")
    vpd.set(dd.vpd(v["temp"], v["hum"]), fmt="%.2f")

dd.run(loop, every_s=2)
