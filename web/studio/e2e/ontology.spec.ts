/**
 * Safely evolving an ontology, end to end on the real backend:
 * draft → edit → save → validate (aligner + Z3) → refinement (Def. 4) → impact → publish gating.
 */
import { expect, test, type APIRequestContext } from '@playwright/test';

const ONT = 'process-pump';

/**
 * Only one open draft per artefact is allowed. Earlier runs may have left one, so it is
 * rejected (audited, never deleted) before the journey starts.
 */
async function rejectOpenDrafts(request: APIRequestContext) {
  const artifact = await (await request.get(`/api/v1/artifacts/${ONT}`)).json();
  for (const v of artifact.versions as { version: number; state: string }[]) {
    if (['draft', 'validating', 'verified'].includes(v.state)) {
      const r = await request.post(`/api/v1/artifacts/${ONT}/versions/${v.version}/reject`, { data: { reason: 'e2e: superseded by a new test draft' } });
      expect(r.ok()).toBeTruthy();
    }
  }
}

test('broken edit shows diagnostics; valid edit validates, refines and preserves alignment', async ({ page, request }) => {
  await rejectOpenDrafts(request);
  await page.goto(`/engineering/ontologies/${ONT}/versions/1`);
  await expect(page.getByText(/is published and immutable/)).toBeVisible();

  await page.getByRole('button', { name: /create draft from v1/i }).click();
  await page.getByRole('dialog').getByRole('textbox').first().fill('e2e: document the pump domain');
  await page.getByRole('button', { name: 'Create draft' }).click();
  await expect(page).toHaveURL(new RegExp(`/engineering/ontologies/${ONT}/versions/(?!1$)\\d+$`));
  const draftUrl = page.url();

  // Publish is not offered as an action on an unvalidated draft.
  await expect(page.getByRole('button', { name: /^publish/i })).toBeDisabled();

  // 1. A broken edit: the backend parser reports a located diagnostic.
  const editor = page.locator('.cm-content').first();
  await editor.click();
  await page.keyboard.press('ControlOrMeta+End');
  await page.keyboard.type('\naxiom broken : (> undeclared_symbol 0)\n');
  await page.getByRole('button', { name: /save draft/i }).click();
  await expect(page.getByText(/undeclared/).first()).toBeVisible();

  // 2. Fix it: remove the broken line (a comment-only change keeps Def. 4).
  await editor.click();
  await page.keyboard.press('ControlOrMeta+End');
  for (let i = 0; i < 2; i++) await page.keyboard.press('ControlOrMeta+Shift+K');
  await page.keyboard.type('\n; documented by e2e\n');
  await page.getByRole('button', { name: /save draft/i }).click();
  await expect(page.getByText(/undeclared/)).toHaveCount(0);

  // 3. Validate: lifecycle moves to VERIFIED only on the backend's decision.
  await page.getByRole('button', { name: /^validate/i }).click();
  await expect(page.getByText(/verified/i).first()).toBeVisible({ timeout: 60_000 });
  await expect(page.getByRole('button', { name: /^publish/i })).toBeEnabled({ timeout: 60_000 });

  // 4. Refinement against the deployed version, with the deployed interpretations.
  await page.getByRole('button', { name: /check refinement/i }).click();
  await page.getByRole('button', { name: /run refinement check/i }).click();
  await expect(page).toHaveURL(/\/maintenance\/refinement\/EV-\d+/, { timeout: 60_000 });
  await expect(page.getByText('Valid refinement').first()).toBeVisible();

  // 5. Impact: the alignment is preserved by Theorem 3 and cites the evidence.
  const ref = new URL(draftUrl).pathname.split('/').pop();
  await page.goto(`/maintenance/impact?ref=${ONT}@${ref}`);
  await expect(page.getByText(/Theorem 3/).first()).toBeVisible();
});

test('a non-refinement is reported with its violated obligations', async ({ page, request }) => {
  const list = await (await request.get('/api/v1/evidence')).json();
  const items = (Array.isArray(list) ? list : list.items) as { id: string; kind: string; verdict: string }[];
  const failing = items.find((e) => e.kind === 'refinement' && e.verdict === 'not_a_refinement');
  test.skip(!failing, 'no failing refinement evidence in this data set');
  await page.goto(`/maintenance/refinement/${failing!.id}`);
  await expect(page.getByText('Not a refinement').first()).toBeVisible();
  await expect(page.getByText(/violated/i).first()).toBeVisible();
});
