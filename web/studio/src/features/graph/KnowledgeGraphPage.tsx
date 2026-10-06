/**
 * Asset knowledge graph: neighbourhood-based exploration (never the whole estate
 * at once), with relationship/asset-type filters, search, expansion of frontier
 * nodes, a details panel and a list alternative. This is the operational asset
 * graph — not the formal ontology.
 */
import Dagre from '@dagrejs/dagre';
import { safeLayout } from '@/design/graphLayout';
import { Background, Controls, MarkerType, MiniMap, Position, ReactFlow, ReactFlowProvider, useReactFlow, type Edge, type Node } from '@xyflow/react';
import '@xyflow/react/dist/style.css';
import { ArrowRight, Maximize2, Plus } from 'lucide-react';
import { useEffect, useMemo, useState } from 'react';
import { Link, useSearchParams } from 'react-router-dom';
import { useAsset, useAssets, useGraphFacets, useNeighborhood } from '@/api/queries';
import type { Asset, Relationship } from '@/api/types';
import { Button, Callout, EmptyState, KeyValue, PageHeader, Panel, QueryState, Segmented } from '@/design';
import { Crumbs } from '@/features/common/links';
import { useTwinScope } from '@/app/twinScope';

function GraphCanvas({ nodes, edges, focus, selected, onSelect, frontier, onExpand }: {
  nodes: Asset[]; edges: Relationship[]; focus: string; selected: string | null; onSelect: (id: string) => void; frontier: string[]; onExpand: (id: string) => void;
}) {
  const flow = useReactFlow();
  const { ns, es } = useMemo(() => {
    const g = new Dagre.graphlib.Graph({ multigraph: true }).setDefaultEdgeLabel(() => ({}));
    // Left-to-right keeps wide sibling sets (a drone's sub-systems) stacked and readable.
    g.setGraph({ rankdir: 'LR', nodesep: 22, ranksep: 110 });
    nodes.forEach((n) => g.setNode(n.id, { width: 180, height: 50 }));
    edges.forEach((e, i) => g.setEdge(e.sourceId, e.targetId, {}, `${i}`));
    safeLayout(g);
    const ns: Node[] = nodes.map((n) => {
      const isFocus = n.id === focus;
      const isSel = n.id === selected;
      return {
        id: n.id,
        position: { x: g.node(n.id).x - 90, y: g.node(n.id).y - 25 },
        sourcePosition: Position.Right,
        targetPosition: Position.Left,
        data: {
          label: (
            <div style={{ textAlign: 'left' }}>
              <div style={{ fontWeight: 600, fontSize: 12 }}>{n.name}{frontier.includes(n.id) ? ' ＋' : ''}</div>
              <div style={{ fontSize: 10, opacity: 0.7 }}>{n.type}{n.twinId ? ' · twin' : ''}</div>
            </div>
          ),
        },
        style: {
          width: 180, borderRadius: 8, padding: '4px 8px', color: 'var(--text)',
          border: `${isFocus || isSel ? 2 : 1}px solid ${isSel ? 'var(--focus)' : isFocus ? 'var(--accent)' : 'var(--border-strong)'}`,
          background: n.twinId ? 'var(--accent-soft)' : 'var(--surface)',
        },
      };
    });
    const es: Edge[] = edges.map((e, i) => ({
      id: `${e.sourceId}-${e.type}-${e.targetId}-${i}`,
      source: e.sourceId,
      target: e.targetId,
      label: e.type,
      labelStyle: { fontSize: 10, fill: 'var(--text-muted)' },
      labelBgStyle: { fill: 'var(--surface)' },
      style: { stroke: e.type === 'contains' ? 'var(--border-strong)' : 'var(--info)', strokeDasharray: e.type === 'contains' ? '4 3' : undefined },
      markerEnd: { type: MarkerType.ArrowClosed, color: e.type === 'contains' ? 'var(--border-strong)' : 'var(--info)' },
    }));
    return { ns, es };
  }, [nodes, edges, focus, selected, frontier]);
  // Fit the neighbourhood when that stays readable; otherwise centre on the focus asset at a
  // readable zoom (the rest is reachable by panning, zooming out, or the List view).
  const focusPos = ns.find((n) => n.id === focus)?.position;
  const fx = focusPos?.x;
  const fy = focusPos?.y;
  useEffect(() => {
    let check: ReturnType<typeof setTimeout> | undefined;
    const fit = setTimeout(() => {
      void flow.fitView({ padding: 0.12, maxZoom: 1.25 });
      check = setTimeout(() => {
        if (flow.getZoom() < 0.75 && fx !== undefined && fy !== undefined) void flow.setCenter(fx + 90, fy + 25, { zoom: 0.85 });
      }, 60);
    }, 30);
    return () => {
      clearTimeout(fit);
      clearTimeout(check);
    };
  }, [ns.length, flow, fx, fy]);
  return (
    <div style={{ height: 600, border: '1px solid var(--border)', borderRadius: 'var(--radius)', background: 'var(--bg)' }}>
      <ReactFlow nodes={ns} edges={es} nodesDraggable={false} nodesConnectable={false} onNodeClick={(_e, n) => onSelect(n.id)}
        onNodeDoubleClick={(_e, n) => onExpand(n.id)} minZoom={0.2} proOptions={{ hideAttribution: true }} aria-label="Asset knowledge graph">
        <Background gap={20} color="var(--divider)" />
        {ns.length > 25 && (
          <MiniMap pannable zoomable ariaLabel="Graph overview" bgColor="var(--surface)" nodeColor="var(--border-strong)" maskColor="color-mix(in srgb, var(--text) 8%, transparent)" style={{ background: 'var(--surface)', border: '1px solid var(--border)' }} />
        )}
        <Controls showInteractive={false} />
      </ReactFlow>
    </div>
  );
}

function Details({ id, onFocus }: { id: string; onFocus: (id: string) => void }) {
  const a = useAsset(id);
  return (
    <QueryState query={a} compact>
      {(d) => (
        <div className="stack">
          <h3>{d.name}</h3>
          <KeyValue compact items={[['Type', d.type], ['Id', <span key="i" className="mono small">{d.id}</span>], ['Part of', d.ancestors.map((x) => x.name).join(' › ') || '—'], ['Twin', d.twin?.name ?? '—']]} />
          {d.description && <p className="small muted">{d.description}</p>}
          <div className="row-wrap">
            <Link className="vts-btn vts-btn--sm vts-btn--primary" to={`/assets/${encodeURIComponent(d.id)}`}>Open asset <ArrowRight size={13} /></Link>
            <Button size="sm" onClick={() => onFocus(d.id)}>Centre graph here</Button>
            <Link className="vts-btn vts-btn--sm" to={`/assets/${encodeURIComponent(d.id)}/telemetry`}>Telemetry</Link>
          </div>
        </div>
      )}
    </QueryState>
  );
}

export default function KnowledgeGraphPage() {
  const [params, setParams] = useSearchParams();
  const scope = useTwinScope();
  const facets = useGraphFacets();
  const roots = useAssets({ parent: '', limit: 50 });
  const focus = params.get('focus') ?? scope?.asset.id ?? roots.data?.items[0]?.id ?? '';
  const depth = Number(params.get('depth') ?? 2);
  const types = (params.get('types') ?? '').split(',').filter(Boolean);
  const assetType = params.get('assetType') ?? '';
  const [selected, setSelected] = useState<string | null>(null);
  const [view, setView] = useState<'graph' | 'list'>('graph');
  const [extra, setExtra] = useState<string[]>([]);
  const [search, setSearch] = useState('');
  const hits = useAssets({ q: search || undefined, limit: 8 });
  const n = useNeighborhood(focus || undefined, depth, types);
  const expansions = useNeighborhoodsMerged(extra, types);
  const set = (patch: Record<string, string>) => {
    const p = new URLSearchParams(params);
    Object.entries(patch).forEach(([k, v]) => (v ? p.set(k, v) : p.delete(k)));
    setParams(p, { replace: true });
  };
  const merged = useMemo(() => {
    const nodes = new Map<string, Asset>();
    const edges = new Map<string, Relationship>();
    for (const part of [n.data, ...expansions]) {
      part?.nodes.forEach((x) => nodes.set(x.id, x));
      part?.edges.forEach((e) => edges.set(`${e.sourceId}|${e.type}|${e.targetId}`, e));
    }
    let list = [...nodes.values()];
    if (assetType) list = list.filter((x) => x.type === assetType || x.id === focus);
    const ids = new Set(list.map((x) => x.id));
    return { nodes: list, edges: [...edges.values()].filter((e) => ids.has(e.sourceId) && ids.has(e.targetId)), frontier: n.data?.frontier ?? [], truncated: n.data?.truncated ?? false };
  }, [n.data, expansions, assetType, focus]);

  return (
    <div className="vts-page">
      <PageHeader
        eyebrow={<Crumbs items={[{ label: 'Assets', to: '/assets' }, { label: 'Knowledge graph' }]} />}
        title="Knowledge graph"
        meta={<span>Asset instances and their operational relationships, explored around a focus. (The formal ontology is under Engineering.)</span>}
      />
      <div className="grid-main-side" style={{ gridTemplateColumns: 'minmax(0, 1fr) 340px' }}>
        <Panel
          title="Neighbourhood"
          subtitle={`${merged.nodes.length} assets, ${merged.edges.length} relationships${merged.truncated ? ' (truncated — refine filters)' : ''}`}
          actions={
            <>
              <label className="row small">Depth
                <select className="vts-select" value={depth} onChange={(e) => set({ depth: e.target.value })}>{[1, 2, 3, 4].map((d) => <option key={d}>{d}</option>)}</select>
              </label>
              <Segmented label="View" value={view} onChange={setView} options={[{ id: 'graph', label: 'Graph' }, { id: 'list', label: 'List' }]} />
            </>
          }
        >
          {!focus ? (
            <EmptyState title="No assets" />
          ) : view === 'graph' ? (
            <ReactFlowProvider>
              <GraphCanvas nodes={merged.nodes} edges={merged.edges} focus={focus} selected={selected} onSelect={setSelected} frontier={merged.frontier} onExpand={(id) => setExtra((x) => (x.includes(id) ? x : [...x, id]))} />
              <p className="xsmall subtle" style={{ marginTop: 6 }}>Click to select · double-click a node marked ＋ to expand its neighbourhood · dashed: containment</p>
            </ReactFlowProvider>
          ) : (
            <table className="vts-table">
              <caption className="sr-only">Relationships in view</caption>
              <thead><tr><th scope="col">From</th><th scope="col">Relationship</th><th scope="col">To</th></tr></thead>
              <tbody>
                {merged.edges.map((e, i) => {
                  const s = merged.nodes.find((x) => x.id === e.sourceId);
                  const t = merged.nodes.find((x) => x.id === e.targetId);
                  return (
                    <tr key={i}>
                      <td><Link to={`/assets/${encodeURIComponent(e.sourceId)}`}>{s?.name ?? e.sourceId}</Link></td>
                      <td><span className="vts-tag">{e.type}</span></td>
                      <td><Link to={`/assets/${encodeURIComponent(e.targetId)}`}>{t?.name ?? e.targetId}</Link></td>
                    </tr>
                  );
                })}
              </tbody>
            </table>
          )}
        </Panel>
        <div className="stack">
          <Panel title="Focus & search">
            <div className="stack">
              <input className="vts-input" placeholder="Find an asset…" value={search} onChange={(e) => setSearch(e.target.value)} aria-label="Find an asset" />
              {search && (
                <ul className="vts-list">
                  {hits.data?.items.map((h) => (
                    <li key={h.id}><button type="button" className="vts-link-row" style={{ border: 0, background: 'none', cursor: 'pointer', width: '100%' }} onClick={() => { set({ focus: h.id }); setExtra([]); setSearch(''); }}>{h.name} <span className="xsmall subtle">{h.type}</span></button></li>
                  ))}
                </ul>
              )}
              <Button size="sm" icon={<Maximize2 size={13} />} onClick={() => setExtra([])} disabled={extra.length === 0}>Reset expansions</Button>
            </div>
          </Panel>
          <Panel title="Filters">
            <QueryState query={facets} compact>
              {(f) => (
                <div className="stack">
                  <fieldset style={{ border: 0, padding: 0, margin: 0 }}>
                    <legend className="vts-label">Relationship types</legend>
                    {f.relationshipTypes.map((r) => (
                      <label key={r.type} className="row small">
                        <input type="checkbox" checked={types.length === 0 || types.includes(r.type)} onChange={(e) => {
                          const all = f.relationshipTypes.map((x) => x.type);
                          const current = types.length === 0 ? all : types;
                          const next = e.target.checked ? [...current, r.type] : current.filter((t) => t !== r.type);
                          set({ types: next.length === all.length ? '' : next.join(',') });
                        }} />
                        {r.type}{r.count !== null ? ` (${r.count})` : ' (hierarchy)'}
                      </label>
                    ))}
                  </fieldset>
                  <label className="vts-field">
                    <span>Asset type</span>
                    <select className="vts-select" value={assetType} onChange={(e) => set({ assetType: e.target.value })}>
                      <option value="">All types</option>
                      {f.assetTypes.map((t) => <option key={t.type} value={t.type}>{t.type} ({t.count})</option>)}
                    </select>
                  </label>
                </div>
              )}
            </QueryState>
          </Panel>
          <Panel title="Selection">
            {selected ? <Details id={selected} onFocus={(id) => { set({ focus: id }); setExtra([]); }} /> : <p className="small muted">Select a node to see details.</p>}
          </Panel>
          {merged.frontier.length > 0 && (
            <Callout tone="neutral" title="More to explore">
              {merged.frontier.length} asset(s) have neighbours outside this view.
              <div className="row-wrap" style={{ marginTop: 6 }}>
                {merged.frontier.slice(0, 6).map((f) => (
                  <Button key={f} size="sm" icon={<Plus size={12} />} onClick={() => setExtra((x) => (x.includes(f) ? x : [...x, f]))}>{merged.nodes.find((m) => m.id === f)?.name ?? f}</Button>
                ))}
              </div>
            </Callout>
          )}
        </div>
      </div>
    </div>
  );
}

/** Neighbourhoods (depth 1) of explicitly expanded nodes. */
function useNeighborhoodsMerged(ids: string[], types: string[]) {
  const a = useNeighborhood(ids[0], 1, types);
  const b = useNeighborhood(ids[1], 1, types);
  const c = useNeighborhood(ids[2], 1, types);
  const d = useNeighborhood(ids[3], 1, types);
  return [a.data, b.data, c.data, d.data].filter((x): x is NonNullable<typeof x> => !!x);
}
