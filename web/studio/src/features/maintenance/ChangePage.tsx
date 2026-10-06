/**
 * Change workspace: coordinated drafts of one twin, their diffs, the release
 * pipeline (computed from evidence on every view), release and deployment.
 */
import { useMutation, useQueryClient } from '@tanstack/react-query';
import { FileDiff, Network, Plus, Rocket, Upload, XCircle } from 'lucide-react';
import { useState } from 'react';
import { Link, useParams } from 'react-router-dom';
import { api, ApiError } from '@/api/client';
import { keys, useChange, useDiff, useEngineeringMutation, useInvalidateEngineering, usePipeline, useTwin } from '@/api/queries';
import type { Change, StructuralChange } from '@/api/types';
import {
  Button,
  Callout,
  Dialog,
  EmptyState,
  ErrorBlock,
  LifecycleBadge,
  PageHeader,
  Panel,
  QueryState,
  StatusBadge,
  TimeStamp,
  humanize,
} from '@/design';
import { Crumbs, PackageLink, RefLink, versionRoute, LearnMore } from '@/features/common/links';
import { PipelineView } from './PipelineView';

export function StructuralChanges({ changes }: { changes: StructuralChange[] }) {
  if (changes.length === 0) return <p className="small muted">No structural change (comments or whitespace only).</p>;
  const tone = (k: string) => (k === 'added' ? 'ok' : k === 'removed' ? 'critical' : k === 'renamed' ? 'neutral' : 'warning');
  return (
    <table className="vts-table">
      <caption className="sr-only">Structural changes</caption>
      <thead><tr><th scope="col">Change</th><th scope="col">Element</th><th scope="col">Before</th><th scope="col">After</th></tr></thead>
      <tbody>
        {changes.map((c, i) => (
          <tr key={i}>
            <td><StatusBadge tone={tone(c.kind)} label={humanize(c.kind)} /></td>
            <td><span className="xsmall subtle">{c.element}</span> <span className="mono small strong">{c.name}</span>{c.previousName && <div className="xsmall subtle">was {c.previousName}</div>}</td>
            <td className="mono xsmall" style={{ maxWidth: 280, overflowWrap: 'anywhere' }}>{c.before || '—'}</td>
            <td className="mono xsmall" style={{ maxWidth: 280, overflowWrap: 'anywhere' }}>{c.after || '—'}</td>
          </tr>
        ))}
      </tbody>
    </table>
  );
}

function DraftDiff({ deployedRef, draftRef }: { deployedRef: string; draftRef: string }) {
  const d = useDiff(deployedRef, draftRef);
  return (
    <QueryState query={d} compact>
      {(r) => (
        <div className="stack-sm">
          {r.structural ? <StructuralChanges changes={r.structural.changes} /> : <p className="small muted">Model: see the source comparison.</p>}
          {r.affectedInterpretations.filter((a) => a.entries.length > 0).map((a) => (
            <Callout key={a.ref} tone="warning" title={`Affected entries in ${a.ref}`}>
              {a.entries.join(', ')} — their meaning may change under the new axioms; the refinement check decides.
            </Callout>
          ))}
        </div>
      )}
    </QueryState>
  );
}

function AddArtifactDialog({ change, open, onOpenChange }: { change: Change; open: boolean; onOpenChange: (o: boolean) => void }) {
  const twin = useTwin(change.twinId);
  const [artifact, setArtifact] = useState('');
  const [description, setDescription] = useState('');
  const add = useEngineeringMutation((b: { artifactId: string; description: string }) => api.post(`/changes/${change.id}/artifacts`, b));
  const candidates = twin.data?.bindings.filter((b) => !change.artifacts.some((a) => a.startsWith(`${b.artifactId}@`))) ?? [];
  const chosen = artifact || candidates[0]?.artifactId || '';
  return (
    <Dialog
      open={open}
      onOpenChange={onOpenChange}
      title="Add an artefact to this change"
      description="A new draft is created from the version the twin currently runs."
      footer={
        <>
          <Button onClick={() => onOpenChange(false)}>Cancel</Button>
          <Button variant="primary" disabled={!chosen || !description.trim()} loading={add.isPending} onClick={() => add.mutate({ artifactId: chosen, description }, { onSuccess: () => onOpenChange(false) })}>
            Create draft
          </Button>
        </>
      }
    >
      <div className="stack">
        <label className="vts-field">
          <span>Artefact (deployed version)</span>
          <select className="vts-select" value={chosen} onChange={(e) => setArtifact(e.target.value)}>
            {candidates.map((b) => <option key={b.artifactId} value={b.artifactId}>{humanize(b.role)}: {b.ref}</option>)}
          </select>
        </label>
        <label className="vts-field">
          <span>Change description</span>
          <textarea className="vts-textarea" value={description} onChange={(e) => setDescription(e.target.value)} />
        </label>
        {add.error && <ErrorBlock error={add.error} compact />}
      </div>
    </Dialog>
  );
}

export default function ChangePage() {
  const { changeId = '' } = useParams();
  const change = useChange(changeId);
  const pipeline = usePipeline(changeId);
  const qc = useQueryClient();
  const invalidate = useInvalidateEngineering();
  const [addOpen, setAddOpen] = useState(false);
  const [abandonOpen, setAbandonOpen] = useState(false);
  const [reason, setReason] = useState('');
  const [deployReason, setDeployReason] = useState('');
  const runStage = useMutation({
    mutationFn: (stage: string) => api.post(`/changes/${changeId}/stages/${stage}/run`),
    onSettled: () => {
      void invalidate();
      void qc.invalidateQueries({ queryKey: keys.pipeline(changeId) });
    },
  });
  const release = useEngineeringMutation(() => api.post(`/changes/${changeId}/release`));
  const abandon = useEngineeringMutation((r: string) => api.post(`/changes/${changeId}/abandon`, { reason: r }));
  const deploy = useEngineeringMutation((b: { twinId: string; packageId: string; reason: string }) => api.post('/deployments', b));

  return (
    <div className="vts-page">
      <QueryState query={change}>
        {(c) => {
          const open = c.state === 'open';
          const p = pipeline.data;
          const pkg = p?.packageId ?? null;
          const releaseStage = p?.stages.find((s) => s.id === 'release');
          const deployStage = p?.stages.find((s) => s.id === 'deploy');
          return (
            <div className="stack">
              <PageHeader
                eyebrow={<Crumbs items={[{ label: 'Studio', to: '/studio' }, { label: 'Changes', to: '/studio/changes' }, { label: c.id }]} />}
                title={c.title}
                meta={
                  <>
                    <StatusBadge tone={open ? 'info' : c.state === 'released' ? 'ok' : 'neutral'} label={c.state} />
                    <span>twin {c.twin?.name ?? c.twinId}</span>
                    <span>opened <TimeStamp value={c.createdAt} /> by {c.createdBy}</span>
                    <LearnMore page="release-and-deployment.html#release-pipeline">About the release pipeline</LearnMore>
                  </>
                }
                actions={
                  open && (
                    <>
                      <Button icon={<Plus size={14} />} onClick={() => setAddOpen(true)}>Add artefact</Button>
                      <Button variant="primary" icon={<Upload size={14} />} disabled={!p?.releaseReady} loading={release.isPending}
                        title={p?.releaseReady ? undefined : `Blocked by: ${p?.blocking?.join(', ') ?? 'pipeline'}`}
                        onClick={() => { if (window.confirm('Release: publish every draft of this change and release its package?')) release.mutate(undefined); }}>
                        Release
                      </Button>
                      <Button variant="danger" icon={<XCircle size={14} />} onClick={() => setAbandonOpen(true)}>Abandon</Button>
                    </>
                  )
                }
              />
              {c.description && <p className="small muted">{c.description}</p>}
              {[release.error, abandon.error, deploy.error, runStage.error].filter(Boolean).map((e, i) => (
                <Callout key={i} tone="critical" title={e instanceof ApiError && e.isConflict ? 'Blocked' : 'Failed'}>
                  {(e as Error).message}
                  {e instanceof ApiError && e.context.length > 0 && <div className="xsmall">{e.context.map((x) => `${x.key}: ${x.value}`).join(' · ')}</div>}
                </Callout>
              ))}
              <div className="grid-main-side" style={{ gridTemplateColumns: 'minmax(0, 1fr) 460px' }}>
                <div className="stack">
                  <Panel title="Artefacts in this change" flush>
                    {(c.artifactVersions ?? []).length === 0 ? (
                      <EmptyState compact title="No drafts yet" action={open ? <Button size="sm" onClick={() => setAddOpen(true)}>Add artefact</Button> : undefined} />
                    ) : (
                      <ul className="vts-list" style={{ padding: '0 var(--s-4)' }}>
                        {c.artifactVersions!.map((v) => (
                          <li key={v.ref} className="stack-sm">
                            <div className="row-between">
                              <span className="row-wrap">
                                <strong>{v.name ?? v.artifactId}</strong>
                                <Link to={versionRoute(v.kind, v.ref)} className="mono small">{v.ref}</Link>
                                <LifecycleBadge state={v.state} />
                                {v.deployedRef && <span className="xsmall subtle">replaces deployed {v.deployedRef}</span>}
                              </span>
                              <span className="row">
                                {v.deployedRef && (
                                  <Link className="vts-btn vts-btn--sm" to={`/studio/history?from=${encodeURIComponent(v.deployedRef)}&to=${encodeURIComponent(v.ref)}`}><FileDiff size={13} /> Source diff</Link>
                                )}
                                <Link className="vts-btn vts-btn--sm" to={`/studio/impact?ref=${encodeURIComponent(v.ref)}`}><Network size={13} /> Impact</Link>
                              </span>
                            </div>
                            {v.deployedRef && <DraftDiff deployedRef={v.deployedRef} draftRef={v.ref} />}
                          </li>
                        ))}
                      </ul>
                    )}
                  </Panel>
                  {(releaseStage?.state === 'pass' || deployStage?.state === 'pass') && pkg && (
                    <Panel title="Deployment">
                      {deployStage?.state === 'pass' ? (
                        <Callout tone="ok" title="Deployed">{deployStage.detail}</Callout>
                      ) : (
                        <div className="stack">
                          <p className="small">Package <PackageLink id={pkg} /> is released. Deploying makes the twin run it; earlier executions keep referring to their own packages.</p>
                          <label className="vts-field">
                            <span>Reason (optional)</span>
                            <input className="vts-input" value={deployReason} onChange={(e) => setDeployReason(e.target.value)} />
                          </label>
                          <Button variant="primary" icon={<Rocket size={14} />} loading={deploy.isPending} onClick={() => deploy.mutate({ twinId: c.twinId, packageId: pkg, reason: deployReason })}>
                            Deploy {pkg}
                          </Button>
                        </div>
                      )}
                    </Panel>
                  )}
                </div>
                <Panel
                  title="Release pipeline"
                  subtitle={p ? <>Computed <TimeStamp value={p.computedAt} relative /> from stored evidence</> : undefined}
                >
                  <QueryState query={pipeline}>
                    {(pl) => (
                      <div className="stack">
                        <PipelineView pipeline={pl} onRun={(s) => runStage.mutate(s)} running={runStage.isPending ? (runStage.variables ?? null) : null} disabled={!open} />
                        {pl.releaseReady ? (
                          <Callout tone="ok" title="Ready to release">Every mandatory stage passed for the exact candidate artefacts.</Callout>
                        ) : open ? (
                          <Callout tone="neutral" title="Release blocked">Mandatory evidence missing or failed: {pl.blocking?.join(', ')}.</Callout>
                        ) : null}
                        {pl.candidateBindings && (
                          <details>
                            <summary className="small">Candidate artefacts</summary>
                            <ul className="vts-list">
                              {pl.candidateBindings.map((b) => (
                                <li key={b.role} className="small row-between">
                                  <span>{humanize(b.role)}</span>
                                  <RefLink refId={b.ref} />
                                </li>
                              ))}
                            </ul>
                          </details>
                        )}
                      </div>
                    )}
                  </QueryState>
                </Panel>
              </div>
              <AddArtifactDialog change={c} open={addOpen} onOpenChange={setAddOpen} />
              <Dialog
                open={abandonOpen}
                onOpenChange={setAbandonOpen}
                title={`Abandon ${c.id}`}
                description="Open drafts are rejected and kept for the record; nothing is deleted."
                footer={
                  <>
                    <Button onClick={() => setAbandonOpen(false)}>Cancel</Button>
                    <Button variant="danger" disabled={!reason.trim()} loading={abandon.isPending} onClick={() => abandon.mutate(reason.trim(), { onSuccess: () => setAbandonOpen(false) })}>Abandon change</Button>
                  </>
                }
              >
                <label className="vts-field">
                  <span>Reason (required)</span>
                  <textarea className="vts-textarea" value={reason} onChange={(e) => setReason(e.target.value)} />
                </label>
              </Dialog>
            </div>
          );
        }}
      </QueryState>
    </div>
  );
}
