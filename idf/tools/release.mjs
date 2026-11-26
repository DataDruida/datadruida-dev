// Publica una versión para OTA: copia el .bin compilado y escribe el
// manifest.json que leen los equipos.
//
//   node idf/tools/release.mjs dd-reles [--notes "texto"] [--out <carpeta>]
//
// Por defecto escribe en ../druida-web-deploy/public/firmware/dd-idf/<app>/,
// que se sirve en https://app.datadruida.com.ar/firmware/dd-idf/<app>/.
// No hace commit ni push: eso queda a mano, porque publicar = todos los
// equipos con auto-actualización la instalan.
import { readFileSync, writeFileSync, copyFileSync, mkdirSync, existsSync } from 'node:fs';
import { createHash } from 'node:crypto';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const args = process.argv.slice(2);
const appDir = args[0];
if (!appDir) { console.error('Uso: node idf/tools/release.mjs <app> [--notes "..."] [--out dir]'); process.exit(1); }
const opt = (k) => { const i = args.indexOf(k); return i > 0 ? args[i + 1] : undefined; };

const build = path.join(here, '..', 'apps', appDir, 'build');
const desc = JSON.parse(readFileSync(path.join(build, 'project_description.json'), 'utf8'));
const name = desc.project_name;            // ej. dd-pantalla-43b: es el app_name de dd_core
const version = desc.project_version;
const bin = path.join(build, `${name}.bin`);
if (!existsSync(bin)) { console.error(`No está ${bin}: compilá primero (idf/build.ps1 ${appDir})`); process.exit(1); }

const out = opt('--out') || path.join(here, '..', '..', '..', 'druida-web-deploy', 'public', 'firmware', 'dd-idf', name);
mkdirSync(out, { recursive: true });
const file = `${name}-${version}.bin`;
copyFileSync(bin, path.join(out, file));
const buf = readFileSync(bin);
const manifest = {
  version,
  url: file,
  size: buf.length,
  sha256: createHash('sha256').update(buf).digest('hex'),
  notes: opt('--notes') || '',
  published: new Date().toISOString(),
};
writeFileSync(path.join(out, 'manifest.json'), JSON.stringify(manifest, null, 2) + '\n');
console.log(`${name} ${version} → ${out}`);
console.log(JSON.stringify(manifest, null, 2));
