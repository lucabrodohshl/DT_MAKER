#!/usr/bin/env node
/**
 * Captures docs/screenshots/first-twin/* for docs/studio/tutorial-first-twin.md ("Build Your
 * First Twin": the Simple Thermal Chamber) from the RUNNING product (no mocks). The twin is built
 * the way the tutorial describes: the New Blueprint wizard, the editors' import controls for the
 * timed automata, the ontology and the interpretations, Run all checks, package, publish, an
 * instance, its deployment and Operate. Section contents (assets, world, data contract, monitors,
 * scenarios) are those of examples/thermal-chamber/blueprint.json, saved through the same section
 * endpoint the editors call — the tutorial shows them being entered in each editor.
 *
 * It MUTATES the stack it runs against (creates, publishes and deploys a Blueprint), so run it on
 * an isolated stack: scripts/capture-screenshots.sh does that.
 *
 * Usage: STUDIO_URL=http://127.0.0.1:18080 node scripts/screenshots/first-twin.mjs
 */
import { createRequire } from 'node:module';
import { mkdirSync, readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

const require = createRequire(new URL('../../web/studio/package.json', import.meta.url));
const { chromium } = require('playwright');

const STUDIO = process.env.STUDIO_URL ?? 'http://127.0.0.1:8080';
const OUT = fileURLToPath(new URL('../../docs/screenshots/first-twin/', import.meta.url));
const EX = fileURLToPath(new URL('../../examples/thermal-chamber/', import.meta.url));
const ID = 'thermal-chamber';
const NAME = 'Simple Thermal Chamber';
const BASE = `/studio/blueprints/${ID}/v/1`;
const INSTANCE = 'chamber-tc1-dt';
mkdirSync(OUT, { recursive: true });

async function api(method, path, body) {
  const r = await fetch(`${STUDIO}/api/v1${path}`, {
    method,
    headers: { 'content-type': 'application/json', 'x-twin-actor': 'tutorial' },
    body: method === 'GET' ? undefined : JSON.stringify(body ?? {}),
  });
  const text = await r.text();
  if (!r.ok) throw new Error(`${method} ${path} -> ${r.status} ${text.slice(0, 300)}`);
  return text ? JSON.parse(text) : {};
}

const doc = JSON.parse(readFileSync(`${EX}blueprint.json`, 'utf8'));
doc.identity = { ...doc.identity, name: NAME, modelId: ID };

async function saveSections(...sections) {
  for (const s of sections) {
    const v = await api('GET', `/blueprints/${ID}/versions/1`);
    await api('PUT', `/blueprints/${ID}/versions/1/sections/${s}`, { revision: v.revision, content: doc[s] });
  }
}

async function settle(page, ms = 1200) {
  await page.waitForLoadState('networkidle').catch(() => undefined);
  await page.waitForTimeout(ms);
}

async function snap(page, name) {
  await settle(page);
  await page.screenshot({ path: `${OUT}${name}` });
  console.log(`captured first-twin/${name}`);
}

async function open(page, path, waitText) {
  await page.goto(`${STUDIO}${path}`, { waitUntil: 'domcontentloaded' });
  if (waitText) await page.getByText(waitText).first().waitFor({ timeout: 60_000 });
  await settle(page);
}

async function importModel(page, role, file) {
  await open(page, `${BASE}/behavior/${role}`);
  await page.getByRole('button', { name: /^Import/ }).first().click();
  await page.getByLabel('Model file').setInputFiles(`${EX}models/${file}`);
  await page.getByRole('button', { name: 'Import into this draft' }).click();
  await page.getByText(/^Imported \(/).waitFor({ timeout: 60_000 });
}

async function importSemantics(page, role, path, file) {
  await open(page, path);
  await page.locator('label', { hasText: 'Import file' }).locator('input[type=file]').setInputFiles(`${EX}models/${file}`);
  const text = readFileSync(`${EX}models/${file}`, 'utf8');
  for (let i = 0; i < 60; i++) {
    const v = await api('GET', `/blueprints/${ID}/versions/1/semantics/${role}`);
    if (v.artifact?.content === text) return;
    await page.waitForTimeout(500);
  }
  throw new Error(`${role} was not saved`);
}

const browser = await chromium.launch(process.env.PLAYWRIGHT_CHROMIUM ? { executablePath: process.env.PLAYWRIGHT_CHROMIUM } : {});
const context = await browser.newContext({ viewport: { width: 1600, height: 1000 }, deviceScaleFactor: 1, colorScheme: 'light', locale: 'en-US', timezoneId: 'UTC' });
const page = await context.newPage();

try {
  // 1-2. New Blueprint: starting point and identity.
  await open(page, '/studio', 'Twin Blueprints');
  await page.getByRole('button', { name: 'New Blueprint' }).first().click();
  await page.getByRole('radio', { name: /Blank Blueprint/ }).click();
  await snap(page, '01-start.png');
  await page.getByRole('button', { name: 'Next: identity' }).click();
  await page.getByLabel('Name').fill(NAME);
  await page.getByRole('textbox', { name: /^Id\b/ }).fill(ID);
  await page.getByLabel('Domain').fill('lab');
  await page.getByLabel('Description').fill('A laboratory heating chamber: heater, door sensor and air-temperature sensor, with an over-temperature latch.');
  await page.getByLabel('Icon').selectOption('thermometer');
  await snap(page, '02-identity.png');
  await page.getByRole('button', { name: 'Create and continue (guided)' }).click();
  await page.getByText('Step 3 of 11: Structure').waitFor({ timeout: 60_000 });

  // 3. Structure: asset types and the assets every chamber instance gets.
  await saveSections('identity', 'structure');
  await open(page, `${BASE}/build/structure?tab=assets&asset=chamber`, 'Hierarchy');
  await snap(page, '03-structure.png');

  // 4. World & layout: the lab floor with the chamber, heater, door and probe.
  await saveSections('world');
  await open(page, `${BASE}/build/world?object=chamber-body`, 'World & Layout');
  await snap(page, '04-world.png');

  // 5. Data & connectivity: signals, events, commands and the simulator source.
  await saveSections('data', 'connectivity');
  await open(page, `${BASE}/build/data?telemetry=temperature`, 'Data & Connectivity');
  await snap(page, '05-data.png');

  // 6-7. Behaviour: the controller's UPPAAL model (PT view) and the twin's view (DT view).
  await importModel(page, 'pt', 'chamber_pt.xml');
  await snap(page, '06-pt-import.png');
  await page.getByRole('button', { name: 'Done' }).click();
  await importModel(page, 'dt', 'chamber_dt.tta.json');
  await page.getByRole('button', { name: 'Done' }).click();
  await open(page, `${BASE}/behavior/dt?state=OVERHEATED`, 'Digital Twin View');
  await snap(page, '07-dt-view.png');

  // 8-10. Semantics: ontology, interpretations, and the cross-layer view.
  await importSemantics(page, 'ontology', `${BASE}/semantics/ontology`, 'thermal-chamber.ont');
  await open(page, `${BASE}/semantics/ontology`, 'Ontology');
  await snap(page, '08-ontology.png');
  await importSemantics(page, 'pt_interpretation', `${BASE}/semantics/interpretations?view=pt`, 'pt.interp');
  await importSemantics(page, 'dt_interpretation', `${BASE}/semantics/interpretations?view=dt`, 'dt.interp');
  await open(page, `${BASE}/semantics/interpretations?view=dt`, 'Interpretations');
  await snap(page, '09-interpretations.png');
  await saveSections('presentation', 'assurance', 'simulation', 'scenarios');
  await open(page, `${BASE}/semantics/binding`, 'Cross-layer binding');
  await snap(page, '10-cross-layer.png');

  // 11-12. Assurance: requirements and monitors.
  await open(page, `${BASE}/assurance/requirements?id=REQ-S1`, 'Requirements');
  await snap(page, '11-requirements.png');
  await open(page, `${BASE}/assurance/monitors?id=never-overheated`, 'Monitors');
  await snap(page, '12-monitors.png');

  // 13. Verification: run every check (validators, compiler, aligner, scenario tests).
  await open(page, `${BASE}/assurance/verification`, 'Verification');
  await page.getByRole('button', { name: 'Run all checks' }).click();
  await page.getByText('VERIFIED — every formal check passes for these inputs').waitFor({ timeout: 300_000 });
  await snap(page, '13-verification.png');
  await open(page, `${BASE}/assurance/alignment`, 'Semantic alignment');
  await snap(page, '14-alignment.png');

  // 15. A scenario: resetting the latch too early is refused by the kernel.
  await open(page, `${BASE}/test/scenarios/reset-too-early`, 'Scenario Builder');
  await page.getByRole('button', { name: 'Run scenario' }).click();
  await page.getByText(/^PASS — /).waitFor({ timeout: 60_000 });
  await snap(page, '15-scenario.png');

  // 16. Release: package and bundle, the gate turns green.
  await open(page, `${BASE}/release/package`, 'Package & release');
  await page.getByRole('button', { name: 'Build package & bundle' }).click();
  await page.getByText('READY TO RELEASE', { exact: true }).first().waitFor({ timeout: 240_000 });
  await snap(page, '16-release.png');
  await page.getByRole('button', { name: 'Publish v1' }).click();
  await page.getByRole('button', { name: 'Publish', exact: true }).click();
  await page.waitForURL(/release\/instances/, { timeout: 60_000 });

  // 17-18. An instance for chamber TC-1, deployed.
  await page.getByRole('button', { name: 'New instance' }).click();
  await page.getByLabel('Instance id').fill(INSTANCE);
  await page.getByLabel('Display name').fill('Chamber TC-1');
  await page.getByLabel('Concrete id of chamber').fill('chamber-tc1');
  await page.getByLabel('Concrete id of lab').fill('lab-m2');
  await snap(page, '17-instance.png');
  await page.getByRole('button', { name: 'Create instance' }).click();
  await page.getByRole('button', { name: 'Deploy now' }).click();
  await page.waitForURL(/release\/deployment/, { timeout: 60_000 });
  await page.getByText('DEPLOYED · RUNNING').first().waitFor({ timeout: 120_000 });
  await snap(page, '18-deployment.png');

  // 19-20. Operate: the running twin and its live monitors.
  await page.waitForTimeout(6000);
  await open(page, `/twins/${INSTANCE}`, 'Chamber TC-1');
  await snap(page, '19-operate.png');
  await open(page, `/twins/${INSTANCE}/behavior/monitors`, 'Behavioural conformance');
  await snap(page, '20-operate-monitors.png');
} finally {
  await browser.close();
}
