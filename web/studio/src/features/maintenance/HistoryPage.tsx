/**
 * Version history and comparison: pick an artefact and two versions to see the
 * structural (semantic) diff and the source diff; plus the version timeline.
 */
import { useSearchParams } from 'react-router-dom';
import { useArtifact, useArtifacts, useDiff } from '@/api/queries';
import { Callout, EmptyState, PageHeader, Panel, QueryState } from '@/design';
import { SourceDiff } from '@/editor/SourceDiff';
import { Crumbs, RefLink } from '@/features/common/links';
import { AuditTable } from '@/features/audit/EngineeringAuditPage';
import { LineagePanel } from '@/features/engineering/versionPanels';
import { StructuralChanges } from './ChangePage';
import { useTwinScope } from '@/app/twinScope';

export default function HistoryPage() {
  const [params, setParams] = useSearchParams();
  const scope = useTwinScope();
  // In a twin workspace with nothing chosen: the deployed ontology against its newest version.
  const deployed = scope?.twin.bindings.find((b) => b.role === 'ontology');
  const explicit = params.has('from') || params.has('to');
  const artifactId = ((params.get('to') || params.get('from')) ?? deployed?.ref ?? '').split('@')[0] ?? '';
  const artifacts = useArtifacts();
  const artifact = useArtifact(artifactId || undefined);
  const newest = artifact.data?.versions.reduce((a, v) => (v.version > a.version ? v : a), artifact.data.versions[0]!);
  const defaultFrom = deployed && newest && newest.ref !== deployed.ref ? deployed.ref : newest?.parentVersion ? `${artifactId}@${newest.parentVersion}` : '';
  const from = explicit ? params.get('from') ?? '' : defaultFrom;
  const to = explicit ? params.get('to') ?? '' : newest?.ref ?? '';
  const diff = useDiff(from || undefined, to || undefined);
  const set = (patch: Record<string, string>) => setParams({ ...Object.fromEntries(params), ...patch });
  const versions = artifact.data?.versions ?? [];
  return (
    <div className="vts-page">
      <PageHeader
        eyebrow={<Crumbs items={[{ label: 'Studio', to: '/studio' }, { label: 'Changes', to: '/studio/changes' }, { label: 'Version history' }]} />}
        title="Version history & comparison"
        actions={
          <>
            <select className="vts-select" value={artifactId} aria-label="Artefact" onChange={(e) => {
              const a = artifacts.data?.find((x) => x.id === e.target.value);
              const latest = a?.latest?.version ?? 1;
              setParams({ from: `${e.target.value}@${Math.max(1, (a?.latest?.parentVersion ?? latest - 1) || 1)}`, to: `${e.target.value}@${latest}` });
            }}>
              <option value="">Choose an artefact…</option>
              {artifacts.data?.map((a) => <option key={a.id} value={a.id}>{a.name}</option>)}
            </select>
            {versions.length > 0 && (
              <>
                <select className="vts-select" value={from} onChange={(e) => set({ from: e.target.value })} aria-label="From version">
                  {versions.map((v) => <option key={v.ref} value={v.ref}>from v{v.version} ({v.state})</option>)}
                </select>
                <select className="vts-select" value={to} onChange={(e) => set({ to: e.target.value })} aria-label="To version">
                  {versions.map((v) => <option key={v.ref} value={v.ref}>to v{v.version} ({v.state})</option>)}
                </select>
              </>
            )}
          </>
        }
      />
      {!from || !to ? (
        <EmptyState title="Choose an artefact and two versions" />
      ) : (
        <div className="grid-main-side">
          <div className="stack">
            <QueryState query={diff}>
              {(d) => (
                <div className="stack">
                  <Panel title={<span><RefLink refId={d.from.ref} kind={d.kind} /> → <RefLink refId={d.to.ref} kind={d.kind} /></span>} subtitle="Semantic / structural diff (theory elements; comments and whitespace ignored)">
                    {d.structural ? <StructuralChanges changes={d.structural.changes} /> : <p className="small muted">Models are compared as source.</p>}
                    {d.structural?.affectedSymbols && d.structural.affectedSymbols.length > 0 && (
                      <p className="small" style={{ marginTop: 8 }}>Symbols whose meaning may change: <span className="mono">{d.structural.affectedSymbols.join(', ')}</span></p>
                    )}
                  </Panel>
                  {d.affectedInterpretations.length > 0 && (
                    <Callout tone="warning" title="Interpretation dependencies affected">
                      <ul style={{ margin: 0, paddingLeft: 16 }}>
                        {d.affectedInterpretations.map((a) => (
                          <li key={a.ref}><RefLink refId={a.ref} kind="interpretation" />: {a.entries.length ? a.entries.join(', ') : 'no entry mentions an affected symbol'}</li>
                        ))}
                      </ul>
                    </Callout>
                  )}
                  <Panel title="Source diff">
                    <SourceDiff a={d.from.content} b={d.to.content} language={d.kind === 'pt_model' || d.kind === 'dt_model' ? 'xml' : 'ontology'} labelA={d.from.ref} labelB={d.to.ref} />
                  </Panel>
                </div>
              )}
            </QueryState>
          </div>
          <div className="stack">
            {artifactId && <Panel title="Lineage"><LineagePanel artifactId={artifactId} current={Number(to.split('@')[1])} /></Panel>}
            {artifactId && <Panel title="Engineering events" flush><AuditTable subject={artifactId} /></Panel>}
          </div>
        </div>
      )}
    </div>
  );
}
