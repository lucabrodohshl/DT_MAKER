#!/usr/bin/env node
/**
 * Captures docs/screenshots/studio/* — the Blueprint Studio tour — from the RUNNING product (no
 * mocks): the seeded indoor drone and centrifugal pump Blueprints, their running instances, and a
 * draft v2 of the drone (so the editors are shown editable). Every state in the pictures is the
 * backend's: validation, the aligner's verdict, the kernel's timing windows and refusals, the
 * release gate, the supervisor's processes.
 *
 * It MUTATES the stack it runs against (creates the drone draft, runs checks, builds its package,
 * starts and stops a preview), so run it on an isolated stack: scripts/capture-screenshots.sh.
 *
 * Usage: STUDIO_URL=http://127.0.0.1:18080 node scripts/screenshots/studio.mjs
 */
import { createRequire } from 'node:module';
import { mkdirSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

const require = createRequire(new URL('../../web/studio/package.json', import.meta.url));
const { chromium } = require('playwright');

const STUDIO = process.env.STUDIO_URL ?? 'http://127.0.0.1:8080';
const OUT = fileURLToPath(new URL('../../docs/screenshots/studio/', import.meta.url));
const DRONE = 'indoor-inspection-drone';
const PUMP = 'centrifugal-pump';
mkdirSync(OUT, { recursive: true });

async function api(method, path, body) {
  const r = await fetch(`${STUDIO}/api/v1${path}`, {
    method,
    headers: { 'content-type': 'application/json', 'x-twin-actor': 'studio-tour' },
    body: method === 'GET' || method === 'DELETE' ? undefined : JSON.stringify(body ?? {}),
  });
  const text = await r.text();
  if (!r.ok) throw new Error(`${method} ${path} -> ${r.status} ${text.slice(0, 300)}`);
  return text ? JSON.parse(text) : {};
}

async function settle(page, ms = 1200) {
  await page.waitForLoadState('networkidle').catch(() => undefined);
  await page.waitForTimeout(ms);
}

async function open(page, path, waitText) {
  await page.goto(`${STUDIO}${path}`, { waitUntil: 'domcontentloaded' });
  if (waitText) await page.getByText(waitText).first().waitFor({ timeout: 60_000 });
  await settle(page);
}

async function snap(page, name, opts = {}) {
  await settle(page, opts.wait ?? 800);
  await page.screenshot({ path: `${OUT}${name}`, fullPage: !!opts.fullPage });
  console.log(`captured studio/${name}`);
}

// A draft of the drone: the editors are shown on an editable version.
const drone = await api('GET', `/blueprints/${DRONE}`);
let v2 = drone.versions.find((v) => v.state === 'draft');
if (!v2) v2 = await api('POST', `/blueprints/${DRONE}/versions/1/drafts`, { note: 'Studio tour' });
const D = `/studio/blueprints/${DRONE}/v/${v2.version}`;
const D1 = `/studio/blueprints/${DRONE}/v/1`;
const P1 = `/studio/blueprints/${PUMP}/v/1`;
// Evidence for exactly the draft's inputs: the aligner (with its duration), the compiler, the tests.
for (const check of ['formal', 'compile', 'alignment', 'scenarios']) await api('POST', `/blueprints/${DRONE}/versions/${v2.version}/checks/${check}`);

const browser = await chromium.launch(process.env.PLAYWRIGHT_CHROMIUM ? { executablePath: process.env.PLAYWRIGHT_CHROMIUM } : {});
const context = await browser.newContext({ viewport: { width: 1600, height: 1000 }, deviceScaleFactor: 1, colorScheme: 'light', locale: 'en-US', timezoneId: 'UTC' });
const page = await context.newPage();

try {
  await open(page, '/studio', 'Twin Blueprints');
  await snap(page, 'studio-blueprints.png');

  await open(page, D, 'Release gate');
  await snap(page, 'studio-overview.png');

  await open(page, `${D}/build/structure?tab=assets&asset=drone`, 'Hierarchy');
  await snap(page, 'studio-structure.png');

  await open(page, `${D}/build/world`, 'World & Layout');
  await snap(page, 'studio-world-layout.png');
  await page.getByRole('tab', { name: 'Ground truth vs knowledge (simulator)' }).click();
  await settle(page, 2000);
  await snap(page, 'studio-world-ground-truth.png');

  await open(page, `${D}/build/data?telemetry=battery_level`, 'Data & Connectivity');
  await snap(page, 'studio-data-contract.png');
  await open(page, `${D}/build/data?tab=connectivity&source=sim`, 'Data & Connectivity');
  await snap(page, 'studio-data-binding.png');

  await open(page, `${D}/build/presentation`, 'Presentation');
  await snap(page, 'studio-presentation.png');

  await open(page, `${D}/behavior/pt`, 'Physical System View');
  await snap(page, 'studio-pt-ta.png', { wait: 1500 });
  await open(page, `${D}/behavior/dt?state=REPLANNING`, 'Digital Twin View');
  await snap(page, 'studio-dt-ta.png', { wait: 1500 });

  await open(page, `${D}/semantics/ontology`, 'Ontology');
  await snap(page, 'studio-ontology.png');
  await open(page, `${D}/semantics/interpretations?view=dt`, 'Interpretations');
  await snap(page, 'studio-interpretations.png');
  await open(page, `${D}/semantics/binding`, 'Cross-layer binding');
  await snap(page, 'studio-cross-layer-binding.png');

  await open(page, `${D}/assurance/requirements?id=REQ-T1`, 'Requirements');
  await snap(page, 'studio-requirements.png');
  await open(page, `${D}/assurance/monitors?id=replanning-deadline`, 'Monitors');
  await page.getByRole('button', { name: 'Check now (design time)' }).click();
  await page.getByText(/HOLDS|VIOLATED|GUARANTEED|INCONCLUSIVE|DOES NOT HOLD/).first().waitFor({ timeout: 60_000 });
  await snap(page, 'studio-monitors.png');
  await open(page, `${D}/assurance/alignment`, 'Semantic alignment');
  await snap(page, 'studio-alignment.png');

  // Scenario Builder: run the regression scenario, then ask the kernel about the replan timeout.
  await open(page, `${D}/test/scenarios/blocked-east-corridor`, 'Scenario Builder');
  await page.getByRole('button', { name: 'Run scenario' }).click();
  await page.getByText(/^PASS — /).waitFor({ timeout: 60_000 });
  await snap(page, 'studio-scenario-builder.png');
  // A taller window for this one: the timeline with the event's legal window, the explanation and the refusal.
  await page.setViewportSize({ width: 1600, height: 1600 });
  await page.getByRole('button', { name: 'Insert a step after s7' }).click();
  await settle(page, 1500);
  await page.locator('.vts-sc-avail__row', { hasText: 'replan_timeout!' }).click();
  await page.getByRole('button', { name: /Why this window/ }).click();
  await page.getByLabel('Time of the event').fill('19');
  await page.getByRole('button', { name: 'Add event' }).click();
  await page.getByText(/^Not allowed: replan_timeout!/).waitFor({ timeout: 30_000 });
  await settle(page, 1200);
  await snap(page, 'studio-event-timing-window.png');
  await page.getByRole('button', { name: 'Choose another event' }).click();
  await page.setViewportSize({ width: 1600, height: 1000 });

  // Isolated Studio preview of the draft: the real runtime and simulator, no twin record.
  await open(page, `${D}/test/preview`, 'STUDIO PREVIEW');
  await page.getByRole('button', { name: 'Start preview' }).click();
  await page.getByText('RUNNING', { exact: true }).waitFor({ timeout: 120_000 });
  await page.waitForTimeout(14_000);
  await snap(page, 'studio-preview-drone.png');
  await api('DELETE', `/blueprints/${DRONE}/versions/${v2.version}/preview`);

  // Release: the draft's package and bundle, the green gate, the two release artefacts.
  await api('POST', `/blueprints/${DRONE}/versions/${v2.version}/checks/package`);
  await open(page, `${D}/release/package`, 'Release readiness');
  await snap(page, 'studio-release-readiness.png');
  await page.getByRole('heading', { name: 'Verified Core Package' }).scrollIntoViewIfNeeded();
  await page.mouse.wheel(0, 300);
  await snap(page, 'studio-package.png');

  // Instances of the published drone, and the deployment of the pump instance.
  await open(page, `${D1}/release/instances`, 'Instances');
  await page.getByRole('button', { name: 'New instance' }).click();
  await page.getByLabel('Instance id').fill('drone-02-dt');
  await page.getByLabel('Display name').fill('Drone-02');
  await snap(page, 'studio-instance.png');
  await page.keyboard.press('Escape');
  await open(page, `${P1}/release/deployment`, 'DEPLOYED');
  await snap(page, 'studio-deployment.png');
} finally {
  await browser.close();
}
