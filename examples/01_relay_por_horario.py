# Hola Mundo 1 — Relay por horario
# Placa: Waveshare ESP32-S3-ETH-8DI-8RO · Sin sensores
# Prende el relay 1 de 08:00 a 20:00 (luz, por ejemplo). Sin hora confiable
# (sin WiFi ni RTC en hora) queda apagado: es lo seguro.
import dd

r = dd.relays()

def loop():
    r.set(1, dd.in_window("08:00", "20:00"))

dd.run(loop, every_s=10)
