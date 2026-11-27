"""Servicio de compilación de DataDruida Developers.

Recibe el programa del usuario (un app.c contra dd.h), lo compila junto con la
base de la placa (WiFi, OTA, vuelta atrás) y devuelve el .bin listo para OTA.

    POST /build   Authorization: Bearer <DD_BUILD_TOKEN>
      {"app": "dd-pantalla" | "dd-reles", "code": "<contenido de app.c>"}
    → 200 {"ok": true, "size": n, "sha256": "...", "seconds": s, "bin_b64": "..."}
    → 200 {"ok": false, "errors": [{"line", "col", "kind", "msg"}], "log": "..."}
      (errores de compilación: van a Claude para que corrija solo)
    → 400 código rechazado por el filtro · 401 sin token · 429 ocupado

    GET /health → {"ok": true, "apps": [...], "busy": bool}

Rápido porque cada app tiene su carpeta de build "caliente": la base ya está
compilada y por pedido sólo se recompila app.c y se enlaza.

Variables de entorno:
  DD_IDF_ROOT     carpeta idf/ de datadruida-dev (por defecto: la de este archivo)
  DD_WORK_DIR     copia de trabajo (opcional): se copia idf/ ahí y se compila
                  sobre la copia, así los pedidos nunca tocan el repo
  DD_BUILD_TOKEN  token compartido con la webApp (obligatorio)
  DD_PORT         puerto (8080)
  IDF_PATH        ESP-IDF v5.5.4 ya activado (export.sh / export.ps1)
"""
import base64
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

SRC = Path(os.environ.get("DD_IDF_ROOT") or Path(__file__).resolve().parent.parent)
WORK = os.environ.get("DD_WORK_DIR")
ROOT = Path(WORK) if WORK else SRC
TOKEN = os.environ.get("DD_BUILD_TOKEN", "")
PORT = int(os.environ.get("DD_PORT", "8080"))
MAX_CODE = 100_000
BUILD_TIMEOUT_S = 240

# app → nombre del proyecto (= nombre del .bin)
APPS = {"dd-pantalla": "dd-pantalla-43b", "dd-reles": "dd-reles"}

_lock = threading.Lock()   # una compilación a la vez: el VPS tiene pocos núcleos

# ── filtro del código recibido ────────────────────────────────────────────
# El contenedor no tiene secretos, pero igual se corta lo que permitiría leer
# archivos del servidor y devolverlos en el binario o en los errores.
_INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]*)[>"]', re.M)
_FORBIDDEN = [
    (re.compile(r"\.incbin", re.I), "no se permite .incbin"),
    (re.compile(r"^\s*#\s*embed\b", re.M), "no se permite #embed"),
    (re.compile(r"__has_include\s*\(\s*[<\"]/"), "no se permiten rutas absolutas"),
    (re.compile(r"^\s*#\s*(line|pragma\s+GCC\s+(push_options|optimize|target))\b", re.M), "directiva no permitida"),
]


def check_code(code: str):
    if not isinstance(code, str) or not code.strip():
        return "El programa está vacío"
    if len(code.encode("utf-8")) > MAX_CODE:
        return f"El programa supera {MAX_CODE // 1000} KB"
    # Normalizar lo que el preprocesador junta o traduce antes de leer:
    # "\<salto>" une líneas (#incl\<salto>ude) y "%:" es el dígrafo de "#".
    norm = re.sub(r"\\\r?\n", "", code).replace("%:", "#")
    for line in re.findall(r"^\s*#\s*include\b(.*)$", norm, re.M):
        if not re.match(r'\s*(<[^>]*>|"[^"]*")\s*(//.*|/\*.*)?$', line):
            return "#include sólo con el nombre entre comillas o <>, sin macros"
    code = norm
    for inc in _INCLUDE.findall(code):
        if inc.startswith(("/", "\\")) or ".." in inc or ":" in inc:
            return f'#include no permitido: "{inc}" (sólo nombres de cabeceras, sin rutas)'
    for rx, msg in _FORBIDDEN:
        if rx.search(code):
            return msg
    return None


# ── compilación ───────────────────────────────────────────────────────────
_ERR = re.compile(r"app\.c:(\d+):(\d+):\s*(fatal error|error|warning|note):\s*(.*)")


def idf_cmd(*args):
    idf = Path(os.environ["IDF_PATH"]) / "tools" / "idf.py"
    return [sys.executable, str(idf), *args]


def run_build(app: str, timeout=BUILD_TIMEOUT_S):
    d = ROOT / "apps" / app
    env = dict(os.environ, PYTHONIOENCODING="utf-8")
    if not (d / "build" / "build.ninja").exists():
        subprocess.run(idf_cmd("set-target", "esp32s3"), cwd=d, env=env, capture_output=True, timeout=900)
    p = subprocess.run(idf_cmd("build"), cwd=d, env=env, capture_output=True, timeout=timeout,
                       text=True, encoding="utf-8", errors="replace")
    return p.returncode, p.stdout + p.stderr


def build(app: str, code: str):
    d = ROOT / "apps" / app
    appc = d / "main" / "app.c"
    project = APPS[app]
    t0 = time.time()
    appc.write_text(code, encoding="utf-8")
    os.utime(appc, None)          # fecha nueva: si no, ninja puede no recompilar
    try:
        rc, log = run_build(app)
    except subprocess.TimeoutExpired:
        return {"ok": False, "errors": [{"line": 0, "col": 0, "kind": "error",
                                         "msg": "La compilación tardó demasiado"}], "log": ""}
    secs = round(time.time() - t0, 1)
    if rc != 0:
        errors, seen = [], set()
        for m in _ERR.finditer(log):
            key = (m.group(1), m.group(2), m.group(4))
            if key in seen or m.group(3) == "note":
                continue
            seen.add(key)
            errors.append({"line": int(m.group(1)), "col": int(m.group(2)),
                           "kind": "error" if "error" in m.group(3) else "warning", "msg": m.group(4).strip()})
        if not any(e["kind"] == "error" for e in errors):
            # error que no es de app.c (enlace: p. ej. una función que no existe)
            tail = [l for l in log.splitlines() if "error" in l.lower() or "undefined reference" in l]
            errors.append({"line": 0, "col": 0, "kind": "error",
                           "msg": " | ".join(x.strip() for x in tail[-4:]) or "Falló la compilación"})
        return {"ok": False, "seconds": secs, "errors": errors[:20], "log": "\n".join(log.splitlines()[-40:])}
    binf = d / "build" / f"{project}.bin"
    data = binf.read_bytes()
    warnings = [{"line": int(m.group(1)), "col": int(m.group(2)), "kind": "warning", "msg": m.group(4).strip()}
                for m in _ERR.finditer(log) if m.group(3) == "warning"]
    return {"ok": True, "app": app, "project": project, "seconds": secs, "size": len(data),
            "sha256": hashlib.sha256(data).hexdigest(), "warnings": warnings[:20],
            "bin_b64": base64.b64encode(data).decode()}


def prepare_work_dir():
    """Copia idf/ a DD_WORK_DIR (sin builds ni logs). Las carpetas build/ de la
    copia se conservan entre reinicios: son las que hacen rápido el servicio."""
    if not WORK:
        return
    ignore = shutil.ignore_patterns("build", "build-log.txt", "sdkconfig", "sdkconfig.old", "__pycache__", "build-service")
    for item in SRC.iterdir():
        if item.name in ("build-service",) or item.name.startswith("."):
            continue
        dst = ROOT / item.name
        if item.is_dir():
            shutil.copytree(item, dst, ignore=ignore, dirs_exist_ok=True)
        else:
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(item, dst)
    for app in APPS:   # la copia arranca con el app.c vacío de fábrica
        shutil.copy2(SRC / "apps" / app / "main" / "app.c", ROOT / "apps" / app / "main" / "app.c")
    print(f"[build] copia de trabajo en {ROOT}", flush=True)


def warm_up():
    """Deja cada app compilada con el app.c vacío: los pedidos después sólo
    recompilan app.c."""
    for app in APPS:
        appc = ROOT / "apps" / app / "main" / "app.c"
        print(f"[build] calentando {app}...", flush=True)
        t0 = time.time()
        rc, log = run_build(app, timeout=1800)
        print(f"[build] {app}: {'ok' if rc == 0 else 'FALLÓ'} en {time.time() - t0:.0f} s", flush=True)
        if rc != 0:
            print(log[-3000:], flush=True)


# ── HTTP ──────────────────────────────────────────────────────────────────
class Handler(BaseHTTPRequestHandler):
    def _send(self, code, obj):
        body = json.dumps(obj, ensure_ascii=False).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, fmt, *args):
        sys.stderr.write("[http] " + fmt % args + "\n")

    def do_GET(self):
        if self.path == "/health":
            return self._send(200, {"ok": True, "apps": list(APPS), "busy": _lock.locked()})
        self._send(404, {"error": "no existe"})

    def do_POST(self):
        if self.path != "/build":
            return self._send(404, {"error": "no existe"})
        if not TOKEN or self.headers.get("Authorization", "") != f"Bearer {TOKEN}":
            return self._send(401, {"error": "token inválido"})
        try:
            n = int(self.headers.get("Content-Length", "0"))
            if n > MAX_CODE * 2:
                return self._send(400, {"error": "pedido demasiado grande"})
            req = json.loads(self.rfile.read(n) or b"{}")
        except (ValueError, json.JSONDecodeError):
            return self._send(400, {"error": "JSON inválido"})
        app, code = req.get("app"), req.get("code")
        if app not in APPS:
            return self._send(400, {"error": f"app desconocida (válidas: {', '.join(APPS)})"})
        bad = check_code(code)
        if bad:
            return self._send(400, {"error": bad})
        # Un pedido espera hasta 2 min a que termine el anterior; si no, 429.
        if not _lock.acquire(timeout=120):
            return self._send(429, {"error": "el compilador está ocupado, probá en un minuto"})
        try:
            res = build(app, code)
        finally:
            _lock.release()
        print(f"[build] {app}: {'ok' if res['ok'] else 'error'} en {res.get('seconds')} s", flush=True)
        self._send(200, res)


def main():
    if not TOKEN:
        sys.exit("Falta DD_BUILD_TOKEN")
    if "IDF_PATH" not in os.environ:
        sys.exit("Falta IDF_PATH: activar ESP-IDF antes (export.sh / export.ps1)")
    prepare_work_dir()
    if "--no-warm" not in sys.argv:
        warm_up()
    srv = ThreadingHTTPServer(("0.0.0.0", PORT), Handler)
    print(f"[build] escuchando en :{PORT}", flush=True)
    srv.serve_forever()


if __name__ == "__main__":
    main()
