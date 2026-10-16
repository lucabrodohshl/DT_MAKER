import { cp, mkdir, rm, stat } from 'node:fs/promises'
import { fileURLToPath } from 'node:url'
import { join } from 'node:path'

const source = fileURLToPath(new URL('../../../docs/.vitepress/dist/', import.meta.url))
const target = fileURLToPath(new URL('../dist/docs/', import.meta.url))

if (!(await stat(join(source, 'index.html')).catch(() => null))) {
  throw new Error('Build the standalone docs before bundling them into Studio.')
}

await rm(target, { recursive: true, force: true })
await mkdir(target, { recursive: true })
await cp(source, target, { recursive: true })
console.log(`Bundled standalone docs into ${target}`)
