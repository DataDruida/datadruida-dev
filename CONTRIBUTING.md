# Cómo contribuir

¡Gracias por sumarte! Esta es la herramienta que usa
**https://app.datadruida.com.ar/developers**: lo que se mergea acá llega a
la página.

## Qué se puede mejorar

- **Sensores y placas nuevas** → `runtime/dd/sensors.py`, `runtime/dd/board.py`
  y `catalog/catalog.json`.
- **Ejemplos** → `examples/` (uno por archivo, con la cabecera
  `# Hola Mundo N — Título`, placa y sensores).
- **Runtime** (`runtime/dd/`) → con cuidado: es lo que protege equipos de 220 V.

## Flujo

1. Hacé un fork (o una rama, si sos del equipo) y trabajá en una rama propia.
2. Corré los tests: `python tests/test_runtime.py` (Python 3.10+, no hace
   falta placa: hay una simulada en `tests/fakes.py`).
3. Abrí un pull request contando qué cambia y **en qué hardware lo probaste**.
4. Los tests corren solos en GitHub. Alguien del equipo revisa y mergea.

## Reglas del runtime (no negociables)

- Los relays arrancan **apagados** y sin pulso (latch antes que CONFIG).
- `sensor.read()` **nunca lanza**: devuelve `None` si falla.
- Ante la duda, **apagar** es lo seguro (sensor caído, error, hora no confiable).
- Nada de credenciales en el código: el WiFi va en `/dd_config.json`.
- Todo cambio en `runtime/dd/` suma o ajusta un test.

## Sellos del catálogo

| `status` | Significa |
|---|---|
| `verified` | Probado en hardware por DataDruida |
| `unverified` | Escrito desde la ficha, sin probar |
| `community` | Aporte de la comunidad, sin verificar |

Un sensor nuevo entra como `community` o `unverified` y pasa a `verified`
cuando alguien del equipo lo prueba en una placa real.

## Versión

Si cambiás algo en `runtime/dd/`, subí `VERSION` en `runtime/dd/__init__.py`:
la página recarga el runtime en la placa sólo cuando la versión cambia.
