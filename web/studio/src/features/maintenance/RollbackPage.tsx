/**
 * Rollback to an earlier released package: preview of every artefact difference,
 * live integrity of the target, reason required; recorded as a new deployment
 * (nothing newer is deleted).
 */
import { Undo2 } from 'lucide-react';
import { useState } from 'react';
import { useSearchParams } from 'react-router-dom';
import { api } from '@/api/client';
import { useEngineeringMutation, usePackages, useRollbackPreview, useTwin, useTwins } from '@/api/queries';
import { Button, Callout, EmptyState, ErrorBlock, PageHeader, Panel, QueryState, StatusBadge, TrustBadge, humanize } from '@/design';
import { Crumbs, PackageLink, RefLink, LearnMore } from '@/features/common/links';
import { useTwinScope } from '@/app/twinScope';

export default function RollbackPage() {
  const [params, setParams] = useSearchParams();
  const scope = useTwinScope();
  const twins = useTwins();
  const twinId = scope?.twin.id ?? params.get('twin') ?? twins.data?.[0]?.id ?? '';
  const twin = useTwin(twinId || null);
  const packages = usePackages(twinId || undefined);
  const current = twin.data?.deployment?.packageId;
  const candidates = (packages.data ?? []).filter((p) => p.state === 'released' && p.id !== current);
  const target = params.get('package') ?? candidates[0]?.id ?? '';
  const preview = useRollbackPreview(twinId || undefined, target || undefined);
  const [reason, setReason] = useState('');
  const rollback = useEngineeringMutation((b: { twinId: string; packageId: string; reason: string }) => api.post('/deployments/rollback', b));
  return (
    <div className="vts-page">
      <PageHeader eyebrow={<Crumbs items={[{ label: 'Studio', to: '/studio' }, { label: 'Changes', to: '/studio/changes' }, { label: 'Rollback' }]} />} title="Rollback" meta={<><span>Return a twin to an earlier verified package. Newer artefacts and packages are kept.</span><LearnMore page="release-and-deployment.html#rollback" /></>} />
      <div className="row-wrap" style={{ marginBottom: 16 }}>
        {!scope && <label className="row small">Twin
          <select className="vts-select" value={twinId} onChange={(e) => setParams({ twin: e.target.value })}>
            {twins.data?.map((t) => <option key={t.id} value={t.id}>{t.name}</option>)}
          </select>
        </label>}
        {candidates.length > 0 && <label className="row small">Target package
          <select className="vts-select" value={target} onChange={(e) => setParams({ twin: twinId, package: e.target.value })}>
            {candidates.map((p) => <option key={p.id} value={p.id}>{p.id} · {p.bindings.find((b) => b.role === 'ontology')?.ref}</option>)}
          </select>
        </label>}
      </div>
      {candidates.length === 0 ? (
        <EmptyState title="No rollback target">This twin has no other released package.</EmptyState>
      ) : (
        <QueryState query={preview}>
          {(p) => (
            <div className="grid-main-side">
              <Panel title={<span>Current <PackageLink id={p.current.id} /> → target <PackageLink id={p.target.id} /></span>} flush>
                <table className="vts-table">
                  <caption className="sr-only">Differences</caption>
                  <thead><tr><th scope="col">Artefact</th><th scope="col">Current</th><th scope="col">After rollback</th><th scope="col" /></tr></thead>
                  <tbody>
                    {p.roles.map((r) => (
                      <tr key={r.role}>
                        <td>{humanize(r.role)}</td>
                        <td>{r.current ? <RefLink refId={r.current} /> : '—'}</td>
                        <td>{r.target ? <RefLink refId={r.target} /> : '—'}</td>
                        <td>{r.same ? <StatusBadge tone="neutral" label="Same" /> : <StatusBadge tone="warning" label="Differs" />}</td>
                      </tr>
                    ))}
                  </tbody>
                </table>
              </Panel>
              <Panel title="Rollback">
                <div className="stack">
                  {p.targetIntegrity && <TrustBadge state={p.targetIntegrity.integrity} label="Target package integrity (verified now)" />}
                  {p.targetEvidence && <span className="small">Build evidence: {p.targetEvidence.summary}</span>}
                  {!p.allowed && <Callout tone="critical">This package cannot be a rollback target (not released, failed integrity, or already deployed).</Callout>}
                  <label className="vts-field">
                    <span>Reason (required, recorded in the engineering audit)</span>
                    <textarea className="vts-textarea" value={reason} onChange={(e) => setReason(e.target.value)} />
                  </label>
                  <Button variant="primary" icon={<Undo2 size={14} />} disabled={!p.allowed || !reason.trim()} loading={rollback.isPending}
                    onClick={() => rollback.mutate({ twinId, packageId: p.target.id, reason: reason.trim() })}>
                    Roll back to {p.target.id}
                  </Button>
                  {rollback.error && <ErrorBlock error={rollback.error} compact />}
                  {rollback.isSuccess && <Callout tone="ok" title="Rolled back">The twin now runs {p.target.id}. The previous package remains available.</Callout>}
                </div>
              </Panel>
            </div>
          )}
        </QueryState>
      )}
    </div>
  );
}
