"""Placa simulada: habla con /api/dd/sync igual que dd_cloud.c.

  python sim_placa.py <base_url> <estado.json> code      → registra un código y espera la vinculación
  python sim_placa.py <base_url> <estado.json> poll [n]  → n consultas vinculada; si hay update lo
                                                          "instala" (baja el .bin, lee su build) y
                                                          reporta que corre ese build
  python sim_placa.py <base_url> <estado.json> fail      → como poll, pero el programa "falla"
                                                          (reporta bad_build y vuelve al anterior)
"""
import json, os, random, secrets, struct, sys, time, urllib.request, urllib.error

BASE, STATE, CMD = sys.argv[1], sys.argv[2], sys.argv[3]
st = json.load(open(STATE)) if os.path.exists(STATE) else {
    "id": "dd00000000" + secrets.token_hex(1), "secret": secrets.token_hex(16),
    "app": "dd-pantalla-43b", "version": "0.1.6", "build": "537f4606b7ae626c", "bad": "",
}
save = lambda: json.dump(st, open(STATE, "w"))


def sync(code=None):
    body = {"id": st["id"], "secret": st["secret"], "app": st["app"], "board": "ESP32-S3-Touch-LCD-4.3B",
            "version": st["version"], "build": st["build"], "bad_build": st["bad"],
            "pending_verify": False, "safe_mode": False, "ip": "192.168.0.99"}
    if code: body["code"] = code
    req = urllib.request.Request(BASE + "/api/dd/sync", json.dumps(body).encode(), {"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            return r.status, json.loads(r.read())
    except urllib.error.HTTPError as e:
        return e.code, json.loads(e.read() or b"{}")


def build_of(url):
    b = urllib.request.urlopen(url, timeout=60).read()
    d = 0x20
    assert b[0] == 0xE9 and struct.unpack_from("<I", b, d)[0] == 0xABCD5432, "no es un .bin de ESP-IDF"
    return b[d + 144:d + 152].hex(), len(b)


if CMD == "code":
    code = "".join(random.choice("ABCDEFGHJKLMNPQRSTUVWXYZ23456789") for _ in range(6))
    st["code"] = code; save()
    print("placa", st["id"], "muestra el código", code, flush=True)
    for i in range(150):                       # 10 min a 4 s, como el firmware
        s, r = sync(code)
        if i == 0: print("1ra consulta:", s, r, flush=True)
        if s == 200 and r.get("paired"):
            print("VINCULADA a", r.get("owner"), "| update:", r.get("update"), flush=True)
            break
        time.sleep(4)
    save()
elif CMD in ("poll", "fail"):
    n = int(sys.argv[4]) if len(sys.argv) > 4 else 1
    for _ in range(n):
        s, r = sync()
        u = r.get("update") if s == 200 else None
        print("consulta:", s, {k: v for k, v in r.items() if k != "update"}, "| update:",
              None if not u else {**u, "url": u["url"][:70] + "…"}, flush=True)
        if u and u.get("build") and u["build"] not in (st["build"], st["bad"]):
            got, size = build_of(u["url"])
            print(f"  bajé {size} bytes, build del .bin = {got} (manifest dice {u['build']})", flush=True)
            assert got == u["build"], "el build del .bin no coincide"
            if CMD == "fail":
                st["bad"] = got
                print("  el programa no arrancó: vuelvo al anterior y reporto bad_build", flush=True)
            else:
                st["build"] = got
                print("  instalado, ahora corro", got, flush=True)
            save()
            s, r = sync()
            print("  consulta tras reiniciar:", s, {k: v for k, v in r.items() if k != "update"}, "| update:", r.get("update"), flush=True)
        time.sleep(1)
    save()
