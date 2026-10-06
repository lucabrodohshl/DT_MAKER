/**
 * Panels of an artefact version page: structure (theory elements), symbol cross
 * references, dependencies, ontology visualisation, interpretation evaluation,
 * and version lineage.
 */
import Dagre from '@dagrejs/dagre';
import { safeLayout } from '@/design/graphLayout';
import { Background, Controls, ReactFlow, type Edge, type Node } from '@xyflow/react';
import '@xyflow/react/dist/style.css';
import { useMutation } from '@tanstack/react-query';
import { Play } from 'lucide-react';
import { useMemo, useState } from 'react';
import { Link } from 'react-router-dom';
import { evaluateInterpretation, useArtifact, useSymbol } from '@/api/queries';
import type { InterpretationStructure, OntologyStructure, Span, VersionDetail } from '@/api/types';
import {
  Button,
  Callout,
  EmptyState,
  ErrorBlock,
  Formula,
  KeyValue,
  LifecycleBadge,
  Panel,
  QueryState,
  StatusBadge,
  TimeStamp,
  TruthBadge,
  displayUnit,
} from '@/design';
import { AssetLink, PackageLink, RefLink, versionRoute } from '@/features/common/links';

export function isOntology(s: VersionDetail['structure']): s is OntologyStructure {
  return !!s && 'axioms' in s;
}
export function isInterpretation(s: VersionDetail['structure']): s is InterpretationStructure {
  return !!s && 'entries' in s;
}

export function StructurePanel({ v, onReveal, onSymbol }: { v: VersionDetail; onReveal: (span: Span) => void; onSymbol: (name: string) => void }) {
  const [q, setQ] = useState('');
  const match = (s: string) => !q || s.toLowerCase().includes(q.toLowerCase());
  const s = v.structure;
  const RevealButton = ({ span, label }: { span: Span; label: string }) => (
    <button type="button" className="vts-btn vts-btn--ghost vts-btn--sm" onClick={() => onReveal(span)} aria-label={`Show ${label} in source (line ${span.line})`}>
      L{span.line}
    </button>
  );
  if (isOntology(s)) {
    return (
      <div className="stack">
        <input className="vts-input" placeholder="Filter symbols and axioms…" value={q} onChange={(e) => setQ(e.target.value)} aria-label="Filter structure" />
        {s.headerComment && <p className="small muted" style={{ whiteSpace: 'pre-line' }}>{s.headerComment}</p>}
        <Panel title={`Sorts (${s.sorts.length})`} subtitle="Value domains S" flush>
          <table className="vts-table">
            <caption className="sr-only">Sorts</caption>
            <tbody>
              {s.sorts.filter((x) => match(x.name)).map((x) => (
                <tr key={x.name}>
                  <td><button type="button" className="vts-btn vts-btn--ghost vts-btn--sm mono" onClick={() => onSymbol(x.name)}>{x.name}</button></td>
                  <td className="small muted">{x.comment}</td>
                  <td style={{ width: 50 }}><RevealButton span={x.span} label={x.name} /></td>
                </tr>
              ))}
            </tbody>
          </table>
        </Panel>
        <Panel title={`Functions (${s.functions.length})`} subtitle="Function symbols F (quantities, constants)" flush>
          <table className="vts-table">
            <caption className="sr-only">Functions</caption>
            <tbody>
              {s.functions.filter((x) => match(x.name)).map((x) => (
                <tr key={x.name}>
                  <td><button type="button" className="vts-btn vts-btn--ghost vts-btn--sm mono" onClick={() => onSymbol(x.name)}>{x.name}</button></td>
                  <td className="mono small">{x.signature}</td>
                  <td className="small muted">{x.comment}</td>
                  <td style={{ width: 50 }}><RevealButton span={x.span} label={x.name} /></td>
                </tr>
              ))}
            </tbody>
          </table>
        </Panel>
        <Panel title={`Relations (${s.relations.length})`} subtitle="Predicates R" flush>
          <table className="vts-table">
            <caption className="sr-only">Relations</caption>
            <tbody>
              {s.relations.filter((x) => match(x.name)).map((x) => (
                <tr key={x.name}>
                  <td><button type="button" className="vts-btn vts-btn--ghost vts-btn--sm mono" onClick={() => onSymbol(x.name)}>{x.name}</button></td>
                  <td className="mono small">{x.signature || '(proposition)'}</td>
                  <td className="small muted">{x.comment}</td>
                  <td style={{ width: 50 }}><RevealButton span={x.span} label={x.name} /></td>
                </tr>
              ))}
            </tbody>
          </table>
        </Panel>
        <Panel title={`Axioms (${s.axioms.length})`} subtitle="Δ — what is known to be true and invariant" flush>
          <table className="vts-table">
            <caption className="sr-only">Axioms</caption>
            <tbody>
              {s.axioms.filter((x) => match(x.id) || match(x.formula)).map((x) => (
                <tr key={x.id}>
                  <td className="mono small strong" style={{ whiteSpace: 'nowrap' }}>{x.id}</td>
                  <td>
                    <Formula>{x.formula}</Formula>
                    {x.comment && <div className="xsmall subtle">{x.comment}</div>}
                  </td>
                  <td style={{ width: 50 }}><RevealButton span={x.span} label={x.id} /></td>
                </tr>
              ))}
            </tbody>
          </table>
        </Panel>
      </div>
    );
  }
  if (isInterpretation(s)) {
    return (
      <div className="stack">
        <input className="vts-input" placeholder="Filter entries…" value={q} onChange={(e) => setQ(e.target.value)} aria-label="Filter entries" />
        {['Locations (state meanings)', 'Events (label meanings)'].map((title, i) => (
          <Panel key={title} title={title} flush>
            <table className="vts-table">
              <caption className="sr-only">{title}</caption>
              <tbody>
                {s.entries.filter((e) => e.isEvent === (i === 1) && (match(e.key) || match(e.formula))).map((e) => (
                  <tr key={e.key}>
                    <td className="mono small strong" style={{ whiteSpace: 'nowrap' }}>{e.key}</td>
                    <td>
                      <Formula>{e.formula}</Formula>
                      <div className="row-wrap" style={{ marginTop: 4 }}>
                        {e.symbols.map((sym) => (
                          <button key={sym} type="button" className="vts-tag" style={{ border: 0, cursor: 'pointer' }} onClick={() => onSymbol(sym)}>{sym}</button>
                        ))}
                      </div>
                    </td>
                    <td style={{ width: 50 }}><RevealButton span={e.span} label={e.key} /></td>
                  </tr>
                ))}
              </tbody>
            </table>
          </Panel>
        ))}
      </div>
    );
  }
  return <EmptyState compact title="No structural view">Models are shown as source; their structure is validated by the compiler.</EmptyState>;
}

export function SymbolPanel({ ontologyRef, name, onReveal }: { ontologyRef: string; name: string | null; onReveal?: (span: Span) => void }) {
  const q = useSymbol(ontologyRef, name ?? undefined);
  if (!name) return <EmptyState compact title="Select a symbol">Choose a symbol in the structure or Cmd/Ctrl-click it in the source.</EmptyState>;
  return (
    <QueryState query={q}>
      {(s) => (
        <div className="stack">
          <div className="row-between">
            <h3 className="mono">{s.declaration.name}</h3>
            {onReveal && <Button size="sm" onClick={() => onReveal(s.declaration.span)}>Go to definition</Button>}
          </div>
          <KeyValue
            compact
            items={[
              ['Kind', s.declaration.kind],
              ['Signature', <span key="s" className="mono">{s.declaration.signature || '—'}</span>],
              ['Documentation', s.declaration.comment || '—'],
              ['Ontology', <RefLink key="o" refId={s.ontologyRef} kind="ontology" />],
            ]}
          />
          <div>
            <span className="vts-label">Axioms using it ({s.axioms.length})</span>
            <ul className="vts-list">
              {s.axioms.map((a) => (
                <li key={a.id} className="stack-sm" style={{ gap: 2 }}>
                  <span className="mono small strong">{a.id}</span>
                  <Formula>{a.formula}</Formula>
                </li>
              ))}
              {s.axioms.length === 0 && <li className="small muted">None</li>}
            </ul>
          </div>
          <div>
            <span className="vts-label">Interpretations depending on it</span>
            {s.interpretations.length === 0 ? (
              <p className="small muted">No interpretation uses this symbol.</p>
            ) : (
              s.interpretations.map((i) => (
                <div key={i.ref} className="stack-sm" style={{ marginTop: 6 }}>
                  <span className="row-wrap small">
                    <RefLink refId={i.ref} kind="interpretation" /> <LifecycleBadge state={i.state} />
                  </span>
                  <ul className="vts-list">
                    {i.entries.map((e) => (
                      <li key={e.key} className="small">
                        <Link to={`${versionRoute('interpretation', i.ref)}?entry=${encodeURIComponent(e.key)}`} className="mono">{e.key}</Link>
                        <div><Formula>{e.formula}</Formula></div>
                      </li>
                    ))}
                  </ul>
                </div>
              ))
            )}
          </div>
          <div>
            <span className="vts-label">Observed by telemetry</span>
            {s.telemetryChannels.length === 0 ? (
              <p className="small muted">No telemetry channel is bound to this symbol.</p>
            ) : (
              <ul className="vts-list">
                {s.telemetryChannels.map((c) => (
                  <li key={c.id} className="small row-between">
                    <Link to={`/assets/${encodeURIComponent(c.assetId)}/telemetry?channel=${encodeURIComponent(c.id)}`}>{c.presentation?.label ?? c.name}</Link>
                    <span className="xsmall subtle">
                      {displayUnit(c.unit)} · on <AssetLink id={c.assetId} />
                    </span>
                  </li>
                ))}
              </ul>
            )}
          </div>
        </div>
      )}
    </QueryState>
  );
}

export function DependenciesPanel({ artifactId }: { artifactId: string }) {
  const a = useArtifact(artifactId);
  return (
    <QueryState query={a}>
      {(d) => (
        <div className="stack">
          {d.kind === 'ontology' && (
            <div>
              <span className="vts-label">Interpretations over this ontology</span>
              {d.interpretations.length === 0 ? (
                <p className="small muted">None.</p>
              ) : (
                <ul className="vts-list">
                  {d.interpretations.map((i) => (
                    <li key={i.ref} className="row-between small">
                      <span>
                        <RefLink refId={i.ref} kind="interpretation" /> {i.name}
                      </span>
                      <span className="row">
                        <span className="xsmall subtle">over {i.ontologyRef}</span>
                        <LifecycleBadge state={i.state} />
                      </span>
                    </li>
                  ))}
                </ul>
              )}
            </div>
          )}
          <div>
            <span className="vts-label">Deployed in</span>
            {d.deployedIn.length === 0 ? (
              <p className="small muted">No deployed twin uses this artefact.</p>
            ) : (
              <ul className="vts-list">
                {d.deployedIn.map((x) => (
                  <li key={`${x.twinId}-${x.role}`} className="small row-between">
                    <span>
                      <strong>{x.twinName}</strong> ({x.role}) uses <RefLink refId={x.ref} kind={d.kind} />
                    </span>
                    <span className="row">
                      <PackageLink id={x.packageId} />
                      <span className="mono xsmall">{x.deploymentId}</span>
                    </span>
                  </li>
                ))}
              </ul>
            )}
          </div>
        </div>
      )}
    </QueryState>
  );
}

/** Ontology visualisation: axioms connected to the symbols they constrain (secondary to the text). */
export function OntologyGraph({ structure, onSymbol }: { structure: OntologyStructure; onSymbol: (s: string) => void }) {
  const { nodes, edges } = useMemo(() => {
    const g = new Dagre.graphlib.Graph().setDefaultEdgeLabel(() => ({}));
    g.setGraph({ rankdir: 'LR', nodesep: 14, ranksep: 140 });
    const ns: Node[] = [];
    const es: Edge[] = [];
    const symbolKind = new Map<string, string>();
    structure.sorts.forEach((s) => symbolKind.set(s.name, 'sort'));
    structure.functions.forEach((s) => symbolKind.set(s.name, 'function'));
    structure.relations.forEach((s) => symbolKind.set(s.name, 'relation'));
    symbolKind.forEach((kind, name) => {
      ns.push({ id: `s:${name}`, position: { x: 0, y: 0 }, data: { label: name }, style: { fontSize: 11, fontFamily: 'var(--font-mono)', width: 170, borderRadius: kind === 'sort' ? 14 : 6, border: '1px solid var(--border-strong)', background: kind === 'relation' ? 'var(--formal-soft)' : kind === 'sort' ? 'var(--ok-soft)' : 'var(--surface)', color: 'var(--text)', padding: 4 } });
      g.setNode(`s:${name}`, { width: 170, height: 30 });
    });
    structure.functions.forEach((f) => {
      const sort = f.returnSort;
      if (symbolKind.get(sort) === 'sort') {
        es.push({ id: `t:${f.name}`, source: `s:${f.name}`, target: `s:${sort}`, style: { stroke: 'var(--ok)', strokeDasharray: '3 3' } });
        g.setEdge(`s:${f.name}`, `s:${sort}`);
      }
    });
    structure.axioms.forEach((a) => {
      ns.push({ id: `a:${a.id}`, position: { x: 0, y: 0 }, data: { label: a.id }, style: { fontSize: 11, width: 150, border: '1px solid var(--formal-border)', background: 'var(--surface)', color: 'var(--text)', padding: 4 } });
      g.setNode(`a:${a.id}`, { width: 150, height: 30 });
      a.symbols.forEach((sym) => {
        if (!symbolKind.has(sym)) return;
        es.push({ id: `e:${a.id}:${sym}`, source: `a:${a.id}`, target: `s:${sym}`, style: { stroke: 'var(--border-strong)' } });
        g.setEdge(`a:${a.id}`, `s:${sym}`);
      });
    });
    safeLayout(g);
    return { nodes: ns.map((n) => ({ ...n, position: { x: g.node(n.id).x - 80, y: g.node(n.id).y - 15 } })), edges: es };
  }, [structure]);
  return (
    <div className="stack-sm">
      <p className="xsmall subtle">Axioms (left) linked to the symbols they constrain; dashed: function result sorts. The text is the authoritative representation.</p>
      <div style={{ height: 560, border: '1px solid var(--border)', borderRadius: 'var(--radius)' }}>
        <ReactFlow nodes={nodes} edges={edges} fitView nodesDraggable={false} nodesConnectable={false} proOptions={{ hideAttribution: true }}
          onNodeClick={(_e, n) => n.id.startsWith('s:') && onSymbol(n.id.slice(2))} aria-label="Ontology structure graph">
          <Background gap={20} color="var(--divider)" />
          <Controls showInteractive={false} />
        </ReactFlow>
      </div>
    </div>
  );
}

export function EvaluatePanel({ v }: { v: VersionDetail }) {
  const symbols = useMemo(() => {
    const s = new Set<string>();
    if (isInterpretation(v.structure)) v.structure.entries.forEach((e) => e.symbols.forEach((x) => s.add(x)));
    return [...s].sort();
  }, [v.structure]);
  const [obs, setObs] = useState<Record<string, string>>({});
  const run = useMutation({
    mutationFn: () => evaluateInterpretation(v.ref, { observations: Object.fromEntries(Object.entries(obs).filter(([, x]) => x.trim() !== '')) }),
  });
  return (
    <div className="stack">
      <Callout tone="info">
        Enter observed values (exact decimals, or true/false for relations). The backend decides each formula with Z3 under the
        ontology axioms: true, false, or unknown when the observations do not decide it.
      </Callout>
      <div className="grid-3" style={{ gridTemplateColumns: 'repeat(auto-fill, minmax(220px, 1fr))' }}>
        {symbols.map((s) => (
          <label key={s} className="vts-field">
            <span className="mono small">{s}</span>
            <input className="vts-input" value={obs[s] ?? ''} onChange={(e) => setObs((o) => ({ ...o, [s]: e.target.value }))} placeholder="unobserved" />
          </label>
        ))}
      </div>
      <div>
        <Button variant="primary" icon={<Play size={14} />} loading={run.isPending} onClick={() => run.mutate()}>
          Evaluate
        </Button>
      </div>
      {run.isError && <ErrorBlock error={run.error} compact />}
      {run.data && (
        <div className="stack-sm">
          {run.data.observationsConsistent === 'false' && <Callout tone="critical">These observations contradict the ontology axioms.</Callout>}
          <table className="vts-table">
            <caption className="sr-only">Evaluation</caption>
            <thead>
              <tr>
                <th scope="col">Entry</th>
                <th scope="col">Truth</th>
                <th scope="col">Unobserved symbols</th>
              </tr>
            </thead>
            <tbody>
              {run.data.entries.map((e) => (
                <tr key={e.key}>
                  <td className="mono small">{e.key}</td>
                  <td><TruthBadge truth={e.truth} /></td>
                  <td className="mono xsmall">{e.unobserved.join(', ') || '—'}</td>
                </tr>
              ))}
            </tbody>
          </table>
          <span className="xsmall subtle">Evaluated by {run.data.checker}</span>
        </div>
      )}
    </div>
  );
}

export function LineagePanel({ artifactId, current }: { artifactId: string; current: number }) {
  const a = useArtifact(artifactId);
  return (
    <QueryState query={a}>
      {(d) => (
        <ol className="vts-chain" style={{ listStyle: 'none', margin: 0, padding: 0 }}>
          {d.versions.map((x) => (
            <li key={x.version} className="vts-chain__item">
              <span className="vts-chain__marker" aria-hidden="true" style={x.version === current ? { background: 'var(--accent)' } : undefined} />
              <div className="stack-sm" style={{ gap: 2 }}>
                <span className="row-wrap">
                  <Link to={versionRoute(d.kind, x.ref)} className="strong">v{x.version}</Link>
                  <LifecycleBadge state={x.state} />
                  {x.parentVersion && <span className="xsmall subtle">from v{x.parentVersion}</span>}
                  {x.version === current && <StatusBadge tone="info" label="Viewing" />}
                </span>
                <span className="small">{x.changeDescription || <span className="muted">No description</span>}</span>
                <span className="xsmall subtle">
                  created <TimeStamp value={x.createdAt} /> by {x.createdBy}
                  {x.publishedAt && <> · published <TimeStamp value={x.publishedAt} /> by {x.publishedBy}</>}
                </span>
              </div>
            </li>
          ))}
        </ol>
      )}
    </QueryState>
  );
}
