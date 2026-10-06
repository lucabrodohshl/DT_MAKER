#!/usr/bin/env node
/**
 * Captures docs/screenshots/tutorial/* for docs/studio/tutorial-evolving-an-ontology.md
 * from the RUNNING product (no mocks): the "Safely Evolving an Ontology" journey on the
 * industrial pump, end to end through the real backend (strict parser, aligner, Z3,
 * compiler, package verifier).
 *
 *   01 published ontology       06 refinement result (Def. 4)
 *   02 create draft             07 impact analysis (Theorem 3)
 *   03 edit + diagnostics       08 release pipeline
 *   04 validated (VERIFIED)     09 deployment history
 *   05 compare (semantic diff)  10 an old execution replayed with its historical artefacts
 *
 * The edit is datasheet revision C (examples/industrial-pump/evolution/process-pump-v2.ont).
 * It MUTATES the stack it runs against (creates a change, publishes, deploys), so run it on
 * an isolated stack: scripts/capture-screenshots.sh does that.
 *
 * Usage: STUDIO_URL=http://127.0.0.1:18080 node scripts/screenshots/tutorial.mjs
 */
import { createRequire } from 'node:module';
import { mkdirSync, readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

const require = createRequire(new URL('../../web/studio/package.json', import.meta.url));
const { chromium } = require('playwright');

const STUDIO = process.env.STUDIO_URL ?? 'http://127.0.0.1:8080';
const OUT = fileURLToPath(new URL('../../docs/screenshots/tutorial/', import.meta.url));
const REV_C = readFileSync(new URL('../../examples/industrial-pump/evolution/process-pump-v2.ont', import.meta.url), 'utf8');
const ONT = 'process-pump';
const TWIN = 'pump-p101-dt';
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

async function settle(page, ms = 1200) {
  await page.waitForLoadState('networkidle').catch(() => undefined);
  await page.waitForTimeout(ms);
}

async function shot(page, name, opts = {}) {
  await settle(page);
  await page.screenshot({ path: `${OUT}${name}.png`, fullPage: opts.fullPage ?? false });
  console.log(`  ${name}.png`);
}

const browser = await chromium.launch(process.env.PLAYWRIGHT_CHROMIUM ? { executablePath: process.env.PLAYWRIGHT_CHROMIUM } : {});
const page = await browser.newPage({ viewport: { width: 1440, height: 900 }, deviceScaleFactor: 1, locale: 'en-US', timezoneId: 'UTC' });
page.setDefaultTimeout(60_000);

try {
  // Only one open draft per artefact: earlier runs on this stack may have left one.
  const art = await api('GET', `/artifacts/${ONT}`);
  for (const v of art.versions) {
    if (['draft', 'validating', 'verified'].includes(v.state)) {
      await api('POST', `/artifacts/${ONT}/versions/${v.version}/reject`, { reason: 'tutorial: superseded' });
    }
  }
  const deployed = art.versions.find((v) => v.state === 'published');
  const change = await api('POST', '/changes', {
    twinId: TWIN,
    title: 'Datasheet revision C',
    description: 'Rated speed fixed at 2980 rpm; lube-oil temperature supervision.',
  });

  // 01 — the deployed (published, immutable) ontology.
  await page.goto(`${STUDIO}/engineering/ontologies/${ONT}/versions/${deployed.version}`);
  await page.getByText(/is published and immutable/).waitFor();
  await shot(page, '01-published-ontology');

  // 02 — new draft from it, attached to the change workspace.
  await page.getByRole('button', { name: new RegExp(`create draft from v${deployed.version}`, 'i') }).click();
  const dialog = page.getByRole('dialog');
  await dialog.getByRole('textbox').first().fill('Datasheet revision C: rated speed 2980 rpm, lube-oil supervision');
  await dialog.locator('select').selectOption(change.id);
  await shot(page, '02-create-draft');
  await dialog.getByRole('button', { name: 'Create draft' }).click();
  await page.waitForURL(new RegExp(`/engineering/ontologies/${ONT}/versions/(?!${deployed.version}$)\\d+$`));
  const draft = Number(new URL(page.url()).pathname.split('/').pop());

  // 03 — an edit with a mistake: the strict parser locates it on save.
  const editor = page.locator('.cm-content').first();
  await editor.click();
  await page.keyboard.press('ControlOrMeta+End');
  await page.keyboard.type('\naxiom lube_limit_val : (= lube_oil_limit 80)\n');
  await page.getByRole('button', { name: /save draft/i }).click();
  await page.getByText(/undeclared symbol/).first().waitFor();
  await shot(page, '03-edit-diagnostics');

  // The corrected revision-C text (same edit as the example file), then reload.
  await api('PUT', `/artifacts/${ONT}/versions/${draft}`, { content: REV_C });
  await page.reload();

  // 04 — Validate: aligner parser + Z3 consistency; VERIFIED only on the backend's decision.
  await page.getByRole('button', { name: /^validate/i }).click();
  await page.getByRole('button', { name: /^publish/i }).and(page.locator(':enabled')).waitFor({ timeout: 120_000 });
  await page.getByRole('tab', { name: 'Validation' }).click().catch(() => undefined);
  await shot(page, '04-validated');

  // 05 — Compare with the deployed version: structural diff plus source diff.
  await page.getByRole('link', { name: /^compare/i }).click();
  await shot(page, '05-compare');
  await page.keyboard.press('Escape');

  // 06 — Refinement check (Def. 4) against the deployed ontology and interpretations.
  await page.goto(`${STUDIO}/engineering/ontologies/${ONT}/versions/${draft}`);
  await page.getByRole('button', { name: /check refinement/i }).click();
  await page.getByRole('button', { name: /run refinement check/i }).click();
  await page.waitForURL(/\/maintenance\/refinement\/EV-\d+/, { timeout: 120_000 });
  await page.getByText('Valid refinement').first().waitFor();
  await shot(page, '06-refinement-result', { fullPage: true });

  // 07 — Impact: what depends on the deployed ontology, and what Theorem 3 preserves.
  await page.goto(`${STUDIO}/maintenance/impact?ref=${encodeURIComponent(`${ONT}@${draft}`)}`);
  await page.getByText(/Theorem 3/).first().waitFor();
  await shot(page, '07-impact', { fullPage: true });

  // 08 — Release pipeline: run the remaining stages through the real backend.
  for (const stage of ['alignment', 'compile', 'package', 'verify']) {
    const pipe = await api('GET', `/changes/${change.id}/pipeline`);
    const st = pipe.stages.find((s) => s.id === stage);
    if (st && st.runnable !== false && st.state !== 'pass') await api('POST', `/changes/${change.id}/stages/${stage}/run`);
  }
  await page.goto(`${STUDIO}/maintenance/changes/${change.id}`);
  await page.getByText('Release').first().waitFor();
  await shot(page, '08-pipeline', { fullPage: true });

  // 09 — Release (publishes the versions, marks the package released) and deploy explicitly.
  const released = await api('POST', `/changes/${change.id}/release`);
  await api('POST', '/deployments', { twinId: TWIN, packageId: released.package.id, reason: 'Datasheet revision C' });
  await page.goto(`${STUDIO}/engineering/deployments`);
  await page.getByText(released.package.id).first().waitFor();
  await shot(page, '09-deployments');

  // 10 — The oldest recorded execution replays with ITS historical artefacts (ontology v1).
  await page.goto(`${STUDIO}/audit/ledger?twin=${TWIN}`);
  const replays = page.locator('a[href$="/replay"]');
  await replays.first().waitFor();
  await replays.last().click();
  await page.getByText('Historical artefacts').waitFor();
  await page.getByText(`${ONT}@${deployed.version}`).first().waitFor();
  await shot(page, '10-old-execution-replay', { fullPage: true });

  console.log(`tutorial screenshots written to docs/screenshots/tutorial/ (draft v${draft}, ${change.id}, ${released.package.id})`);
} finally {
  await browser.close();
}
