#!/usr/bin/env node
/** Capture the current Studio UI from an isolated, seeded demo stack. */
import { createRequire } from 'node:module'
import { mkdirSync, readFileSync } from 'node:fs'
import { fileURLToPath } from 'node:url'

const require = createRequire(new URL('../../web/studio/package.json', import.meta.url))
const { chromium } = require('playwright')
const origin = process.env.STUDIO_URL || 'http://127.0.0.1:18080'
const out = fileURLToPath(new URL('../../docs/screenshots/current/', import.meta.url))
const hero = fileURLToPath(new URL('../../docs/screenshots/doppelganger-drone-hero.png', import.meta.url))
const example = fileURLToPath(new URL('../../examples/thermal-chamber/model.json', import.meta.url))
mkdirSync(out, { recursive: true })

async function api(method, path, body) {
  const response = await fetch(`${origin}/api/v1${path}`, {
    method,
    headers: { 'content-type': 'application/json', 'x-twin-actor': 'docs-capture' },
    body: method === 'GET' ? undefined : JSON.stringify(body ?? {})
  })
  const value = await response.json()
  if (!response.ok) throw new Error(`${method} ${path}: ${response.status} ${JSON.stringify(value).slice(0, 300)}`)
  return value
}

const browser = await chromium.launch(process.env.PLAYWRIGHT_CHROMIUM
  ? { executablePath: process.env.PLAYWRIGHT_CHROMIUM }
  : {})
const context = await browser.newContext({
  viewport: { width: 1600, height: 900 },
  deviceScaleFactor: 1,
  colorScheme: 'light',
  locale: 'en-US',
  timezoneId: 'UTC'
})
const page = await context.newPage()

async function open(path, heading) {
  await page.goto(`${origin}${path}`, { waitUntil: 'domcontentloaded' })
  await page.getByRole('heading', { name: heading }).first().waitFor({ timeout: 30_000 })
}
async function capture(name) {
  await page.screenshot({ path: `${out}${name}` })
  console.log(`captured ${name}`)
}

try {
  await open('/twins', 'Your twins')
  await capture('running-twins.png')

  await open('/studio', 'Twin designs')
  await capture('twin-designs.png')

  await open('/studio/new', 'New twin design')
  await capture('new-design.png')
  await page.getByRole('button', { name: 'Next: identity' }).click()
  await page.getByRole('textbox', { name: 'Name', exact: true }).fill('Thermal Chamber')
  await page.getByRole('textbox', { name: /Id Lower-case/ }).fill('thermal-chamber-docs')
  await capture('design-identity.png')
  await page.getByRole('button', { name: 'Create twin design' }).click()
  await page.waitForURL(/\/studio\/blueprints\/thermal-chamber-docs\/v\/1\/model/)

  const path = '/blueprints/thermal-chamber-docs/versions/1/model'
  const current = await api('GET', path)
  const model = JSON.parse(readFileSync(example, 'utf8'))
  await api('PUT', path, { revision: current.revision, model })
  await page.reload({ waitUntil: 'domcontentloaded' })
  await page.getByRole('tab', { name: /Structure/ }).waitFor()
  await capture('thermal-structure.png')
  await page.getByRole('tab', { name: /Behavior/ }).click()
  await capture('thermal-behavior.png')
  await page.getByRole('tab', { name: /Generated/ }).click()
  await page.getByRole('tab', { name: 'Automaton' }).waitFor()
  await capture('thermal-generated.png')
  await page.getByRole('tab', { name: 'Formal ontology' }).click()
  await capture('thermal-ontology.png')

  await open('/studio/ontologies', 'Ontologies')
  await capture('ontology-library.png')

  const twin = 'indoor-drone-dt'
  const runtime = (method, suffix, body) => api(method, `/twins/${twin}${suffix}`, body)
  await runtime('POST', '/simulation/reset')
  await runtime('POST', '/mission/start')
  let state = await runtime('GET', '/simulation/state')
  while (state.now.ticks < 16800 && state.status !== 'finished' && state.status !== 'failed') {
    state = await runtime('POST', '/simulation/step')
  }
  await page.setViewportSize({ width: 1920, height: 1100 })
  await page.goto(`${origin}/twins/${twin}/assets/drone-01/view/drone`, { waitUntil: 'domcontentloaded' })
  await page.getByText('Physical world').first().waitFor({ timeout: 30_000 })
  await page.screenshot({ path: hero })
  console.log('captured doppelganger-drone-hero.png')
} finally {
  await browser.close()
}
