# DataDruida para developers

Programá placas ESP32 **sin compilar**: conectás la placa por USB, elegís
sensores, le pedís a Claude lo que querés en castellano y la página carga el
programa en la placa con un botón. Todo desde el navegador.

👉 **https://app.datadruida.com.ar/developers**

Este repo es la parte abierta (licencia **Apache-2.0**): el runtime `dd` para
MicroPython, el catálogo de placas y sensores, y los ejemplos.

## Cómo sumarte

- **Usar la herramienta:** pedí acceso en https://app.datadruida.com.ar/developers
  (cuenta de DataDruida + una solicitud corta; mientras haya cupo, entrás en el momento).
- **Mejorar el código:** abrí un *issue* (bug o sensor nuevo, con las plantillas) o un
  *pull request* siguiendo [CONTRIBUTING.md](CONTRIBUTING.md). Si dejás tu usuario de
  GitHub en la solicitud, te sumamos al equipo del repo.
- **Instalar y vender equipos:** si sos electricista, instalador o integrador y querés
  trabajar con DruidaBot, contalo en la solicitud: tenemos un programa de instaladores.

## Qué es abierto y qué no

DataDruida es *open core*: abrimos lo que corre en tu placa y cobramos lo que
hace falta para que un equipo funcione todos los días sin que lo mires.

| Pieza | | |
|---|---|---|
| Runtime `dd`, catálogo, ejemplos (este repo) | **Abierto** · Apache-2.0 | Leelo, cambialo, llevalo a otra placa. |
| API pública v1 y formatos de datos | **Abierto** · documentado | Cualquiera puede integrarse con DataDruida. |
| Plano del tablero y guías de instalación | **Abierto** · público en la web | |
| Firmware del DruidaBot 3.0 | **Cerrado** | Fail-safe, OTA, reloj, cola sin red, interlocks: probado en equipos en producción. |
| Nube DataDruida (app, histórico, alertas, IA, compartir) | **Cerrado** · servicio | Incluida con el DruidaBot; para placas propias, con licencia (próximamente). |
| Marcas "DataDruida" y "DruidaBot" | **Reservadas** | Ver [TRADEMARKS.md](TRADEMARKS.md). |

El código que Claude genera para vos en la página es **tuyo**: hacé con él lo
que quieras.

## Cómo funciona

1. **Una sola vez:** la página instala MicroPython oficial en la placa
   (ESP Web Tools). Esto borra lo que tenía la placa.
2. **Cada programa:** la página sube el runtime `dd` (si falta o cambió) y tu
   `main.py` por Web Serial (raw REPL de MicroPython) y reinicia la placa.
   No se compila nada en ningún lado.

Navegadores: Chrome, Edge u Opera en computadora (Web Serial). No funciona
en iPhone/iPad.

## Placas

| Placa | Estado |
|---|---|
| Waveshare ESP32-S3-ETH-8DI-8RO (8 relays, 8 DI, RS485, RTC) | Probado por DataDruida |

## Sensores (RS485 Modbus, todos a 4800 8N1)

| Sensor | id en el catálogo | Estado |
|---|---|---|
| SLICETEX TH-MB-02S | `slicetex-th-mb-02s` | Probado |
| SLICETEX SOIL-MB-THC-S | `slicetex-soil-mb-thc-s` | Probado |
| SLICETEX SOIL-MB-NPKPHCTH-S | `slicetex-soil-mb-npkphcth-s` | Sin probar (mapa NPK/pH) |
| Seeed S-CO2-03 | `seeed-s-co2-03` | Probado |
| Seeed S-EC-01 | `seeed-s-ec-01` | Sin probar |
| Seeed S-pH&ORP-01A | `seeed-s-ph-orp-01a` | Probado |
| Seeed S-Temp&VWC&EC-04A | `seeed-s-temp-vwc-ec-04a` | Probado |

Los Seeed salen de fábrica a 9600: se pasan a 4800 una vez, con ese único
sensor en el bus, y después se desenchufan y se vuelven a enchufar:

```python
import dd.sensors
dd.sensors.seeed_setup("seeed-s-co2-03", 45)
```

## El runtime `dd` en 30 segundos

```python
import dd

th = dd.sensor("slicetex-th-mb-02s", 1)       # dict o None, nunca explota
r = dd.relays()                               # relays 1..8
extractor = dd.Hysteresis(low=25, high=28, mode="down")

def loop():
    v = th.read()
    r.set(2, extractor.update(v["temp"] if v else None))
    r.set(1, dd.in_window("08:00", "20:00"))

dd.run(loop, every_s=5)
```

| API | Qué hace |
|---|---|
| `dd.relays()` | `set(n, on)`, `on(n)`, `off(n)`, `get(n)`, `all_off()`, `set_max_on(n, s)`, `reset(n)`, `state()` |
| `dd.sensor(id, addr)` | `.read()` → dict o `None` |
| `dd.in_window("HH:MM", "HH:MM")` | ventana horaria (cruza medianoche; sin hora confiable da `False`) |
| `dd.Hysteresis(low, high, mode)` | `mode="down"` enfría/extrae, `"up"` calienta/humidifica/riega |
| `dd.Cycle(on_s, off_s)` | pulso/pausa |
| `dd.vpd(temp, hum)` | VPD en kPa |
| `dd.inputs.closed(n)` | entrada digital DIn cerrada |
| `dd.run(loop, every_s)` | bucle con seguridad (ver abajo) |

### Seguridad incluida

- Los relays arrancan apagados, sin pulso al encender (latch antes que CONFIG).
- Cada 1 s se re-asienta el chip de relays si un pico de tensión lo reseteó.
- `set_max_on(n, s)`: tope de encendido continuo por relay; si se cumple, el
  relay se apaga y queda bloqueado.
- Watchdog por software: programa colgado → todo apagado y reinicio.
- Error en tu programa → relays apagados; 3 errores seguidos → se queda apagado.
- Sensor caído → `None` → `Hysteresis` apaga.
- Sin hora confiable → los horarios quedan apagados.
- El WiFi va en `/dd_config.json` (lo escribe la página), nunca en el código.

⚠ Los relays manejan 220 V. Nunca conectes una carga en el contacto NC.

## Ejemplos

`examples/01..06`: relay por horario, temperatura/humedad en vivo,
termostato, riego por humedad de suelo, alarma de CO₂, monitor de solución.

## Tests (en la PC, con placa simulada)

```bash
python tests/test_runtime.py
```

## Publicar en la web

```bash
node tools/sync-web.mjs ../druida-web-deploy
```

Copia `runtime/`, `catalog/` y `examples/` a `public/developers/` del sitio.

## Contribuir

Ver [CONTRIBUTING.md](CONTRIBUTING.md). Bugs y sensores nuevos: abrí un
issue con la plantilla que corresponda. Para que podamos mergear un pull
request hace falta aceptar el [acuerdo de contribución (CLA)](CLA.md).

## Licencia

Código: [Apache-2.0](LICENSE) (ver también [NOTICE](NOTICE)). Hasta el
12-nov-2026 el repo estaba bajo MIT; lo publicado antes de esa fecha se puede
seguir usando bajo MIT. Marcas: [TRADEMARKS.md](TRADEMARKS.md).

## Niveles de soporte

1. **Ejemplos oficiales** — los mantiene DataDruida.
2. **Código generado por IA** — lo revisás vos antes de cargarlo.
3. **Comunidad** — se muestra con el sello "Comunidad, sin verificar".
