// Copia runtime, catálogo y ejemplos a druida-web-deploy/public/developers/
// para que la página /developers los sirva y los cargue en la placa.
//   node tools/sync-web.mjs [ruta/a/druida-web-deploy]
import { readdirSync, readFileSync, writeFileSync, mkdirSync, rmSync, copyFileSync } from 'node:fs'
import { join, dirname } from 'node:path'
import { fileURLToPath } from 'node:url'

const root = join(dirname(fileURLToPath(import.meta.url)), '..')
const web = process.argv[2] || join(root, '..', 'druida-web-deploy')
const out = join(web, 'public', 'developers')

rmSync(out, { recursive: true, force: true })
mkdirSync(join(out, 'runtime', 'dd'), { recursive: true })
mkdirSync(join(out, 'examples'), { recursive: true })

const runtime = readdirSync(join(root, 'runtime', 'dd')).filter(f => f.endsWith('.py')).sort()
for (const f of runtime) copyFileSync(join(root, 'runtime', 'dd', f), join(out, 'runtime', 'dd', f))

const examples = readdirSync(join(root, 'examples')).filter(f => f.endsWith('.py')).sort().map(f => {
  copyFileSync(join(root, 'examples', f), join(out, 'examples', f))
  const src = readFileSync(join(root, 'examples', f), 'utf8')
  const title = (src.match(/^# (.+)$/m) || [, f])[1]
  return { file: f, title }
})

const version = (readFileSync(join(root, 'runtime', 'dd', '__init__.py'), 'utf8').match(/VERSION = "([^"]+)"/) || [])[1]
copyFileSync(join(root, 'catalog', 'catalog.json'), join(out, 'catalog.json'))
writeFileSync(join(out, 'index.json'), JSON.stringify({ runtime_version: version, runtime, examples }, null, 2) + '\n')
console.log(`ok: dd ${version}, ${runtime.length} archivos de runtime, ${examples.length} ejemplos -> ${out}`)
