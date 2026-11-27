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
  build-service/           servicio de compilación (server.py + Dockerfile)
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
| `POST /api/pair` `{action:"start"\|"cancel"}` | sí | muestra / cancela el código para vincular a una cuenta |
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

## Servicio de compilación (`build-service/`)

Recibe el `app.c` del usuario, lo compila con la base de la placa y devuelve el
`.bin` para OTA. Python sin dependencias + ESP-IDF; en el VPS corre en Docker.

```
POST /build   Authorization: Bearer <DD_BUILD_TOKEN>
  {"app": "dd-pantalla" | "dd-reles", "code": "<app.c>"}
→ {"ok": true,  "seconds": 15.1, "size": ..., "sha256": "...", "warnings": [], "bin_b64": "..."}
→ {"ok": false, "errors": [{"line": 3, "col": 26, "kind": "error", "msg": "expected ';' before '}' token"}]}
```

- **Rápido porque está "caliente":** cada app ya está compilada con el `app.c`
  vacío; un pedido sólo recompila `app.c` y enlaza. Medido en la PC de
  escritorio (Windows, 12 hilos): **15–17 s** por pedido; un error de
  compilación vuelve en **2 s**. Calentar de cero: ~4 min la pantalla, ~2 min relés.
- **Errores para Claude:** línea, columna y mensaje de gcc (incluye el
  "did you mean…" de funciones que no existen), para corregir solo.
- **Una compilación a la vez** (lock); un pedido espera hasta 2 min y si no, 429.
- **Copia de trabajo** (`DD_WORK_DIR`): los pedidos nunca tocan el repo.
- **Filtro del código** antes de compilar: `#include` sólo con nombre (sin
  rutas, `..`, unidades ni macros), sin `.incbin`, `#embed`, `#line` ni
  `#pragma GCC optimize/target`; normaliza `\`+salto y el dígrafo `%:`
  antes de revisar. Tope 100 KB.
- **Aislamiento en el VPS:** usuario sin privilegios, sin secretos en la
  imagen, topes de CPU/memoria/procesos (`docker run --cpus 3 --memory 4g
  --pids-limit 512`).

En esta PC (sin Docker):

```powershell
. C:\esp\esp-idf\export.ps1; $env:DD_BUILD_TOKEN="..."; $env:DD_WORK_DIR="C:\dd-build-work"; python idf\build-service\server.py
```

En el VPS:

```bash
docker build -f idf/build-service/Dockerfile -t dd-build idf/
docker run -d --name dd-build --restart unless-stopped -e DD_BUILD_TOKEN=... -p 8080:8080 --cpus 3 --memory 4g --pids-limit 512 dd-build
```

⚠ El Dockerfile no se probó todavía (esta PC no tiene Docker).
Lo de "versión por programa" quedó resuelto en el paso 3 (abajo): una placa
vinculada decide por id de compilación, no por versión.

## Vincular la placa a una cuenta y recibir programas (paso 3)

Código: `components/dd_core/dd_cloud.c` (firmware) y, en `druida-web-deploy`,
`app/api/dd/{sync,claim,devices,deploy}` + `lib/dd/server.ts` + la vista
`components/developers/studio/BoardsView.tsx` ("Mis placas" en /developers).
Migración `0046_dd_devices.sql` (corrida el 27-nov-2026).

**Cómo se usa:**
1. En la placa: **Vincular a mi cuenta** (pestaña Inicio de la pantalla, o el
   panel `http://dd-xxxxxx.local` en las placas sin pantalla). Muestra un código
   de 6 letras (sin 0/O/1/I) que vence a los 10 min.
2. En `/developers` → **Mis placas**: escribir el código. La placa se entera en
   ≤4 s ("Vinculada a br***@gmail.com").
3. Vinculada, la placa consulta `POST /api/dd/sync` **cada 1 min**. Si su cuenta
   le asignó un programa, recibe una URL firmada (1 h) del bucket `dd-builds`, lo
   baja e instala solo, con la vuelta atrás de siempre.

**Identidad:** la placa se presenta con su MAC completa (`uid`, 12 hex) y un
secreto de 32 hex que genera en el primer arranque (NVS `"secret"`). La nube
guarda sólo el sha256. El código viaja junto con el hash del secreto y, al
vincular, ese hash pasa a ser el de la placa: el que tiene la placa en la mano
es el que la vincula. Otra placa que use la misma MAC recibe **403** y no baja
nada. Una placa reflasheada desde cero (secreto nuevo) también recibe 403 hasta
que el dueño la desvincule en Mis placas.

**Qué instalar se decide por id de compilación, no por versión.** El id es el
SHA-256 del ELF (16 hex), que ESP-IDF graba en el descriptor de la app
(`esp_app_get_elf_sha256` en la placa; `parseAppDesc()` lo lee del `.bin` en la
webApp: offset 0x20+144). Verificado que coinciden en los dos binarios. Reglas
(`dd_ota_offer()` en `dd_ota.c`):
- update **con** `build` (programa de la cuenta) → se instala si es otro id, y
  nunca el que ya falló (`bad_build`, sale de `esp_ota_get_last_invalid_partition`).
  Se instala aunque esté apagado "Instalar solas": el usuario lo mandó a propósito.
- update **sin** `build` (base pública) → sólo si la versión es mayor.
- Vinculada, la placa **no** lee el manifest público: manda la nube. Si la cuenta
  no le asignó ningún programa, `/api/dd/sync` le devuelve la base pública.

**La webApp** marca el programa `installed` cuando la placa reporta que corre ese
id (y ya no está a prueba), y `failed` cuando lo reporta como `bad_build` — ahí
deja de ofrecérselo. `deploy` rechaza un `.bin` de otro proyecto (pantalla en una
placa de relés o al revés) leyendo el `project_name` del descriptor.

**Probar sin placa:** `tools/sim_placa.py` habla con `/api/dd/sync` igual que el
firmware (`code` registra un código y espera la vinculación; `poll` consulta e
"instala" bajando el `.bin` y comprobando su id; `fail` simula un programa que
no arranca). Para la webApp local con el compilador local:
`druida-web-deploy/.env.development.local` con `DD_BUILD_URL` y `DD_BUILD_TOKEN`
(ignorado por git).

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
- **Paso 3 (27-nov-2026), vinculación + programas por cuenta: PROBADO DE PUNTA A
  PUNTA con la placa simulada** (webApp local + compilador local, cuenta admin):
  vincular con el código (la placa ve "vinculada a bm***"), mandar un app.c
  (compila en ~20 s, la placa lo baja, el id coincide, la web dice "Corriendo tu
  programa"), error de compilación (línea y columna, no se manda nada) y programa
  que no arranca (`fail`: la web dice "El programa no arrancó: la placa volvió al
  anterior" y no se lo vuelve a ofrecer). Falta probarlo en una placa real.
  - Ojo al probar: dos `sim_placa.py code` con el mismo estado se pisan el código
    cada 4 s ("Ese código no existe o ya venció"). Una sola a la vez.
  - Login en `localhost`: Supabase devuelve el login de Google a datadruida.com.ar.
    Atajo: copiar el `?code=` de esa URL a
    `http://localhost:3000/auth/callback?code=<code>&next=/developers` (el
    verificador quedó en la cookie de localhost). Lo definitivo es agregar
    `http://localhost:3000/**` en Supabase → Authentication → Redirect URLs.
  - Los programas salen con la versión del repo (hoy **0.1.5**); la base nueva para
    el comprador tiene que ser 0.1.6 o más (abajo).
  - `/api/dd/sync` le ofrece la base pública 0.1.3 a una placa vinculada sin
    programa aunque tenga una versión mayor; la placa la ignora (sólo instala
    versiones mayores), pero sobra.
  Quedan filas de prueba en `dd_devices` (`dd00000000b6`, `dd000000006d`).
- ⚠ Para que la 4.3B del comprador reciba la vinculación, publicar la base nueva
  como **0.1.6 o más** (tiene la 0.1.5).
- ⚠ En producción `/api/dd/deploy` responde 503 hasta configurar
  `DD_BUILD_URL`/`DD_BUILD_TOKEN` en Vercel con el servicio en un VPS.
