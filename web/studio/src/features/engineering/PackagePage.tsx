/** One package: bound artefacts, build evidence, live integrity re-verification, deployments. */
import { useMutation, useQueryClient } from '@tanstack/react-query';
import { ShieldCheck } from 'lucide-react';
import { Link, useParams } from 'react-router-dom';
import { keys, usePackage, verifyPackage } from '@/api/queries';
import { Button, Callout, HashChip, KeyValue, OutcomeBadge, PageHeader, Panel, QueryState, StatusBadge, TimeStamp, TrustBadge, humanize } from '@/design';
import { ChangeLink, Crumbs, EvidenceLink, RefLink } from '@/features/common/links';

/** @param id Package to show (twin workspace); otherwise the :packageId route parameter. */
export default function PackagePage({ id }: { id?: string } = {}) {
  const params = useParams();
  const packageId = id ?? params.packageId ?? '';
  const q = usePackage(packageId);
  const qc = useQueryClient();
  const verify = useMutation({ mutationFn: () => verifyPackage(packageId), onSuccess: () => void qc.invalidateQueries({ queryKey: keys.package(packageId) }) });
  return (
    <div className="vts-page">
      <QueryState query={q}>
        {(p) => {
          const integrity = verify.data ?? p.integrity;
          return (
            <div className="stack">
              <PageHeader
                eyebrow={<Crumbs items={[{ label: 'Studio', to: '/studio' }, { label: 'Packages', to: '/studio/packages' }, { label: p.id }]} />}
                title={`Package ${p.id}`}
                meta={
                  <>
                    <StatusBadge tone={p.state === 'released' ? 'ok' : 'info'} label={p.state === 'released' ? 'Released' : 'Built'} />
                    <span>{p.twinId} · model version {p.modelVersion}</span>
                    {p.changeId && <span>change <ChangeLink id={p.changeId} /></span>}
                  </>
                }
                actions={<Button variant="primary" icon={<ShieldCheck size={14} />} loading={verify.isPending} onClick={() => verify.mutate()}>Re-verify integrity now</Button>}
              />
              <div className="grid-main-side">
                <div className="stack">
                  <Panel title="Bound artefacts" subtitle="Exactly these versions and bytes were compiled, aligned and packaged">
                    <table className="vts-table">
                      <caption className="sr-only">Bindings</caption>
                      <thead><tr><th scope="col">Role</th><th scope="col">Version</th><th scope="col">Content hash</th></tr></thead>
                      <tbody>
                        {p.bindings.map((b) => (
                          <tr key={b.role}>
                            <td>{humanize(b.role)}</td>
                            <td><RefLink refId={b.ref} /></td>
                            <td><HashChip value={b.sha256} /></td>
                          </tr>
                        ))}
                      </tbody>
                    </table>
                  </Panel>
                  <Panel title="Integrity" subtitle={integrity ? <>Verified <TimeStamp value={integrity.verifiedAt} relative /> by {integrity.verifier}</> : undefined} flush>
                    {integrity ? (
                      <div className="stack">
                        <div style={{ padding: 'var(--s-3) var(--s-4) 0' }}>
                          <TrustBadge state={integrity.integrity} label="Package integrity" size="lg" />
                          {integrity.integrity === 'fail' && <Callout tone="critical" title="First failure">{integrity.firstFailure}</Callout>}
                        </div>
                        <div className="vts-table-wrap" style={{ maxHeight: 360 }}>
                          <table className="vts-table">
                            <caption className="sr-only">Integrity checks</caption>
                            <tbody>
                              {integrity.checks.map((c) => (
                                <tr key={c.name}><td className="small">{c.name}</td><td><StatusBadge tone={c.passed ? 'ok' : 'critical'} label={c.passed ? 'Passed' : 'Failed'} /></td></tr>
                              ))}
                            </tbody>
                          </table>
                        </div>
                      </div>
                    ) : (
                      <p className="small muted" style={{ padding: 16 }}>Integrity could not be determined.</p>
                    )}
                  </Panel>
                </div>
                <div className="stack">
                  <Panel title="Identity">
                    <KeyValue
                      compact
                      items={[
                        ['Package hash', <HashChip key="p" value={p.packageHash} />],
                        ['Twin IR', <HashChip key="i" value={p.irSha256} />],
                        ['Built', <TimeStamp key="c" value={p.createdAt} />],
                        ['Built by', p.createdBy],
                        ['Released', p.releasedAt ? <TimeStamp key="r" value={p.releasedAt} /> : 'not released'],
                        ['Build evidence', p.evidenceId ? <EvidenceLink key="e" id={p.evidenceId} /> : '—'],
                      ]}
                    />
                    {p.buildEvidence && <div style={{ marginTop: 8 }}><OutcomeBadge outcome={p.buildEvidence.outcome} verdict={p.buildEvidence.verdict} /></div>}
                  </Panel>
                  <Panel title="Deployments of this package">
                    {p.deployments.length === 0 ? (
                      <p className="small muted">Never deployed.</p>
                    ) : (
                      <ul className="vts-list">
                        {p.deployments.map((d) => (
                          <li key={d.id} className="small">
                            {d.id} ({d.kind}) <TimeStamp value={d.deployedAt} /> by {d.deployedBy}
                          </li>
                        ))}
                      </ul>
                    )}
                    <Link className="small" to={`/studio/deployments?twin=${encodeURIComponent(p.twinId)}`}>Deployment history</Link>
                  </Panel>
                </div>
              </div>
            </div>
          );
        }}
      </QueryState>
    </div>
  );
}
