/**
 * Impact analysis: "If I change this artefact version, what could be affected?"
 * Classifications are computed by the backend from the dependency graph and the
 * stored evidence (including Theorem 3 preservation); the UI renders them.
 */
import Dagre from '@dagrejs/dagre';
import { safeLayout } from '@/design/graphLayout';
import { Background, Controls, MarkerType, ReactFlow, type Edge, type Node } from '@xyflow/react';
import '@xyflow/react/dist/style.css';
import { useMemo, useState } from 'react';
import { Link, useSearchParams } from 'react-router-dom';
import { useArtifacts, useImpact } from '@/api/queries';
import type { ImpactReport } from '@/api/types';
import { Callout, EmptyState, ImpactBadge, PageHeader, Panel, QueryState, Segmented, TimeStamp } from '@/design';
import { Crumbs, EvidenceLink, RefLink, LearnMore } from '@/features/common/links';
import { useTwinScope } from '@/app/twinScope';

const CLASS_COLOR: Record<string, string> = {
  changed: 'var(--info)',
  definitely_stale: 'var(--crit)',
  requires_verification: 'var(--warn)',
  preserved: 'var(--formal)',
  potentially_affected: 'var(--neutral)',
  unaffected: 'var(--ok)',
};

function ImpactGraph({ report }: { report: ImpactReport }) {
  const { nodes, edges } = useMemo(() => {
    const g = new Dagre.graphlib.Graph().setDefaultEdgeLabel(() => ({}));
    g.setGraph({ rankdir: 'LR', nodesep: 24, ranksep: 90 });
    report.nodes.forEach((n) => g.setNode(n.id, { width: 200, height: 52 }));
    report.edges.forEach((e) => g.setEdge(e.from, e.to));
    safeLayout(g);
    const ns: Node[] = report.nodes.map((n) => ({
      id: n.id,
      position: { x: g.node(n.id).x - 100, y: g.node(n.id).y - 26 },
      data: { label: <div style={{ textAlign: 'left' }}><div style={{ fontSize: 10, opacity: 0.7, textTransform: 'uppercase' }}>{n.type}</div><div style={{ fontSize: 12, fontWeight: 600 }}>{n.label}</div></div> },
      style: { width: 200, borderRadius: 8, border: `2px solid ${CLASS_COLOR[n.classification]}`, background: 'var(--surface)', color: 'var(--text)', padding: '4px 8px' },
    }));
    const es: Edge[] = report.edges.map((e, i) => ({
      id: `${e.from}-${e.to}-${i}`,
      source: e.from,
      target: e.to,
      label: e.label,
      labelStyle: { fontSize: 10, fill: 'var(--text-muted)' },
      labelBgStyle: { fill: 'var(--surface)' },
      markerEnd: { type: MarkerType.ArrowClosed },
      style: { stroke: 'var(--border-strong)' },
    }));
    return { nodes: ns, edges: es };
  }, [report]);
  return (
    <div style={{ height: 460, border: '1px solid var(--border)', borderRadius: 'var(--radius)' }}>
      <ReactFlow nodes={nodes} edges={edges} fitView nodesDraggable={false} nodesConnectable={false} proOptions={{ hideAttribution: true }} aria-label="Dependency graph">
        <Background gap={20} color="var(--divider)" />
        <Controls showInteractive={false} />
      </ReactFlow>
    </div>
  );
}

export default function ImpactPage() {
  const [params, setParams] = useSearchParams();
  const scope = useTwinScope();
  const artifacts = useArtifacts();
  // In a twin workspace, start from an open draft of one of the twin's artefacts (what is changing).
  const bound = new Set(scope?.twin.bindings.map((b) => b.artifactId) ?? []);
  const openDraft = (artifacts.data ?? []).find((a) => bound.has(a.id) && a.open)?.open?.ref;
  const ref = params.get('ref') ?? openDraft ?? '';
  const impact = useImpact(ref || undefined);
  const [view, setView] = useState<'list' | 'graph'>('list');
  const options = (artifacts.data ?? []).flatMap((a) => [a.open, a.published].filter((v): v is NonNullable<typeof v> => !!v).map((v) => ({ ref: v.ref, label: `${a.name} — ${v.ref} (${v.state})` })));
  return (
    <div className="vts-page">
      <PageHeader
        eyebrow={<Crumbs items={[{ label: 'Studio', to: '/studio' }, { label: 'Changes', to: '/studio/changes' }, { label: 'Impact analysis' }]} />}
        title="Impact analysis"
        meta={<><span>What depends on an artefact version, and what must be re-verified if it replaces the deployed one</span><LearnMore page="impact-and-staleness.html#impact-analysis" /></>}
        actions={
          <select className="vts-select" value={ref} onChange={(e) => setParams({ ref: e.target.value })} aria-label="Artefact version">
            <option value="">Choose an artefact version…</option>
            {options.map((o) => <option key={o.ref} value={o.ref}>{o.label}</option>)}
          </select>
        }
      />
      {!ref ? (
        <EmptyState title={scope ? 'Nothing of this twin is changing' : 'Choose an artefact version'}>
          {scope ? 'No draft of its ontology, interpretations or models is open. ' : ''}Select a draft (or any version) above to see which interpretations, evidence, packages, deployments and twins depend on it.
        </EmptyState>
      ) : (
        <QueryState query={impact}>
          {(r) => (
            <div className="stack">
              <div className="row-wrap small">
                Analysed <RefLink refId={r.subject.ref} kind={r.subject.kind} /> against current deployments · <TimeStamp value={r.analyzedAt} relative />
              </div>
              {r.requiredActions.length > 0 ? (
                <Callout tone="warning" title="Required next steps">
                  <ul style={{ margin: 0, paddingLeft: 16 }}>{r.requiredActions.map((a) => <li key={a.action}>{a.action}</li>)}</ul>
                </Callout>
              ) : (
                <Callout tone="ok" title="No re-verification required">No deployed artefact needs new evidence because of this version.</Callout>
              )}
              <Panel title="Dependants" actions={<Segmented label="View" value={view} onChange={setView} options={[{ id: 'list', label: 'List' }, { id: 'graph', label: 'Graph' }]} />} flush={view === 'list'}>
                {view === 'graph' ? (
                  <ImpactGraph report={r} />
                ) : (
                  <table className="vts-table">
                    <caption className="sr-only">Impact classification</caption>
                    <thead><tr><th scope="col">Element</th><th scope="col">Classification</th><th scope="col">Reason (from evidence)</th></tr></thead>
                    <tbody>
                      {r.nodes.map((n) => (
                        <tr key={n.id}>
                          <td>
                            <div className="xsmall subtle">{n.type}</div>
                            <strong className="small">{n.label}</strong>
                            {n.entries && n.entries.length > 0 && <div className="mono xsmall">{n.entries.join(', ')}</div>}
                          </td>
                          <td><ImpactBadge classification={n.classification} /></td>
                          <td className="small">
                            {n.reason}
                            <div className="row-wrap xsmall">
                              {n.evidenceId && <span>evidence <EvidenceLink id={n.evidenceId} /></span>}
                              {n.refinementEvidenceId && <span>refinement <EvidenceLink id={n.refinementEvidenceId} kind="refinement" /></span>}
                              {n.packageId && <Link to={`/studio/packages/${n.packageId}`}>{n.packageId}</Link>}
                            </div>
                          </td>
                        </tr>
                      ))}
                    </tbody>
                  </table>
                )}
              </Panel>
              <Panel title="Legend">
                <ul className="vts-list">
                  {Object.entries(r.legend).map(([k, v]) => (
                    <li key={k} className="row small"><ImpactBadge classification={k as keyof typeof r.legend} /> {v}</li>
                  ))}
                </ul>
              </Panel>
            </div>
          )}
        </QueryState>
      )}
    </div>
  );
}
