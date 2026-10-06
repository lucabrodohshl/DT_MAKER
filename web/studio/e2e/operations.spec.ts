/**
 * Customer journey on the live pump twin: asset → telemetry → behaviour → meaning →
 * prediction → ledger verification → replay. Every conclusion shown comes from the backend.
 */
import { expect, test } from '@playwright/test';

const ASSET = 'pump-p101';

test('select an asset from the explorer and see its live mode', async ({ page }) => {
  await page.goto('/assets');
  await page.getByPlaceholder(/filter by name/i).fill('P-101');
  await page.getByRole('link', { name: 'Pump P-101' }).first().click();
  await expect(page).toHaveURL(new RegExp(`/assets/${ASSET}`));
  await expect(page.getByRole('heading', { name: 'Pump P-101' })).toBeVisible();
  await expect(page.getByText(/^Mode: /).first()).toBeVisible();
});

test('telemetry chart renders recorded history', async ({ page }) => {
  await page.goto(`/assets/${ASSET}/telemetry`);
  await expect(page.locator('.uplot canvas').first()).toBeVisible();
});

test('current behaviour, graph and enabled transitions come from the kernel', async ({ page }) => {
  await page.goto(`/assets/${ASSET}/behavior`);
  await expect(page.getByText('Committed semantic state of the verified kernel')).toBeVisible();
  await expect(page.getByText('Available next actions')).toBeVisible();
  await page.getByRole('tab', { name: 'Behavioural graph' }).click();
  await expect(page.locator('.react-flow__node').first()).toBeVisible();
});

test('"Why?" opens the backend evidence chain', async ({ page }) => {
  await page.goto(`/assets/${ASSET}/behavior`);
  await page.getByRole('button', { name: /why\?/i }).first().click();
  const drawer = page.getByRole('dialog');
  await expect(drawer).toBeVisible();
  await expect(drawer.getByText(/Observations used|Symbol|Truth/).first()).toBeVisible();
});

test('prediction explores from the live state', async ({ page }) => {
  await page.goto(`/assets/${ASSET}/predict`);
  await expect(page.getByText(/states explored/)).toBeVisible();
});

test('ledger chain verifies and an execution can be replayed', async ({ page }) => {
  await page.goto('/audit/ledger');
  await page.locator('a[href*="/audit/executions/"]').first().click();
  await page.getByRole('button', { name: /verify chain now/i }).click();
  await expect(page.getByText(/LEDGER (CHAIN )?VALID|Verified /).first()).toBeVisible();

  await page.goto('/audit/ledger');
  await page.getByRole('link', { name: /replay/i }).first().click();
  await expect(page).toHaveURL(/\/replay$/);
  await expect(page.getByText(/REPLAY MODE/i).first()).toBeVisible();
  await page.getByRole('button', { name: 'Step forward' }).click();
  await expect(page.getByLabel('Replay position')).toBeVisible();
});

test('an execution ledger exports as complete JSON', async ({ page }) => {
  await page.goto('/audit/ledger');
  await page.locator('a[href*="/audit/executions/"]:not([href$="/replay"])').first().click();
  const [download] = await Promise.all([page.waitForEvent('download'), page.getByRole('button', { name: /export ledger/i }).click()]);
  const doc = JSON.parse(await (await download.createReadStream()).toArray().then((c) => Buffer.concat(c).toString('utf8')));
  expect(doc.records.length).toBe(doc.total_records);
  expect(doc.records[0].kind).toBe('genesis');
});
