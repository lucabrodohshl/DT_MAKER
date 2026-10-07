/**
 * End-to-end: Blueprint Studio against the REAL product (start it with scripts/start-demo.sh;
 * no mocks). Twins are built through the UI an engineer uses — the New Blueprint wizard, the
 * model and semantics import controls, Run all checks, package, publish, instance, deployment,
 * Operate — and the assertions read what the UI shows, which is what the backend decided.
 * Long form-filling (every field of the data contract) is replaced by the section saves the
 * editors themselves send (PUT .../sections/{section}); everything formal goes through the UI.
 *
 *   STUDIO_URL=http://127.0.0.1:8080 npx playwright test e2e/blueprint-studio.spec.ts
 */
import { expect, test, type APIRequestContext, type Page } from '@playwright/test';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

const ROOT = fileURLToPath(new URL('../../../', import.meta.url));
const CHAMBER = `${ROOT}examples/thermal-chamber`;
const DRONE = `${ROOT}examples/indoor-drone`;
const DRONE_MODELS = `${ROOT}models/indoor_drone`;
const STAMP = Date.now().toString(36);

type Doc = Record<string, unknown> & { identity: Record<string, unknown> };

function readJson(path: string): Doc {
  return JSON.parse(readFileSync(path, 'utf8')) as Doc;
}

/** The example document with its {"$file": ...} sections inlined (as the seeder resolves them). */
function exampleDocument(dir: string): Doc {
  const doc = readJson(`${dir}/blueprint.json`);
  for (const [k, v] of Object.entries(doc)) {
    if (v && typeof v === 'object' && '$file' in (v as Record<string, unknown>)) doc[k] = readJson(`${dir}/${(v as { $file: string }).$file}`);
  }
  return doc;
}

async function api<T>(request: APIRequestContext, method: 'GET' | 'POST' | 'PUT' | 'DELETE', path: string, body?: unknown): Promise<T> {
  const url = `/api/v1${path}`;
  const res =
    method === 'GET' ? await request.get(url) : method === 'DELETE' ? await request.delete(url) : await request.fetch(url, { method, data: body ?? {} });
  expect(res.ok(), `${method} ${path} -> ${res.status()} ${await res.text()}`).toBeTruthy();
  return (await res.json()) as T;
}

/** Save sections exactly as the editors do: each save is based on the current revision. */
async function saveSections(request: APIRequestContext, id: string, doc: Doc, sections: string[]) {
  for (const section of sections) {
    const v = await api<{ revision: number }>(request, 'GET', `/blueprints/${id}/versions/1`);
    await api(request, 'PUT', `/blueprints/${id}/versions/1/sections/${section}`, { revision: v.revision, content: doc[section] });
  }
}

const SECTIONS = ['identity', 'structure', 'world', 'data', 'connectivity', 'presentation', 'assurance', 'simulation', 'scenarios'];

async function importModel(page: Page, base: string, role: 'pt' | 'dt', file: string) {
  await page.goto(`${base}/behavior/${role}`);
  await page.getByRole('button', { name: /^Import/ }).first().click();
  await page.getByLabel('Model file').setInputFiles(file);
  await page.getByRole('button', { name: 'Import into this draft' }).click();
  await expect(page.getByText(/^Imported \(/)).toBeVisible();
  await page.getByRole('button', { name: 'Done' }).click();
}

/** Import a semantics file in its editor and wait until the backend holds exactly that text. */
async function importSemantics(page: Page, request: APIRequestContext, id: string, role: string, url: string, file: string) {
  await page.goto(url);
  await page.locator('label', { hasText: 'Import file' }).locator('input[type=file]').setInputFiles(file);
  const text = readFileSync(file, 'utf8');
  await expect
    .poll(async () => (await api<{ artifact: { content: string } | null }>(request, 'GET', `/blueprints/${id}/versions/1/semantics/${role}`)).artifact?.content === text, {
      timeout: 30_000,
    })
    .toBeTruthy();
}

async function runAllChecks(page: Page, base: string) {
  await page.goto(`${base}/assurance/verification`);
  await page.getByRole('button', { name: 'Run all checks' }).click();
  await expect(page.getByText('VERIFIED — every formal check passes for these inputs')).toBeVisible({ timeout: 240_000 });
}

test.describe.configure({ mode: 'serial' });

test('Simple Thermal Chamber: from a blank Blueprint to a deployed, monitored instance', async ({ page, request }) => {
  test.setTimeout(600_000);
  const name = `E2E Chamber ${STAMP}`;
  const id = `e2e-chamber-${STAMP}`;

  // Steps 1-2 of the wizard: blank Blueprint, identity; the draft exists on the server.
  await page.goto('/studio');
  await page.getByRole('button', { name: 'New Blueprint' }).first().click();
  await page.getByRole('radio', { name: /Blank Blueprint/ }).click();
  await page.getByRole('button', { name: 'Next: identity' }).click();
  await page.getByLabel('Name').fill(name);
  await expect(page.getByRole('textbox', { name: /^Id\b/ })).toHaveValue(id);
  await page.getByRole('button', { name: 'Create and continue (guided)' }).click();
  await expect(page.getByText('Step 3 of 11: Structure')).toBeVisible();
  const base = `/studio/blueprints/${id}/v/1`;

  // Build sections (what the Structure, World, Data, Presentation, Assurance and Scenario editors save).
  const doc = exampleDocument(CHAMBER);
  doc.identity = { ...doc.identity, name, modelId: id };
  await saveSections(request, id, doc, SECTIONS);

  // Behaviour and semantics through the UI: UPPAAL import of the PT view, canonical DT view, then
  // the ontology and both interpretations from files.
  await importModel(page, base, 'pt', `${CHAMBER}/models/chamber_pt.xml`);
  await importModel(page, base, 'dt', `${CHAMBER}/models/chamber_dt.tta.json`);
  await importSemantics(page, request, id, 'ontology', `${base}/semantics/ontology`, `${CHAMBER}/models/thermal-chamber.ont`);
  await importSemantics(page, request, id, 'dt_interpretation', `${base}/semantics/interpretations?view=dt`, `${CHAMBER}/models/dt.interp`);
  await importSemantics(page, request, id, 'pt_interpretation', `${base}/semantics/interpretations?view=pt`, `${CHAMBER}/models/pt.interp`);

  // The cross-layer view connects every signal of the chamber.
  await page.goto(`${base}/semantics/binding`);
  await expect(page.getByText('Air temperature').first()).toBeVisible();

  // Assurance: the real validators, compiler, aligner and scenario tests.
  await runAllChecks(page, base);
  await page.goto(`${base}/assurance/alignment`);
  await expect(page.getByText('PASS', { exact: true })).toBeVisible();
  await expect(page.getByText(/Strong: PASS|Strong: UNKNOWN/)).toBeVisible();

  // Release: package and bundle, then publish behind the gate.
  await page.goto(`${base}/release/package`);
  await page.getByRole('button', { name: 'Build package & bundle' }).click();
  await expect(page.getByText('READY TO RELEASE', { exact: true }).first()).toBeVisible({ timeout: 180_000 });
  await expect(page.getByRole('heading', { name: 'Verified Core Package' })).toBeVisible();
  await expect(page.getByRole('heading', { name: 'Deployment Bundle' })).toBeVisible();
  await page.getByRole('button', { name: 'Publish v1' }).click();
  await page.getByRole('button', { name: 'Publish', exact: true }).click();
  await expect(page).toHaveURL(new RegExp(`${base}/release/instances`));

  // An instance of the published version, deployed under the supervisor.
  const instance = `e2e-tc-${STAMP}`;
  await page.getByRole('button', { name: 'New instance' }).click();
  await page.getByLabel('Instance id').fill(instance);
  await page.getByLabel('Display name').fill(`TC ${STAMP}`);
  await page.getByRole('button', { name: 'Create instance' }).click();
  await page.getByRole('button', { name: 'Deploy now' }).click();
  await expect(page).toHaveURL(new RegExp(`${base}/release/deployment`));
  await expect(page.getByText('DEPLOYED · RUNNING').first()).toBeVisible({ timeout: 90_000 });

  // Operate: the twin runs, and its monitors are evaluated live by the backend.
  await page.getByRole('link', { name: 'Open in Operate' }).first().click();
  await expect(page).toHaveURL(new RegExp(`/twins/${instance}`));
  await page.goto(`/twins/${instance}/behavior/monitors`);
  await expect(page.getByRole('heading', { name: 'Monitors' }).first()).toBeVisible();
  await expect(page.getByText('Behavioural conformance')).toBeVisible();
  await expect(page.getByText(/SATISFIED|VIOLATED/).first()).toBeVisible({ timeout: 60_000 });

  await api(request, 'POST', `/instances/${instance}/stop`);
});

test('Indoor drone: formal models imported into a new Blueprint run in an isolated Studio preview', async ({ page, request }) => {
  test.setTimeout(600_000);
  const name = `E2E Drone ${STAMP}`;
  const id = `e2e-drone-${STAMP}`;

  await page.goto('/studio/new?mode=formal');
  await expect(page.getByRole('radio', { name: /Import formal models/ })).toHaveAttribute('aria-checked', 'true');
  const inputs = page.locator('input[type=file]');
  await inputs.nth(0).setInputFiles(`${DRONE_MODELS}/V_P_flight_controller.xml`);
  await inputs.nth(1).setInputFiles(`${DRONE_MODELS}/V_D_mission_supervisor.xml`);
  await inputs.nth(2).setInputFiles(`${DRONE_MODELS}/domain.ont`);
  await inputs.nth(3).setInputFiles(`${DRONE_MODELS}/pt.interp`);
  await inputs.nth(4).setInputFiles(`${DRONE_MODELS}/dt.interp`);
  await page.getByRole('button', { name: 'Next: identity' }).click();
  await page.getByLabel('Name').fill(name);
  await page.getByLabel('Runtime mode').selectOption('cosimulation');
  await page.getByRole('button', { name: 'Create and open editor' }).click();
  await expect(page).toHaveURL(new RegExp(`/studio/blueprints/${id}/v/1`));
  const base = `/studio/blueprints/${id}/v/1`;

  // The UPPAAL views were converted on the server, not approximated in the browser.
  await page.goto(`${base}/behavior/dt`);
  await expect(page.getByText('NAVIGATING').first()).toBeVisible();

  // World, data contract, simulator and assurance of the drone (what the World and Data editors save).
  const doc = exampleDocument(DRONE);
  doc.identity = { ...doc.identity, name, modelId: id };
  await saveSections(request, id, doc, SECTIONS);

  await runAllChecks(page, base);

  // The mission runs in an isolated STUDIO PREVIEW: no twin record, the real simulator and runtime.
  await page.goto(`${base}/test/preview`);
  await expect(page.getByText('STUDIO PREVIEW', { exact: true })).toBeVisible();
  await page.getByRole('button', { name: 'Start preview' }).click();
  await expect(page.getByText('RUNNING', { exact: true })).toBeVisible({ timeout: 120_000 });
  await expect(page.getByText('Mission map (preview)')).toBeVisible();
  await expect(page.getByText('Physical world')).toBeVisible();
  const twins = await api<{ id: string }[]>(request, 'GET', '/twins');
  expect(twins.some((t) => t.id.startsWith('preview~'))).toBeFalsy();
  await page.getByRole('button', { name: 'Stop preview' }).click();
  await expect(page.getByText('STOPPED', { exact: true })).toBeVisible({ timeout: 30_000 });
});

test('Centrifugal pump: the Scenario Builder refuses an illegal event with the kernel’s earliest legal time', async ({ page }) => {
  test.setTimeout(300_000);
  const id = `e2e-pump-${STAMP}`;

  // A non-drone Blueprint: clone the published pump through the New Blueprint page.
  await page.goto('/studio/new?mode=clone&from=centrifugal-pump');
  await page.getByRole('button', { name: 'Next: identity' }).click();
  await page.getByLabel('Name').fill(`E2E Pump ${STAMP}`);
  await page.getByRole('button', { name: 'Create and open editor' }).click();
  await expect(page).toHaveURL(new RegExp(`/studio/blueprints/${id}/v/1`));

  await page.goto(`/studio/blueprints/${id}/v/1/test/scenarios/cooling-too-short`);
  // Availability at the end of the scenario comes from the kernel: cooling completes LATER.
  const row = page.locator('.vts-sc-avail__row', { hasText: 'cooling_complete!' });
  await expect(row).toContainText('LATER');
  await row.click();
  await page.getByLabel('Time of the event').fill('40');
  await page.getByRole('button', { name: 'Add event' }).click();
  await expect(page.getByText('Not allowed: cooling_complete! at t = 40')).toBeVisible();
  await expect(page.getByText(/too early/)).toBeVisible();
  await page.getByRole('button', { name: 'Move to earliest legal time (t = 50)' }).click();

  // The step is added at the legal time and the scenario passes as a test.
  const step = page.locator('.vts-sc-steps tbody tr').filter({ hasText: 'Event' }).filter({ hasText: 'cooling_complete!' });
  await expect(step.getByLabel(/Time of/)).toHaveValue('50');
  await expect(page.getByText('All changes saved')).toBeVisible({ timeout: 20_000 });
  await page.getByRole('button', { name: 'Run scenario' }).click();
  await expect(page.getByText(/^PASS — /)).toBeVisible({ timeout: 60_000 });
});
