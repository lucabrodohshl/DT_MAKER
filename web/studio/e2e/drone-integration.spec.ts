/**
 * End-to-end integration of the drone showcase, against the REAL running product
 * (start it with `make demo`; no mocks). The mission is driven deterministically through
 * the runtime API (each POST /simulation/step advances exactly 0.1 s of logical time)
 * while the assertions read what the GUI displays.
 *
 * Covers: application starts; frontend reaches backend; example assets load; drone
 * execution starts; telemetry arrives; runtime state changes; obstacle becomes known;
 * path becomes invalid; REPLANNING occurs; planner generates a replacement plan; the new
 * plan is executed; the mission reaches its targets; the ledger contains the semantic
 * transitions and verifies; replay loads the execution — and one transition traced from
 * the simulator's event to the ledger record to its display in the GUI.
 */
import { expect, test, type APIRequestContext } from '@playwright/test';

const TWIN = 'indoor-drone-dt';
const VIEW = '/assets/drone-01/view/drone';

async function rt<T>(request: APIRequestContext, method: 'GET' | 'POST', path: string, body: unknown = {}): Promise<T> {
  const url = `/api/v1/twins/${TWIN}${path}`;
  const res = method === 'GET' ? await request.get(url) : await request.post(url, { data: body });
  expect(res.ok(), `${method} ${path} -> ${res.status()}`).toBeTruthy();
  return (await res.json()) as T;
}

interface SimState { status: string; session: string; now: { ticks: number } }

async function stepTo(request: APIRequestContext, seconds: number): Promise<SimState> {
  let st = await rt<SimState>(request, 'GET', '/simulation/state');
  while (st.now.ticks < Math.round(seconds * 1000) && st.status !== 'finished' && st.status !== 'failed') {
    st = await rt<SimState>(request, 'POST', '/simulation/step');
  }
  return st;
}

test.describe.configure({ mode: 'serial' });

test('the product starts and the frontend reaches the backend', async ({ page }) => {
  await page.goto('/');
  await expect(page.getByText('Verified Twin Studio').first()).toBeVisible();
  await expect(page.getByText(/Asset estate/i)).toBeVisible();
  await expect(page.getByText(/Drone-01 mission twin/).first()).toBeVisible();
  await expect(page.getByText(/P-101 reliability twin/).first()).toBeVisible();
});

test('the drone example assets load', async ({ page }) => {
  await page.goto('/assets/drone-01');
  await expect(page.getByRole('heading', { name: 'Drone-01' })).toBeVisible();
  await expect(page.getByText(/Mission map/)).toBeVisible();
});

test('the drone mission runs through discovery, replanning and completion', async ({ page, request }) => {
  test.setTimeout(240_000);
  await rt(request, 'POST', '/simulation/reset');
  await rt(request, 'POST', '/mission/start');

  // Navigating on the initial route; telemetry flows into the view.
  await stepTo(request, 10.0);
  await page.goto(VIEW);
  await expect(page.getByText('Digital twin knowledge')).toBeVisible();
  await expect(page.getByText(/Navigating · NAVIGATING/)).toBeVisible();
  await expect(page.getByText(/^\d+\.\d % \(/).first()).toBeVisible(); // battery from telemetry

  // The closed fire door becomes known; the route is invalidated; the kernel enters REPLANNING.
  await stepTo(request, 15.6);
  await page.goto(VIEW);
  await expect(page.getByText(/Replanning · REPLANNING/)).toBeVisible();
  await expect(page.getByText('NAVIGATING → REPLANNING').first()).toBeVisible();
  await expect(page.getByText(/New obstacle discovered/).first()).toBeVisible();
  await expect(page.getByText(/route crosses door_closed/).first()).toBeVisible();

  // The planner's candidates are judged (geometry + kernel) and one is selected.
  await stepTo(request, 16.8);
  await page.goto(VIEW);
  await expect(page.getByText(/Episode 2 at t = 16\.8 s/)).toBeVisible();
  await expect(page.getByText('Admitted by kernel').first()).toBeVisible();
  await expect(page.getByText('Selected').first()).toBeVisible();

  // The new plan is executed (back to NAVIGATING) and the mission completes.
  await stepTo(request, 17.5);
  await page.goto(VIEW);
  await expect(page.getByText('REPLANNING → NAVIGATING').first()).toBeVisible();
  const done = await stepTo(request, 400);
  expect(done.status).toBe('finished');
  await page.goto(VIEW);
  await expect(page.getByText('Mission finished').first()).toBeVisible();
  await expect(page.getByRole('button', { name: 'Replay mission' })).toBeVisible();
});

test('the ledger verifies, replay is identical, and a transition is traceable to the GUI', async ({ page, request }) => {
  test.setTimeout(120_000);
  const st = await rt<SimState>(request, 'GET', '/simulation/state');
  const session = st.session;

  // Trace one PT observation: the simulator's poi_arrived! -> E -> target_reached! -> ledger record.
  interface StepBody {
    input?: { name: string; source: string; payload: { pt_label?: string } };
    outcome: { branches: { target: string }[] };
  }
  const ledger = await rt<{ records: { seq: number; hash: string; body: StepBody }[] }>(
    request, 'GET', `/runtime/ledger?session=${encodeURIComponent(session)}&kind=step&limit=500`);
  const reached = ledger.records.find((r) => r.body.input?.name === 'target_reached!');
  expect(reached, 'a target_reached! step is recorded').toBeTruthy();
  expect(reached!.body.input?.source).toBe('pt-adapter');
  expect(reached!.body.input?.payload.pt_label).toBe('poi_arrived!');
  expect(reached!.body.outcome.branches[0]?.target).toBe('INSPECTING');
  const transitions = ledger.records.map((r) => r.body.input?.name);
  for (const label of ['path_invalidated!', 'plan_accepted!', 'inspection_complete!', 'landing_complete!']) {
    expect(transitions).toContain(label);
  }

  // The GUI shows that very record with its hash.
  await page.goto(`/audit/executions/${TWIN}/${encodeURIComponent(session)}`);
  await expect(page.getByText(/target_reached!: NAVIGATING → INSPECTING/).first()).toBeVisible();
  await expect(page.getByText(reached!.hash.slice(0, 10)).first()).toBeVisible();

  // Verify Ledger performs the real verification.
  await page.getByRole('button', { name: /verify chain now/i }).click();
  await expect(page.getByText(/LEDGER VALID/)).toBeVisible();

  // Replay re-executes the recorded inputs with the recorded package.
  await page.goto(`/audit/executions/${TWIN}/${encodeURIComponent(session)}/replay`);
  await expect(page.getByText('Replay identical to the recorded execution')).toBeVisible({ timeout: 60_000 });
});
