/** Component and interaction tests (rendering only backend conclusions; API mocked with MSW). */
import { screen, waitFor, within } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { http, HttpResponse } from 'msw';
import { describe, expect, it } from 'vitest';
import type { TrustState } from '@/api/types';
import { ErrorBlock, TrustBadge } from '@/design';
import { ApiError } from '@/api/client';
import EngineeringAuditPage from '@/features/audit/EngineeringAuditPage';
import RefinementPage from '@/features/maintenance/RefinementPage';
import RollbackPage from '@/features/maintenance/RollbackPage';
import VersionPage from '@/features/engineering/VersionPage';
import { refinementEvidence, version } from './test/fixtures';
import { renderRoute, renderWithProviders, server } from './test/utils';

const STATES: [TrustState, string][] = [
  ['pass', 'Pass'],
  ['fail', 'Fail'],
  ['unknown', 'Unknown'],
  ['not_checked', 'Not checked'],
  ['stale', 'Stale'],
  ['invalidated', 'Invalidated'],
  ['check_running', 'Check running'],
  ['unavailable', 'Unavailable'],
  ['error', 'Check failed'],
];

describe('TrustBadge', () => {
  it.each(STATES)('renders %s with a text label and an icon (not colour only)', (state, label) => {
    const { container } = renderWithProviders(<TrustBadge state={state} label="Alignment" />);
    expect(screen.getByText(`Alignment: ${label}`)).toBeInTheDocument();
    expect(container.querySelector('svg')).not.toBeNull();
  });
  it('NOT CHECKED is never presented as passing', () => {
    renderWithProviders(<TrustBadge state="not_checked" />);
    expect(screen.queryByText(/pass/i)).toBeNull();
  });
});

describe('ErrorBlock', () => {
  it('shows "Runtime not connected" as an explained state, not a failure', () => {
    renderWithProviders(<ErrorBlock error={new ApiError(503, 'runtime_not_connected', 'No twin-runtime is connected')} />);
    expect(screen.getByText('Runtime not connected')).toBeInTheDocument();
    expect(screen.queryByText(/could not load/i)).toBeNull();
  });
  it('shows the backend message and hides internals by default', () => {
    renderWithProviders(<ErrorBlock error={new ApiError(409, 'state_error', 'only draft versions can be edited', [{ key: 'state', value: 'published' }])} />);
    expect(screen.getByText('only draft versions can be edited')).toBeInTheDocument();
    expect(screen.queryByText(/state: published/)).toBeNull();
  });
});

describe('RefinementPage', () => {
  const route = [{ path: '/maintenance/refinement/:evidenceId', element: <RefinementPage /> }];

  it('shows CHECK FAILED distinctly from NOT A REFINEMENT', async () => {
    server.use(
      http.get('/api/v1/evidence/EV-0042', () =>
        HttpResponse.json(refinementEvidence({ verdict: 'check_failed', summary: 'The refinement check could not be carried out', failureReasons: ['the base ontology is not valid'] })),
      ),
    );
    renderRoute(route, '/maintenance/refinement/EV-0042');
    expect(await screen.findByText('Check failed')).toBeInTheDocument();
    expect(screen.queryByText('Not a refinement')).toBeNull();
    expect(screen.getByText('the base ontology is not valid')).toBeInTheDocument();
  });

  it('shows violated obligations with their counter-model', async () => {
    server.use(
      http.get('/api/v1/evidence/EV-0042', () =>
        HttpResponse.json(
          refinementEvidence({
            verdict: 'not_a_refinement',
            summary: 'condition b violated',
            obligations: [{ condition: 'b', subject: 'axiom limit', statement: 'Δ ⊨ (<= limit 95)', status: 'violated', counterModel: [{ symbol: 'limit', value: '110' }], note: 'counter-model shown' }],
          }),
        ),
      ),
    );
    renderRoute(route, '/maintenance/refinement/EV-0042');
    expect(await screen.findByText('Not a refinement')).toBeInTheDocument();
    expect(screen.getByText('axiom limit')).toBeInTheDocument();
    expect(screen.getByText('110')).toBeInTheDocument();
  });

  it('refuses to show a verdict for an unreadable evidence document', async () => {
    server.use(http.get('/api/v1/evidence/EV-0042', () => HttpResponse.json({ ...refinementEvidence({}), document: { verdict: 'valid_refinement' } })));
    renderRoute(route, '/maintenance/refinement/EV-0042');
    expect(await screen.findByText('Unreadable refinement evidence')).toBeInTheDocument();
    expect(screen.queryByText('Valid refinement')).toBeNull();
  });
});

describe('VersionPage', () => {
  const route = [{ path: '/engineering/ontologies/:artifactId/versions/:version', element: <VersionPage /> }];
  const base = () => {
    server.use(
      http.get('/api/v1/artifacts/process-pump', () => HttpResponse.json({ id: 'process-pump', kind: 'ontology', name: 'Process pump domain', description: '', createdAt: '', createdBy: '', versions: [], interpretations: [], deployedIn: [] })),
      http.get('/api/v1/changes', () => HttpResponse.json([])),
    );
  };

  it('keeps Publish disabled until the draft is VERIFIED', async () => {
    base();
    server.use(http.get('/api/v1/artifacts/process-pump/versions/3', () => HttpResponse.json(version({ state: 'draft' }))));
    renderRoute(route, '/engineering/ontologies/process-pump/versions/3');
    const publish = await screen.findByRole('button', { name: /publish/i });
    expect(publish).toBeDisabled();
    expect(screen.getByRole('button', { name: /save draft/i })).toBeDisabled();
  });

  it('enables Publish for a VERIFIED draft and surfaces a 409 from the server', async () => {
    base();
    server.use(
      http.get('/api/v1/artifacts/process-pump/versions/3', () => HttpResponse.json(version({ state: 'verified' }))),
      http.post('/api/v1/artifacts/process-pump/versions/3/publish', () =>
        HttpResponse.json({ error: { code: 'state_error', message: 'only VERIFIED versions can be published; validate the draft first', context: [] } }, { status: 409 }),
      ),
    );
    window.confirm = () => true;
    renderRoute(route, '/engineering/ontologies/process-pump/versions/3');
    const publish = await screen.findByRole('button', { name: /publish/i });
    expect(publish).toBeEnabled();
    await userEvent.click(publish);
    expect(await screen.findByText('Not allowed in the current lifecycle state')).toBeInTheDocument();
  });

  it('shows published versions as immutable with a "create draft" action', async () => {
    base();
    server.use(http.get('/api/v1/artifacts/process-pump/versions/3', () => HttpResponse.json(version({ state: 'published' }))));
    renderRoute(route, '/engineering/ontologies/process-pump/versions/3');
    expect(await screen.findByText(/is published and immutable/)).toBeInTheDocument();
    expect(screen.getByRole('button', { name: /create draft from v3/i })).toBeInTheDocument();
    expect(screen.queryByRole('button', { name: /save draft/i })).toBeNull();
  });

  it('lists server diagnostics with their location', async () => {
    base();
    server.use(
      http.get('/api/v1/artifacts/process-pump/versions/3', () =>
        HttpResponse.json(version({ diagnostics: [{ code: 'ONT007', severity: 'error', message: "axiom 'a' uses undeclared symbol 'x'", span: { line: 3, column: 11, length: 1 } }] })),
      ),
    );
    renderRoute(route, '/engineering/ontologies/process-pump/versions/3');
    expect(await screen.findByText(/uses undeclared symbol/)).toBeInTheDocument();
    expect(screen.getByText('ONT007')).toBeInTheDocument();
  });
});

describe('RollbackPage', () => {
  it('requires a reason before rolling back', async () => {
    server.use(
      http.get('/api/v1/twins', () => HttpResponse.json([{ id: 'pump', name: 'Pump twin', presentation: {} }])),
      http.get('/api/v1/twins/pump', () =>
        HttpResponse.json({
          id: 'pump', name: 'Pump twin', presentation: {}, bindings: [], deployment: { packageId: 'PKG-0002' }, package: null,
          trust: Object.fromEntries(['alignment', 'ontologyRefinement', 'packageIntegrity', 'runtimeCompatibility', 'compilation'].map((k) => [k, { state: 'pass', detail: '' }])),
        }),
      ),
      http.get('/api/v1/packages', () => HttpResponse.json([{ id: 'PKG-0001', twinId: 'pump', state: 'released', bindings: [{ role: 'ontology', ref: 'process-pump@1' }] }, { id: 'PKG-0002', twinId: 'pump', state: 'released', bindings: [] }])),
      http.get('/api/v1/deployments/rollback-preview', () =>
        HttpResponse.json({
          current: { id: 'PKG-0002' }, target: { id: 'PKG-0001' }, allowed: true, targetEvidence: null,
          roles: [{ role: 'ontology', current: 'process-pump@3', target: 'process-pump@1', same: false }],
          targetIntegrity: { packageId: 'PKG-0001', integrity: 'pass', firstFailure: '', checks: [], verifiedAt: '2026-10-04T10:00:00Z', verifier: 'v', packageHash: 'x' },
        }),
      ),
    );
    renderRoute([{ path: '/maintenance/rollback', element: <RollbackPage /> }], '/maintenance/rollback?twin=pump');
    const button = await screen.findByRole('button', { name: /roll back to PKG-0001/i });
    expect(button).toBeDisabled();
    await userEvent.type(screen.getByRole('textbox'), 'field issue');
    expect(button).toBeEnabled();
    expect(screen.getByText('Differs')).toBeInTheDocument();
  });
});

describe('Engineering audit verification', () => {
  it('reports the first invalid record when the chain is broken', async () => {
    server.use(
      http.get('/api/v1/audit', () => HttpResponse.json({ items: [], total: 0, limit: 100, offset: 0 })),
      http.post('/api/v1/audit/verify', () =>
        HttpResponse.json({ valid: false, records: 5, firstInvalidSeq: 3, reason: 'content modified: the record no longer matches its hash', headHash: '0'.repeat(64), verifiedAt: '2026-10-04T10:00:00Z' }),
      ),
    );
    renderRoute([{ path: '/', element: <EngineeringAuditPage /> }], '/');
    await userEvent.click(await screen.findByRole('button', { name: /verify engineering audit chain/i }));
    const alert = await screen.findByText('AUDIT CHAIN INVALID');
    expect(within(alert.closest('[role]')!).getByText(/First invalid record #3/)).toBeInTheDocument();
    await waitFor(() => expect(screen.queryByText('AUDIT CHAIN VALID')).toBeNull());
  });
});
