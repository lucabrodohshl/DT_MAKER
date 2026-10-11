#!/usr/bin/env node
/**
 * Screenshots of the "Connect a real twin" tutorial (docs/studio/tutorial-connect-real-twin.md).
 *
 * Expects a running Studio (STUDIO_URL, default http://127.0.0.1:8080) on which the
 * centrifugal-pump Blueprint is published and the instance pump-p102-dt is deployed with
 * only its real (non-simulator) source and has been fed by examples/real-twin/twin_bridge.py
 * from examples/real-twin/plc-p102.jsonl. The script captures the healthy twin, then sends
 * one event the verified model does not allow and captures how the deviation is shown.
 *
 *   STUDIO_URL=http://127.0.0.1:18480 PLAYWRIGHT_CHROMIUM=<chromium> node scripts/screenshots/real-twin.mjs
 */
import { createRequire } from 'node:module';
import { mkdirSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

const require = createRequire(new URL('../../web/studio/package.json', import.meta.url));
const { chromium } = require('playwright');

const STUDIO = process.env.STUDIO_URL ?? 'http://127.0.0.1:8080';
const OUT = fileURLToPath(new URL('../../docs/screenshots/real-twin/', import.meta.url));
const INSTANCE = 'pump-p102-dt';
mkdirSync(OUT, { recursive: true });

async function settle(page, ms = 1500) {
  await page.waitForLoadState('networkidle').catch(() => undefined);
  await page.waitForTimeout(ms);
}

async function shot(page, path, name, waitText) {
  await page.goto(`${STUDIO}${path}`, { waitUntil: 'domcontentloaded' });
  if (waitText) await page.getByText(waitText).first().waitFor({ timeout: 60_000 }).catch(() => undefined);
  await settle(page);
  await page.screenshot({ path: `${OUT}${name}` });
  console.log(`captured real-twin/${name}`);
}

const browser = await chromium.launch(process.env.PLAYWRIGHT_CHROMIUM ? { executablePath: process.env.PLAYWRIGHT_CHROMIUM } : {});
const context = await browser.newContext({ viewport: { width: 1600, height: 1000 }, deviceScaleFactor: 1, colorScheme: 'light', locale: 'en-US', timezoneId: 'UTC' });
const page = await context.newPage();
try {
  await shot(page, '/studio/blueprints/centrifugal-pump/v/1/build/data', '01-blueprint-data-sources.png', 'MQTT');
  await shot(page, `/twins/${INSTANCE}`, '02-instance-overview.png', 'Pump P-102');
  await shot(page, `/twins/${INSTANCE}/operations/telemetry`, '03-live-telemetry.png');
  await shot(page, `/twins/${INSTANCE}/behavior/monitors`, '04-monitors.png');
  await shot(page, `/twins/${INSTANCE}/audit/ledger`, '05-ledger.png');

  // A deviation: the PLC reports cooling as done while the pump is stopped (not allowed by the verified model).
  const r = await fetch(`${STUDIO}/api/v1/twins/${INSTANCE}/runtime/pt-event`, {
    method: 'POST',
    headers: { 'content-type': 'application/json', 'x-twin-actor': 'twin-bridge' },
    body: JSON.stringify({ label: 'aux_cooling_done!', time: '360' }),
  });
  const verdict = await r.json();
  if (verdict.accepted !== false) throw new Error(`expected a rejection, got ${JSON.stringify(verdict)}`);
  await shot(page, `/twins/${INSTANCE}/behavior/conformance`, '06-conformance-deviation.png');
  await shot(page, `/twins/${INSTANCE}/behavior/monitors`, '07-monitors-after-deviation.png');
} finally {
  await browser.close();
}
