# DataDruida en ESP-IDF

Base nueva de la plataforma developers: las placas llegan con un firmware
DataDruida en **ESP-IDF** (C, sin MicroPython) que se conecta al WiFi del
usuario y desde ahí **se actualiza por OTA**. El USB se usa una sola vez,
en el taller, antes de despachar la placa.

```
idf/
  components/dd_core/      base común: WiFi, portal, OTA, rollback, modo seguro, panel web
  components/dd_app/       API del programa del usuario (dd.h): relés, entradas, sensores, hora, control, pantalla
  boards/lcd43b/           drivers de la Waveshare ESP32-S3-Touch-LCD-4.3B (CH422G, RGB, GT911)
  apps/dd-pantalla/        firmware de prueba con pantalla (4.3B, LVGL 9.4)
  apps/dd-reles/           firmware de prueba sin pantalla (ETH-8DI-8RO, Relay-6CH, Relay-1CH)
  partitions-16mb.csv      tabla de particiones común (A/B de 6 MB + 3,8 MB de datos)
  examples/                programas de ejemplo en C (un app.c cada uno)
  build.ps1                compilar / grabar
  tools/release.mjs        publicar una versión para OTA
  tools/panel-mock.mjs     simulador del panel web para diseñarlo sin placa
```

## Qué vive la persona que compra la placa

**Con pantalla (4.3B):**
1. La enchufa. La pantalla dice "¡Hola! Tu placa ya tiene DataDruida" (la placa es Waveshare; DataDruida es el firmware) con tres pasos.
2. Toca *Elegir mi WiFi* → *Buscar redes* → toca la suya → escribe la contraseña
   con el teclado táctil → *Conectar*. (O, si prefiere, *Usar el celular*:
   abre la red `DataDruida-XXXX` con portal cautivo.)
3. Queda conectada. La pantalla muestra un QR al panel de la placa, su
   dirección `http://dd-xxxxxx.local` y la **clave del equipo**.
4. Las versiones nuevas aparecen en *Actualizar* → *Instalar*, con barra de
   progreso. Nunca hace falta el cable.

**Sin pantalla (placas de relés):**
1. La enchufa. Aparece la red WiFi abierta `DataDruida-XXXX`.
2. Se conecta con el celular: se abre sola la página de configuración
   (portal cautivo), que muestra la clave del equipo, busca redes y pide la
   contraseña.
3. La placa se pasa a la red de la casa y cierra la suya. El panel queda en
   `http://dd-xxxxxx.local`: probar relés, ver entradas, actualizar.
4. Actualiza sola (se puede apagar en el panel).

## Cómo se protege de un firmware roto (sin USB)

| Riesgo | Qué pasa |
|---|---|
| La imagen nueva no arranca o se cuelga | Arranca "a prueba" (`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`). Si se reinicia antes de confirmarse, el bootloader vuelve solo a la anterior. |
| Arranca pero rompe el WiFi | Sólo se confirma tras conectar al WiFi y andar 20 s. Si hay red guardada y en 3 min no conecta → vuelta atrás. |
| Se confirma pero después se cae en bucle | 3 reinicios por error seguidos (antes de 60 s de uso) → **modo seguro**: arranca sin la app, con portal + OTA, para instalar una versión corregida. |
| La misma versión rota se vuelve a bajar | La versión que falló queda marcada y no se reintenta. |
| Se sube un .bin de otro chip | Se rechaza antes de escribir (chip_id del header). |
| Alguien en la red de la casa | Todo lo que modifica pide la clave del equipo (header `X-DD-Key`). La clave sólo se muestra en la pantalla o en la red de configuración (hay que estar al lado). |

En las placas sin pantalla, **mantener BOOT 3 s al encender** también entra al
modo seguro. En la 4.3B no existe esa opción: GPIO0 es una línea de datos del LCD.

⚠ **La tabla de particiones no se puede cambiar por OTA.** Es lo único que
obliga a pasar por USB; por eso es común y tiene margen (dos apps de 6 MB).
⚠ **El bootloader tampoco** (y es el que hace la vuelta atrás): el primer
grabado tiene que ser este, completo.

## Compilar y grabar (taller)

Requiere ESP-IDF v5.5.4 en `C:\esp\esp-idf`.

```powershell
.\idf\build.ps1 dd-pantalla
```
```powershell
.\idf\build.ps1 dd-pantalla flash COM94
```

El `flash` graba todo (bootloader, tabla, otadata y app). Es la única vez que
se usa el USB. Antes de despachar: encender, verificar que aparece la
pantalla de bienvenida (o la red `DataDruida-XXXX`) y desconectar.

⚠ Relay-1CH: confirmar que el módulo es de 16 MB (`esptool flash_id`) antes de
grabar `dd-reles`; ETH-8DI-8RO y Relay-6CH están confirmadas en 16 MB.

## Publicar una versión (OTA)

1. Subir `PROJECT_VER` en `apps/<app>/CMakeLists.txt` y compilar.
2. `node idf/tools/release.mjs dd-reles --notes "qué cambió"` → copia el .bin y
   escribe `manifest.json` en `druida-web-deploy/public/firmware/dd-idf/<app>/`.
3. Commit + push de `druida-web-deploy` (Vercel lo publica). Los equipos lo
   ven al conectarse y cada 6 h.

El equipo lee `https://app.datadruida.com.ar/firmware/dd-idf/<app>/manifest.json`:

```json
{ "version": "0.2.0", "url": "dd-reles-0.2.0.bin", "sha256": "...", "notes": "..." }
```

La URL del manifest se puede cambiar por equipo (`POST /api/settings
{"manifest":"https://..."}`): es el enganche para que, más adelante, cada
placa siga el firmware que su dueño compila desde app.datadruida.com.ar/developers.

## API del panel local

| Ruta | Clave | Qué hace |
|---|---|---|
| `GET /api/status` | no | estado (sin la clave, salvo desde la red de configuración) |
| `GET /api/scan` | sí | redes cercanas |
| `POST /api/wifi` `{ssid,pass}` | sí | guarda y conecta |
| `POST /api/ota/check` / `install` | sí | buscar / instalar desde la nube |
| `POST /api/ota/upload` (body = .bin) | sí | instalar un .bin local |
| `POST /api/settings` `{auto_update, manifest}` | sí | |
| `POST /api/restart` | sí | |
| `POST /api/relay` `{ch,on}`, `/api/relays/test`, `/api/relays/off`, `/api/board` | sí | sólo `dd-reles` |

## El programa del usuario: `app.c` + `dd.h`

Cada firmware compila un único archivo de usuario, `main/app.c`, contra la API
de `components/dd_app/include/dd.h`. Vacío de fábrica (la placa sólo espera un
programa); el servicio de compilación de /developers va a reemplazar **sólo**
ese archivo, así la base (WiFi, OTA, vuelta atrás) va siempre incluida.

```c
#include "dd.h"

static dd_sensor_t *th;
static dd_hyst_t extractor = DD_HYST(25.0, 28.0, DD_BAJA);

void dd_setup(void) { th = dd_sensor("slicetex-th-mb-02s", 1); dd_every(5000); }

void dd_loop(void) {
    dd_reading_t v;
    float t = dd_sensor_read(th, &v) ? v.temp : NAN;
    dd_relay_set(2, dd_hyst_update(&extractor, t));
    dd_log("T %.1f extractor %s", t, dd_relay_get(2) ? "ON" : "OFF");
}
```

Mismas ideas que el runtime MicroPython (`dd.relays()`, `dd.sensor()`,
`dd.in_window()`, `Hysteresis`, `Cycle`, `vpd`), con `dd_setup`/`dd_loop`
al estilo Arduino. Lo que el sistema hace solo: relés apagados al arrancar,
hora por internet (UTC-3; sin hora, `dd_in_window()` da false), watchdog de
60 s que apaga relés y reinicia, re-asentado del TCA9554, topes de tiempo
encendido (`dd_relay_max_on`), valores faltantes = `NAN` → los controles apagan.

En placas con pantalla el programa dibuja con LVGL 9 en la pestaña
**"Mi programa"** (`dd_ui_root()`, siempre entre `dd_ui_lock()`/`dd_ui_unlock()`,
y leyendo sensores fuera del lock). `dd_log()` sale por serie y en la sección
"Tu programa" del panel web (`GET /api/log`, con clave): se depura sin USB.

```powershell
.\idf\build.ps1 dd-reles -Program examples\02_termostato.c
```

| Ejemplo | Placa | Qué hace |
|---|---|---|
| `01_relay_por_horario.c` | con relés | relé 1 de 08:00 a 20:00 |
| `02_termostato.c` | con relés + TH-MB-02S | extractor con banda 25–28 °C |
| `03_riego_por_humedad.c` | con relés + SOIL-MB-THC-S | riego por pulsos con tope de 60 s |
| `04_pantalla_hola.c` | 4.3B | hora en grande + botón que cuenta toques |
| `05_pantalla_clima.c` | 4.3B + TH-MB-02S | temperatura, humedad y VPD |

⚠ **`main` se enlaza con `WHOLE_ARCHIVE`.** `dd_app` trae `dd_setup`/`dd_loop`
vacíos como `weak`; sin `WHOLE_ARCHIVE` el enlazador se queda con esos y
descarta los de `app.c` (compila sin errores y la placa corre un programa vacío).
El servicio de compilación tiene que conservar esto.

⚠ `build.ps1 -Program` le pone fecha nueva a `app.c`: `Copy-Item` conserva la
del original y ninja no lo recompilaba.

⚠ En la 4.3B el RS485 de la placa usa GPIO43/44, los mismos que la consola
UART0: al abrir el bus se corta el log por serie (sigue en el panel). Sin probar.

## Fuentes con acentos

Las Montserrat de LVGL son sólo ASCII. `tools/gen_fonts.py` (Pillow) genera
`dd_font_16/20/28` en el mismo formato compilado, con á é í ó ú ñ ¿ ¡ °, y usa
la Montserrat de LVGL como respaldo para los íconos `LV_SYMBOL_*`.

```powershell
python idf/tools/gen_fonts.py idf/apps/dd-pantalla/main/fonts/Montserrat-Medium.ttf idf/apps/dd-pantalla/main/fonts 16 20 28
```

⚠ **No usar TinyTTF** (dibujar la letra en el equipo): en la 4.3B reiniciaba la
placa la primera vez que aparecían muchas letras nuevas juntas (la lista de
redes, el teclado). Visto en hardware el 26-nov-2026.

## Estado (26-nov-2026)

- Compilan las dos apps sin warnings (`dd-pantalla-43b` 1,7 MB, `dd-reles` 1,0 MB).
- Panel web verificado en navegador con `tools/panel-mock.mjs`.
- **4.3B verificada en hardware (COM79, equipo dd-2a6d88):** pantalla, táctil,
  WiFi elegido desde la pantalla, mDNS, HTTPS al servidor de actualizaciones.
  La primera versión (TinyTTF) se reiniciaba al buscar redes y al abrir el
  teclado; con fuentes compiladas anda.
- **OTA local verificada:** 0.1.0 → 0.1.1 subiendo el .bin al panel por WiFi
  (1,5 MB en 7 s). Arrancó desde `ota_1` "a prueba", conectó en 2,5 s y a los
  22 s quedó confirmada ("Imagen nueva verificada"). Sin USB.
- **OTA desde la nube verificada:** 0.1.2 publicada en app.datadruida.com.ar
  (druida-web-deploy `07b3384`), la placa la vio, se instaló desde la pantalla y
  quedó confirmada. Después 0.1.3 (título de Inicio + punto del encabezado) por
  OTA local; la web sigue publicando 0.1.2.
- Falta probar una vuelta atrás real (subir una imagen que no conecte).
- `dd-reles` sin probar en hardware.
