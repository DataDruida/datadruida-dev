// Simulador del panel local (components/dd_core/web/index.html) para
// diseñarlo en el navegador sin placa:   node idf/tools/panel-mock.mjs [puerto]
// ?ap=1 simula entrar desde la red de configuración (muestra la clave).
import http from 'node:http';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import path from 'node:path';

const dir = path.dirname(fileURLToPath(import.meta.url));
const html = readFileSync(path.join(dir, '../components/dd_core/web/index.html'));
const port = Number(process.argv[2] || 8090);

const S = {
  app: 'dd-reles', version: '0.1.0', board: 'waveshare-esp32-s3-eth-8di-8ro', device: 'a1b2c3',
  hostname: 'dd-a1b2c3', net: 'off', ssid: '', ip: '', rssi: 0, setup_ap: true, safe_mode: false,
  pending_verify: false, ota: 'idle', ota_progress: 0, ota_new_version: '', ota_msg: '', auto_update: true,
  has_screen: false, uptime_s: 300, heap_kb: 180,
  boards: [
    { id: 'waveshare-esp32-s3-eth-8di-8ro', name: 'ESP32-S3-ETH-8DI-8RO (8 relés)' },
    { id: 'waveshare-esp32-s3-relay-6ch', name: 'ESP32-S3-Relay-6CH (6 relés)' },
    { id: 'waveshare-esp32-s3-relay-1ch', name: 'ESP32-S3-Relay-1CH (1 relé)' },
  ],
  board_unknown: false, relays: Array(8).fill(false), inputs: [false, true, false, false, false, false, false, false],
};
const KEY = 'k7pq2mzx';

const body = (req) => new Promise((r) => { let b = ''; req.on('data', (c) => (b += c)); req.on('end', () => r(b)); });
const json = (res, code, o) => { res.writeHead(code, { 'Content-Type': 'application/json' }); res.end(JSON.stringify(o)); };

http.createServer(async (req, res) => {
  const u = new URL(req.url, 'http://x');
  const ap = (req.headers.referer || '').includes('ap=1');
  const authed = ap || req.headers['x-dd-key'] === KEY;
  if (u.pathname === '/') { res.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8' }); return res.end(html); }
  if (u.pathname === '/api/status') return json(res, 200, { ...S, via_setup_ap: ap, ...(ap ? { key: KEY } : {}), uptime_s: S.uptime_s++ });
  if (!authed) return json(res, 401, { error: 'Falta la clave del equipo' });
  const b = req.method === 'POST' ? await body(req) : '';
  const j = b && b[0] === '{' ? JSON.parse(b) : {};
  switch (u.pathname) {
    case '/api/scan':
      await new Promise((r) => setTimeout(r, 1200));
      return json(res, 200, { networks: [
        { ssid: 'Casa-Fibertel', rssi: -48, secure: true }, { ssid: 'Vecino 5G', rssi: -77, secure: true },
        { ssid: 'Invitados', rssi: -66, secure: false } ] });
    case '/api/wifi':
      Object.assign(S, { ssid: j.ssid, net: 'connecting' });
      setTimeout(() => Object.assign(S, { net: 'online', ip: '192.168.0.57', rssi: -50 }), 3000);
      return json(res, 200, { ok: true });
    case '/api/ota/check':
      Object.assign(S, { ota: 'available', ota_new_version: '0.2.0', ota_msg: 'Versión 0.2.0 disponible' });
      return json(res, 200, { ok: true });
    case '/api/ota/install': {
      Object.assign(S, { ota: 'downloading', ota_progress: 0, ota_msg: 'Descargando...' });
      const t = setInterval(() => { S.ota_progress += 10; if (S.ota_progress >= 100) { clearInterval(t); Object.assign(S, { ota: 'up_to_date', version: '0.2.0', ota_msg: 'Tenés la última versión' }); } }, 400);
      return json(res, 200, { ok: true });
    }
    case '/api/relay': S.relays[j.ch - 1] = j.on; return json(res, 200, { ok: true });
    case '/api/relays/off': S.relays.fill(false); return json(res, 200, { ok: true });
    case '/api/relays/test': S.relays.forEach((_, i) => { setTimeout(() => (S.relays[i] = true), i * 650); setTimeout(() => (S.relays[i] = false), i * 650 + 500); }); return json(res, 200, { ok: true });
    case '/api/board': S.board = j.board; return json(res, 200, { ok: true });
    case '/api/settings': if ('auto_update' in j) S.auto_update = j.auto_update; return json(res, 200, { ok: true });
    case '/api/ota/upload': return json(res, 200, { ok: true, msg: 'Instalado. Reiniciando...' });
    default: return json(res, 200, { ok: true });
  }
}).listen(port, () => console.log(`Panel simulado en http://localhost:${port}/?ap=1`));
