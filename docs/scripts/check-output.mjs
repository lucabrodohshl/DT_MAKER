import { existsSync, readFileSync, readdirSync } from 'node:fs'
import { join, resolve } from 'node:path'

const output = resolve('.vitepress/dist')
const base = process.env.DOCS_BASE || '/docs/'
const required = [
  'index.html', 'installation.html', 'getting-started.html',
  'tutorial-first-twin.html', 'tutorial-connect-real-twin.html',
  'tutorial-evolving-an-ontology.html', 'architecture.html',
  'runtime-api.html', 'troubleshooting.html', 'hosting.html',
  'robots.txt'
]
const files = readdirSync(output, { recursive: true })
const errors = []

for (const file of required) {
  if (!existsSync(join(output, file))) errors.push(`Missing ${file}`)
}
if (!files.some(file => /\/\@localSearchIndex[^/]*\.js$/.test(file))) {
  errors.push('Missing local search index')
}
if (!files.some(file => /doppelganger-drone-hero.*\.png$/.test(file))) {
  errors.push('Missing bundled product screenshot')
}

for (const file of files.filter(file => file.endsWith('.html'))) {
  const html = readFileSync(join(output, file), 'utf8')
  for (const match of html.matchAll(/(?:src|href)="([^"#?]+)(?:[?#][^"]*)?"/g)) {
    let url = match[1].replaceAll('&amp;', '&')
    if (/^(https?:|mailto:|data:|javascript:)/.test(url)) continue
    if (url.startsWith(base)) url = url.slice(base.length)
    else if (url.startsWith('/')) continue
    else url = join(file === 'index.html' ? '' : file.replace(/[^/]+$/, ''), url)
    if (!url || url === '.') url = 'index.html'
    if (url.endsWith('/')) url += 'index.html'
    if (!existsSync(join(output, url))) errors.push(`${file}: missing asset or page ${match[1]}`)
  }
}

if (errors.length) {
  console.error(errors.join('\n'))
  process.exitCode = 1
} else {
  console.log(`Checked ${files.filter(file => file.endsWith('.html')).length} static pages, local links, search index and screenshots at base ${base}`)
}
