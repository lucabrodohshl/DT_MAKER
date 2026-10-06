#!/usr/bin/env node
/**
 * Captures docs/screenshots/* from the RUNNING integrated product (no mocks).
 *
 * The drone mission is driven deterministically through the real runtime API
 * (POST /simulation/step advances exactly 0.1 s of logical time), so each picture
 * shows the same logical moment on every run: t = 10.0 s (navigating), 15.6 s
 * (closed fire door discovered, route invalidated, REPLANNING), 16.8 s (candidates
 * evaluated, decision pending), then the completed mission (ledger, replay).
 *
 * Usage: STUDIO_URL=http://127.0.0.1:8080 node scripts/screenshots/capture.mjs
 * (scripts/capture-screenshots.sh starts an isolated stack and runs this.)
 */
import { createRequire } from 'node:module';
import { mkdirSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

const require = createRequire(new URL('../../web/studio/package.json', import.meta.url));
const { chromium } = require('playwright');

const STUDIO = process.env.STUDIO_URL ?? 'http://127.0.0.1:8080';
const OUT = fileURLToPath(new URL('../../docs/screenshots/', import.meta.url));
const DRONE = 'indoor-drone-dt';
const VIEW = '/assets/drone-01/view/drone';
mkdirSync(OUT, { recursive: true });

async function api(method, path, body) {
  const r = await fetch(`${STUDIO}/api/v1${path}`, {
    method,
    headers: { 'content-type': 'application/json' },
    body: method === 'GET' ? undefined : JSON.stringify(body ?? {}),
  });
  const text = await r.text();
  if (!r.ok) throw new Error(`${method} ${path} -> ${r.status} ${text.slice(0, 300)}`);
  return text ? JSON.parse(text) : {};
}
const rt = (method, path, body) => api(method, `/twins/${DRONE}${path}`, body);

async function stepTo(seconds) {
  let st = await rt('GET', '/simulation/state');
  while (st.now.ticks < Math.round(seconds * 1000) && st.status !== 'finished' && st.status !== 'failed') {
    st = await rt('POST', '/simulation/step');
  }
  return st;
}

async function settle(page, ms = 1500) {
  await page.waitForLoadState('networkidle').catch(() => undefined);
  await page.waitForTimeout(ms);
}

async function shot(page, name, path, { waitText, action, fullPage = false, clip } = {}) {
  await page.goto(`${STUDIO}${path}`, { waitUntil: 'domcontentloaded' });
  if (waitText) await page.getByText(waitText).first().waitFor({ timeout: 30_000 });
  await settle(page);
  if (action) {
    await action(page);
    await settle(page, 1200);
  }
  await page.screenshot({ path: `${OUT}${name}`, fullPage, clip });
  console.log(`captured ${name}`);
}

const browser = await chromium.launch(process.env.PLAYWRIGHT_CHROMIUM ? { executablePath: process.env.PLAYWRIGHT_CHROMIUM } : {});
const context = await browser.newContext({
  viewport: { width: 1600, height: 1000 },
  deviceScaleFactor: 1,
  colorScheme: 'light',
  locale: 'en-US', // consistent number and date formatting, whatever the host's locale
  timezoneId: 'UTC',
});
const page = await context.newPage();

try {
  // ---- Knowledge graph
  await shot(page, '02-asset-knowledge-graph.png', '/graph', { waitText: /Drone-01|Building A/ });

  // ---- The drone mission, deterministically stepped through the real runtime
  await rt('POST', '/simulation/reset');
  await rt('POST', '/mission/start');
  await stepTo(10.0);
  await shot(page, '03-drone-live-navigation.png', VIEW, { waitText: /Digital twin knowledge/ });
  await stepTo(15.6);
  await shot(page, '04-drone-obstacle-discovered.png', VIEW, { waitText: /Digital twin knowledge/ });
  await stepTo(16.8);
  await shot(page, '05-drone-replanning.png', VIEW, { waitText: /Planner candidates/, fullPage: true });
  // Hero: the most interesting moment at a presentation size.
  await page.setViewportSize({ width: 1920, height: 1200 });
  await shot(page, 'verified-twin-studio-drone-hero.png', VIEW, { waitText: /Planner candidates/ });
  await page.setViewportSize({ width: 1600, height: 1000 });
  await shot(page, '06-behavioral-graph.png', '/assets/drone-01/behavior', {
    waitText: /Replanning|REPLANNING/,
    action: async (p) => {
      const tab = p.getByRole('tab', { name: /behavioural graph/i }).first();
      if (await tab.count()) await tab.click();
      else await p.getByText(/Behavioural graph/).first().click();
    },
  });
  await shot(page, '07-prediction-plans.png', '/assets/drone-01/predict', {
    waitText: /Predict|Planning/,
    action: async (p) => {
      const tab = p.getByRole('tab', { name: /planning/i }).first();
      if (await tab.count()) await tab.click();
    },
    fullPage: true,
  });

  // ---- Complete the mission, then audit it
  const done = await stepTo(400);
  const session = done.session;
  await shot(page, '08-ledger.png', `/audit/executions/${DRONE}/${encodeURIComponent(session)}`, {
    waitText: /ledger|Ledger/,
    action: async (p) => {
      const verify = p.getByRole('button', { name: /verify/i }).first();
      if (await verify.count()) await verify.click();
    },
  });
  // Replay positioned on the first route invalidation (found in the real ledger).
  const steps = await rt('GET', `/runtime/ledger?session=${encodeURIComponent(session)}&kind=step&limit=500`);
  const invalidation = steps.records.find((r) => r.body?.input?.name === 'path_invalidated!');
  await shot(page, '09-replay.png', `/audit/executions/${DRONE}/${encodeURIComponent(session)}/replay`, {
    waitText: /Replay identical|Replay differs/,
    action: async (p) => {
      if (invalidation) await p.locator('select').first().selectOption(String(invalidation.seq)).catch(() => undefined);
    },
    fullPage: true,
  });
  await shot(page, '01-overview.png', '/', { waitText: /Asset estate/i });

  // ---- Engineering and maintenance
  await shot(page, '10-engineering-assurance.png', '/engineering', { waitText: /Assurance|Verification/ });
  await shot(page, '11-ontology-editor.png', '/engineering/ontologies/indoor-drone-domain/versions/1', { waitText: /indoor-drone-domain|Indoor drone domain/ });
  const evidence = await api('GET', '/evidence').catch(() => ({ items: [] }));
  const items = Array.isArray(evidence) ? evidence : evidence.items ?? evidence.evidence ?? [];
  const refinement = items.find((e) => (e.kind ?? '').toLowerCase().includes('refinement'));
  if (refinement) {
    await shot(page, '12-ontology-refinement.png', `/maintenance/refinement/${encodeURIComponent(refinement.id)}`, { waitText: /refinement|Refinement/, fullPage: true });
  } else {
    console.warn('no refinement evidence found; 12-ontology-refinement.png not captured');
  }
  await shot(page, '13-pump-example.png', '/assets/pump-p101', { waitText: /P-101/ });
} finally {
  await browser.close();
}
