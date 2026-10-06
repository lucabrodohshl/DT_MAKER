/**
 * Interactive timed-automaton diagram (view mode).
 *
 * Locations are nodes (name, initial marker, invariant); transitions are edges labelled with
 * event, guard and resets; self-loops and parallel transitions are drawn distinctly.
 * Pan, zoom, drag, fit, reset layout, minimap, search, centre on the current state,
 * neighbourhood / full model, details on/off. Dragging changes only presentation
 * coordinates (kept per viewer); it never edits the model. Live highlighting (current
 * location, enabled now / later, last taken) is supplied by the caller from runtime state.
 */
import {
  Background,
  BaseEdge,
  Controls,
  EdgeLabelRenderer,
  Handle,
  MarkerType,
  MiniMap,
  Position,
  ReactFlow,
  ReactFlowProvider,
  useInternalNode,
  useReactFlow,
  type Edge,
  type EdgeProps,
  type Node,
  type NodeChange,
  type NodeProps,
} from '@xyflow/react';
import '@xyflow/react/dist/style.css';
import Dagre from '@dagrejs/dagre';
import { Crosshair, Eye, EyeOff, LocateFixed, Maximize2, RotateCcw, Search } from 'lucide-react';
import { useEffect, useMemo, useState } from 'react';
import clsx from 'clsx';
import { Button, Segmented } from '@/design';
import { useDisclosure } from '@/app/disclosure';
import { safeLayout } from '@/design/graphLayout';
import type { TaEdge, TaGraph, TaLayout } from './taModel';
import './ta.css';

export type TaSelection = { kind: 'location'; id: string } | { kind: 'edge'; id: string };

export interface TaOverlay {
  current?: Set<string>;
  enabledNow?: Set<string>;
  enabledLater?: Set<string>;
  recent?: Set<string>;
  /** Edges to emphasise (e.g. an alignment correspondence). */
  emphasis?: Set<string>;
}

interface Props {
  graph: TaGraph;
  /** Stable key of the model version (for the per-viewer layout). */
  layoutKey: string;
  layout?: TaLayout;
  overlay?: TaOverlay;
  selection: TaSelection | null;
  onSelect: (s: TaSelection | null) => void;
  height?: number;
}

const NODE_W = 170;
const NODE_H = 58;
const storageKey = (k: string) => `vts.ta.layout.${k}`;

function readOverrides(key: string): TaLayout {
  try {
    return JSON.parse(localStorage.getItem(storageKey(key)) ?? '{}') as TaLayout;
  } catch {
    return {};
  }
}

// ------------------------------------------------------------------ nodes
type LocData = { label: string; initial: boolean; invariant: string; details: boolean; current: boolean; hit: boolean; selected: boolean; dim: boolean };

function LocationNode({ data }: NodeProps<Node<LocData>>) {
  return (
    <div className={clsx('ta-loc', data.current && 'is-current', data.selected && 'is-selected', data.hit && 'is-hit', data.dim && 'is-dim', data.initial && 'is-initial')}>
      <Handle type="target" position={Position.Top} className="ta-handle" />
      {data.initial && <span className="ta-loc__init" title="Initial location">initial</span>}
      <div className="ta-loc__name">{data.current && <span className="ta-loc__dot" aria-label="current" />}{data.label}</div>
      {data.details && data.invariant && <div className="ta-loc__inv mono">inv: {data.invariant}</div>}
      <Handle type="source" position={Position.Bottom} className="ta-handle" />
    </div>
  );
}

// ------------------------------------------------------------------ edges
type EdgeData = { edge: TaEdge; offset: number; details: boolean; state: 'now' | 'later' | null; recent: boolean; selected: boolean; emphasis: boolean; dim: boolean };

/** Point where the segment from the centre of a w×h box to (tx,ty) leaves the box. */
function border(cx: number, cy: number, w: number, h: number, tx: number, ty: number) {
  const dx = tx - cx;
  const dy = ty - cy;
  if (dx === 0 && dy === 0) return { x: cx, y: cy };
  const sx = (w / 2) / Math.abs(dx || 1e-9);
  const sy = (h / 2) / Math.abs(dy || 1e-9);
  const s = Math.min(sx, sy);
  return { x: cx + dx * s, y: cy + dy * s };
}

function TransitionEdge({ id, source, target, data, markerEnd }: EdgeProps<Edge<EdgeData>>) {
  const s = useInternalNode(source);
  const t = useInternalNode(target);
  if (!s || !t || !data) return null;
  const sw = s.measured.width ?? NODE_W;
  const sh = s.measured.height ?? NODE_H;
  const tw = t.measured.width ?? NODE_W;
  const th = t.measured.height ?? NODE_H;
  const scx = s.internals.positionAbsolute.x + sw / 2;
  const scy = s.internals.positionAbsolute.y + sh / 2;
  const tcx = t.internals.positionAbsolute.x + tw / 2;
  const tcy = t.internals.positionAbsolute.y + th / 2;
  let path: string;
  let lx: number;
  let ly: number;
  if (source === target) {
    // Self-loop above the node; stacked loops grow outward.
    const top = s.internals.positionAbsolute.y;
    const r = 34 + data.offset * 22;
    path = `M ${scx - 22} ${top} C ${scx - 22 - r} ${top - 1.6 * r}, ${scx + 22 + r} ${top - 1.6 * r}, ${scx + 22} ${top}`;
    lx = scx;
    ly = top - 1.2 * r - 6;
  } else {
    // Curved edge; parallel/opposite edges get distinct curvature.
    const dx = tcx - scx;
    const dy = tcy - scy;
    const len = Math.hypot(dx, dy) || 1;
    const nx = -dy / len;
    const ny = dx / len;
    const bend = 26 + data.offset * 30;
    const mx = (scx + tcx) / 2 + nx * bend;
    const my = (scy + tcy) / 2 + ny * bend;
    const a = border(scx, scy, sw, sh, mx, my);
    const b = border(tcx, tcy, tw, th, mx, my);
    path = `M ${a.x} ${a.y} Q ${mx} ${my} ${b.x} ${b.y}`;
    lx = 0.25 * a.x + 0.5 * mx + 0.25 * b.x;
    ly = 0.25 * a.y + 0.5 * my + 0.25 * b.y;
  }
  const e = data.edge;
  const cls = clsx('ta-edge', data.state === 'now' && 'is-now', data.state === 'later' && 'is-later', data.recent && 'is-recent', data.selected && 'is-selected', data.emphasis && 'is-emph', data.dim && 'is-dim');
  return (
    <>
      <BaseEdge id={id} path={path} markerEnd={markerEnd} className={cls} interactionWidth={18} />
      <EdgeLabelRenderer>
        <div className={clsx('ta-elabel nodrag nopan', cls)} style={{ transform: `translate(-50%, -50%) translate(${lx}px, ${ly}px)` }} data-edge={id}>
          <span className="mono ta-elabel__ev">{e.label}</span>
          {data.details && e.guard && <span className="mono ta-elabel__g">[{e.guard}]</span>}
          {data.details && e.resets.length > 0 && <span className="mono ta-elabel__r">{e.resets.map((r) => `${r}:=0`).join(', ')}</span>}
        </div>
      </EdgeLabelRenderer>
    </>
  );
}

const nodeTypes = { loc: LocationNode };
const edgeTypes = { ta: TransitionEdge };

// ------------------------------------------------------------------ layout
function autoLayout(graph: TaGraph): TaLayout {
  const g = new Dagre.graphlib.Graph({ multigraph: true }).setDefaultEdgeLabel(() => ({}));
  g.setGraph({ rankdir: 'LR', nodesep: 70, ranksep: 150, marginx: 20, marginy: 20 });
  for (const l of graph.locations) g.setNode(l.id, { width: NODE_W, height: NODE_H });
  graph.edges.forEach((e) => g.setEdge(e.source, e.target, {}, e.id));
  safeLayout(g);
  return Object.fromEntries(graph.locations.map((l) => {
    const n = g.node(l.id) as unknown as { x: number; y: number };
    return [l.id, { x: n.x - NODE_W / 2, y: n.y - NODE_H / 2 }];
  }));
}

/** UPPAAL coordinates are compact; spread them so labels fit. */
function scaleLayout(l: TaLayout): TaLayout {
  return Object.fromEntries(Object.entries(l).map(([k, v]) => [k, { x: v.x * 1.7, y: v.y * 1.9 }]));
}

function Inner({ graph, layoutKey, layout, overlay = {}, selection, onSelect, height = 560 }: Props) {
  const flow = useReactFlow();
  const { engineering } = useDisclosure();
  const [details, setDetails] = useState(engineering);
  const [mode, setMode] = useState<'full' | 'neighbourhood'>(graph.locations.length > 18 ? 'neighbourhood' : 'full');
  const [query, setQuery] = useState('');
  const [overrides, setOverrides] = useState<TaLayout>(() => readOverrides(layoutKey));
  const base = useMemo(() => (layout ? scaleLayout(layout) : autoLayout(graph)), [graph, layout]);
  const current = useMemo(() => overlay.current ?? new Set<string>(), [overlay.current]);

  const focus = useMemo(() => {
    if (mode === 'full') return null;
    const seeds = new Set<string>([...current, ...(selection?.kind === 'location' ? [selection.id] : [])]);
    if (seeds.size === 0 && graph.locations[0]) seeds.add(graph.locations.find((l) => l.initial)?.id ?? graph.locations[0].id);
    const keep = new Set(seeds);
    for (const e of graph.edges) {
      if (seeds.has(e.source)) keep.add(e.target);
      if (seeds.has(e.target)) keep.add(e.source);
    }
    return keep;
  }, [mode, current, selection, graph]);

  const q = query.trim().toLowerCase();
  const hitLocs = useMemo(() => new Set(q ? graph.locations.filter((l) => l.id.toLowerCase().includes(q)).map((l) => l.id) : []), [q, graph]);
  const hitEdges = useMemo(() => new Set(q ? graph.edges.filter((e) => `${e.label} ${e.id}`.toLowerCase().includes(q)).map((e) => e.id) : []), [q, graph]);

  const nodes = useMemo<Node<LocData>[]>(
    () =>
      graph.locations
        .filter((l) => !focus || focus.has(l.id))
        .map((l) => ({
          id: l.id,
          type: 'loc',
          position: overrides[l.id] ?? base[l.id] ?? { x: 0, y: 0 },
          data: {
            label: l.id,
            initial: l.initial,
            invariant: l.invariant,
            details,
            current: current.has(l.id),
            hit: hitLocs.has(l.id),
            selected: selection?.kind === 'location' && selection.id === l.id,
            dim: !!q && !hitLocs.has(l.id) && hitEdges.size === 0,
          },
        })),
    [graph, base, overrides, details, focus, selection, q, hitLocs, hitEdges, current],
  );

  const edges = useMemo<Edge<EdgeData>[]>(() => {
    const seen = new Map<string, number>();
    return graph.edges
      .filter((e) => !focus || (focus.has(e.source) && focus.has(e.target)))
      .map((e) => {
        const pairKey = e.source === e.target ? `loop:${e.source}` : [e.source, e.target].sort().join('→');
        const k = seen.get(pairKey) ?? 0;
        seen.set(pairKey, k + 1);
        const state = overlay.enabledNow?.has(e.id) ? 'now' : overlay.enabledLater?.has(e.id) ? 'later' : null;
        const color = state === 'now' ? 'var(--ok)' : state === 'later' ? 'var(--info)' : overlay.recent?.has(e.id) ? 'var(--formal)' : 'var(--border-strong)';
        return {
          id: e.id,
          source: e.source,
          target: e.target,
          type: 'ta',
          markerEnd: { type: MarkerType.ArrowClosed, color, width: 16, height: 16 },
          data: {
            edge: e,
            offset: k,
            details,
            state,
            recent: !!overlay.recent?.has(e.id),
            selected: selection?.kind === 'edge' && selection.id === e.id,
            emphasis: !!overlay.emphasis?.has(e.id) || hitEdges.has(e.id),
            dim: (!!q && !hitEdges.has(e.id) && !hitLocs.has(e.source)) || (!!overlay.emphasis?.size && !overlay.emphasis.has(e.id)),
          },
        };
      });
  }, [graph, focus, details, overlay, selection, q, hitEdges, hitLocs]);

  // Fit on first render and whenever the visible set changes.
  useEffect(() => {
    // Twice: once on mount, once after the nodes have been measured.
    const a = setTimeout(() => void flow.fitView({ padding: 0.12, maxZoom: 1.2 }), 40);
    const b = setTimeout(() => void flow.fitView({ padding: 0.12, maxZoom: 1.2 }), 350);
    return () => { clearTimeout(a); clearTimeout(b); };
  }, [flow, mode, graph, nodes.length]);

  // Dragging moves the picture only: positions are presentation state, saved per viewer on drop.
  const onNodesChange = (changes: NodeChange<Node<LocData>>[]) => {
    const moved = changes.filter((c): c is NodeChange<Node<LocData>> & { type: 'position'; id: string; position: { x: number; y: number } } => c.type === 'position' && !!(c as { position?: unknown }).position);
    if (moved.length) setOverrides((o) => ({ ...o, ...Object.fromEntries(moved.map((c) => [c.id, c.position])) }));
  };
  const saveDrag = (_: unknown, node: Node) => {
    try {
      localStorage.setItem(storageKey(layoutKey), JSON.stringify({ ...overrides, [node.id]: node.position }));
    } catch {
      /* presentation preference only */
    }
  };
  const resetLayout = () => {
    setOverrides({});
    try {
      localStorage.removeItem(storageKey(layoutKey));
    } catch {
      /* ignore */
    }
    setTimeout(() => void flow.fitView({ padding: 0.15, maxZoom: 1.1 }), 40);
  };
  const centerCurrent = () => {
    const ids = [...current];
    if (ids.length) void flow.fitView({ nodes: ids.map((id) => ({ id })), padding: 0.8, maxZoom: 1.2, duration: 250 });
  };
  const findNext = () => {
    const ids = [...hitLocs, ...graph.edges.filter((e) => hitEdges.has(e.id)).flatMap((e) => [e.source, e.target])];
    if (ids.length) void flow.fitView({ nodes: [...new Set(ids)].map((id) => ({ id })), padding: 0.6, maxZoom: 1.2, duration: 250 });
  };

  return (
    <div
      className="ta-wrap"
      onClick={(e) => {
        // Edge labels are rendered in a separate layer: route their clicks to the selection.
        const el = (e.target as HTMLElement).closest<HTMLElement>('[data-edge]');
        if (el?.dataset.edge) onSelect({ kind: 'edge', id: el.dataset.edge });
      }}
    >
      <div className="ta-toolbar">
        <label className="ta-search">
          <Search size={13} aria-hidden="true" />
          <input value={query} onChange={(e) => setQuery(e.target.value)} onKeyDown={(e) => e.key === 'Enter' && findNext()} placeholder="Search state or event…" aria-label="Search states and transitions" />
        </label>
        <Segmented label="Scope" value={mode} onChange={setMode} options={[{ id: 'full', label: 'Full model' }, { id: 'neighbourhood', label: 'Neighbourhood' }]} />
        <Button size="sm" icon={details ? <EyeOff size={13} /> : <Eye size={13} />} onClick={() => setDetails((d) => !d)}>{details ? 'Hide details' : 'Show details'}</Button>
        <Button size="sm" icon={<Crosshair size={13} />} onClick={centerCurrent} disabled={current.size === 0}>Center current</Button>
        <Button size="sm" icon={<Maximize2 size={13} />} onClick={() => void flow.fitView({ padding: 0.15, maxZoom: 1.1, duration: 250 })}>Fit</Button>
        <Button size="sm" icon={<RotateCcw size={13} />} onClick={resetLayout} disabled={Object.keys(overrides).length === 0}>Reset layout</Button>
      </div>
      <div className="ta-canvas" style={{ height }}>
        <ReactFlow
          nodes={nodes}
          edges={edges}
          nodeTypes={nodeTypes}
          edgeTypes={edgeTypes}
          onNodesChange={onNodesChange}
          onNodeDragStop={saveDrag}
          nodesConnectable={false}
          elementsSelectable
          onNodeClick={(_, n) => onSelect({ kind: 'location', id: n.id })}
          onEdgeClick={(_, e) => onSelect({ kind: 'edge', id: e.id })}
          onPaneClick={() => onSelect(null)}
          minZoom={0.15}
          proOptions={{ hideAttribution: true }}
          aria-label={`Timed automaton ${graph.name}`}
        >
          <Background gap={22} color="var(--divider)" />
          {graph.locations.length > 12 && (
            <MiniMap pannable zoomable ariaLabel="Model overview" bgColor="var(--surface)" nodeColor={(n) => ((n.data as LocData).current ? 'var(--accent)' : 'var(--border-strong)')} style={{ border: '1px solid var(--border)', width: 150, height: 100 }} />
          )}
          <Controls showInteractive={false} />
        </ReactFlow>
      </div>
      <div className="ta-legend xsmall">
        <span><span className="ta-sw ta-sw--cur" /> current state</span>
        <span><span className="ta-sw ta-sw--now" /> enabled now</span>
        <span><span className="ta-sw ta-sw--later" /> enabled after a delay</span>
        <span><span className="ta-sw ta-sw--recent" /> last taken</span>
        <span className="muted"><LocateFixed size={11} aria-hidden="true" /> Drag states to rearrange: presentation only, the model is unchanged.</span>
      </div>
    </div>
  );
}

export function TaDiagram(props: Props) {
  return (
    <ReactFlowProvider>
      <Inner {...props} />
    </ReactFlowProvider>
  );
}
