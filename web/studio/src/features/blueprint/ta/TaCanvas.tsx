/**
 * Editable diagram of one timed automaton (React Flow): locations are nodes (initial marker,
 * invariant, error highlight), transitions are floating edges with guard / sync / reset
 * labels — self-loops and parallel transitions are drawn apart. Drag a location to move it,
 * drag from its connector dot onto another location (or itself) to create a transition,
 * double-click the background to add a location. Positions are presentation only.
 */
import {
  Background,
  BaseEdge,
  ConnectionMode,
  Controls,
  EdgeLabelRenderer,
  Handle,
  MiniMap,
  Position,
  ReactFlow,
  ReactFlowProvider,
  useInternalNode,
  useReactFlow,
  type Edge,
  type EdgeProps,
  type Node,
  type NodeProps,
  type OnSelectionChangeParams,
} from '@xyflow/react';
import '@xyflow/react/dist/style.css';
import clsx from 'clsx';
import { memo, useCallback, useEffect, useMemo, useRef } from 'react';
import type { TaLayout, TaModel } from '@/api/types';
import { conjunctionText, edgeLabel } from './taEdit';

const NODE_W = 150;
const NODE_H = 52;

interface LocData extends Record<string, unknown> {
  name: string;
  initial: boolean;
  invariant: string;
  error: boolean;
  dim: boolean;
}

const LocationNode = memo(function LocationNode({ data, selected }: NodeProps<Node<LocData>>) {
  return (
    <div className={clsx('vts-ta-node', data.initial && 'is-initial', selected && 'is-selected', data.error && 'has-error')} style={{ width: NODE_W, opacity: data.dim ? 0.35 : 1 }} title={data.initial ? `${data.name} (initial)` : data.name}>
      <Handle type="target" position={Position.Left} className="vts-ta-target" isConnectableStart={false} />
      <span>{data.name}</span>
      {data.invariant && <span className="vts-ta-node__inv">inv: {data.invariant}</span>}
      <Handle type="source" position={Position.Right} className="vts-ta-source" />
    </div>
  );
});

interface EdgeData extends Record<string, unknown> {
  guard: string;
  sync: string;
  resets: string;
  index: number;
  count: number;
  error: boolean;
  dim: boolean;
  onPick: (id: string, additive: boolean) => void;
}

/** Intersection of the segment centre(a) -> centre(b) with a's rounded box (approximated as a rectangle). */
function borderPoint(cx: number, cy: number, w: number, h: number, tx: number, ty: number): [number, number] {
  const dx = tx - cx;
  const dy = ty - cy;
  if (dx === 0 && dy === 0) return [cx, cy];
  const sx = w / 2 / Math.abs(dx || 1e-9);
  const sy = h / 2 / Math.abs(dy || 1e-9);
  const s = Math.min(sx, sy);
  return [cx + dx * s, cy + dy * s];
}

const TaEdgeView = memo(function TaEdgeView({ id, source, target, data, selected, markerEnd }: EdgeProps<Edge<EdgeData>>) {
  const a = useInternalNode(source);
  const b = useInternalNode(target);
  if (!a || !b || !data) return null;
  const aw = a.measured.width ?? NODE_W;
  const ah = a.measured.height ?? NODE_H;
  const bw = b.measured.width ?? NODE_W;
  const bh = b.measured.height ?? NODE_H;
  const acx = a.internals.positionAbsolute.x + aw / 2;
  const acy = a.internals.positionAbsolute.y + ah / 2;
  const bcx = b.internals.positionAbsolute.x + bw / 2;
  const bcy = b.internals.positionAbsolute.y + bh / 2;
  let path: string;
  let lx: number;
  let ly: number;
  if (source === target) {
    // Self-loop above the location, stacked for several loops.
    const r = 34 + data.index * 22;
    const x1 = acx - 26;
    const x2 = acx + 26;
    const y = acy - ah / 2;
    path = `M ${x1} ${y} C ${x1 - r} ${y - r * 1.6}, ${x2 + r} ${y - r * 1.6}, ${x2} ${y}`;
    lx = acx;
    ly = y - r * 1.25;
  } else {
    // Parallel transitions bend apart; the pair (a->b, b->a) uses opposite sides.
    const offset = (data.index - (data.count - 1) / 2) * 46 + (source > target ? 20 : -20) * (data.count > 1 ? 0 : 1);
    const mx = (acx + bcx) / 2;
    const my = (acy + bcy) / 2;
    const len = Math.hypot(bcx - acx, bcy - acy) || 1;
    const nx = -(bcy - acy) / len;
    const ny = (bcx - acx) / len;
    const cx = mx + nx * offset * 1.4;
    const cy = my + ny * offset * 1.4;
    const [sx, sy] = borderPoint(acx, acy, aw, ah, cx, cy);
    const [tx, ty] = borderPoint(bcx, bcy, bw, bh, cx, cy);
    path = `M ${sx} ${sy} Q ${cx} ${cy} ${tx} ${ty}`;
    lx = 0.25 * sx + 0.5 * cx + 0.25 * tx;
    ly = 0.25 * sy + 0.5 * cy + 0.25 * ty;
  }
  const colour = selected ? 'var(--accent)' : data.error ? 'var(--crit)' : 'var(--text-muted)';
  return (
    <>
      <BaseEdge id={id} path={path} markerEnd={markerEnd} style={{ stroke: colour, strokeWidth: selected ? 2.5 : 1.6, opacity: data.dim ? 0.3 : 1 }} interactionWidth={14} />
      <EdgeLabelRenderer>
        <div
          className={clsx('vts-ta-edge-label nodrag nopan', selected && 'is-selected')}
          style={{ position: 'absolute', transform: `translate(-50%, -50%) translate(${lx}px, ${ly}px)`, opacity: data.dim ? 0.35 : 1 }}
          onClick={(ev) => data.onPick(id, ev.shiftKey)}
          role="button"
          tabIndex={-1}
          aria-label={`Transition ${data.sync}${data.guard ? ` when ${data.guard}` : ''}`}
        >
          {data.guard && <div className="g">{data.guard}</div>}
          <div className="s">{data.sync}</div>
          {data.resets && <div className="r">{data.resets}</div>}
        </div>
      </EdgeLabelRenderer>
    </>
  );
});

const nodeTypes = { loc: LocationNode };
const edgeTypes = { ta: TaEdgeView };

export interface TaCanvasProps {
  model: TaModel;
  layout: TaLayout;
  editable: boolean;
  selection: { locations: string[]; edges: string[] };
  onSelection: (s: { locations: string[]; edges: string[] }) => void;
  onMove: (positions: Record<string, { x: number; y: number }>) => void;
  onConnect: (source: string, target: string) => void;
  onAddLocation: (at: { x: number; y: number }) => void;
  errorElements: Set<string>;
  /** Locations/edges matching the search; others are dimmed. */
  highlight: Set<string> | null;
  focus: { id: string; nonce: number } | null;
}

function Inner({ model, layout, editable, selection, onSelection, onMove, onConnect, onAddLocation, errorElements, highlight, focus }: TaCanvasProps) {
  const rf = useReactFlow();
  const selRef = useRef(selection);
  useEffect(() => {
    selRef.current = selection;
  });
  const pick = useCallback(
    (id: string, additive: boolean) => {
      const s = selRef.current;
      onSelection(additive ? { locations: s.locations, edges: s.edges.includes(id) ? s.edges.filter((x) => x !== id) : [...s.edges, id] } : { locations: [], edges: [id] });
    },
    [onSelection],
  );
  const nodes: Node<LocData>[] = useMemo(
    () =>
      model.locations.map((l) => {
        const p = layout.locations[l.name] ?? { x: 0, y: 0 };
        return {
          id: l.name,
          type: 'loc',
          position: { x: p.x - NODE_W / 2, y: p.y - NODE_H / 2 },
          data: { name: l.name, initial: l.initial, invariant: conjunctionText(l.invariant), error: errorElements.has(l.name), dim: !!highlight && !highlight.has(l.name) },
          selected: selection.locations.includes(l.name),
          draggable: editable,
          connectable: editable,
        };
      }),
    [model.locations, layout.locations, errorElements, highlight, selection.locations, editable],
  );
  const edges: Edge<EdgeData>[] = useMemo(() => {
    const groups = new Map<string, string[]>();
    for (const e of model.edges) {
      const key = e.source === e.target ? `${e.source}|loop` : [e.source, e.target].sort().join('|');
      groups.set(key, [...(groups.get(key) ?? []), e.id]);
    }
    return model.edges.map((e) => {
      const key = e.source === e.target ? `${e.source}|loop` : [e.source, e.target].sort().join('|');
      const g = groups.get(key)!;
      return {
        id: e.id,
        source: e.source,
        target: e.target,
        type: 'ta',
        markerEnd: { type: 'arrowclosed' as const, width: 16, height: 16, color: selection.edges.includes(e.id) ? 'var(--accent)' : 'var(--text-muted)' },
        selected: selection.edges.includes(e.id),
        data: {
          guard: conjunctionText(e.guard),
          sync: edgeLabel(e),
          resets: e.resets.length ? e.resets.map((r) => `${r} := 0`).join(', ') : '',
          index: g.indexOf(e.id),
          count: g.length,
          error: errorElements.has(e.id),
          dim: !!highlight && !highlight.has(e.id),
          onPick: pick,
        },
      };
    });
  }, [model.edges, selection.edges, errorElements, highlight, pick]);

  useEffect(() => {
    if (!focus) return;
    if (model.locations.some((l) => l.name === focus.id)) void rf.fitView({ nodes: [{ id: focus.id }], duration: 300, maxZoom: 1.4 });
    else {
      const e = model.edges.find((x) => x.id === focus.id);
      if (e) void rf.fitView({ nodes: [{ id: e.source }, { id: e.target }], duration: 300, maxZoom: 1.2 });
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [focus]);

  return (
    <ReactFlow
      nodes={nodes}
      edges={edges}
      nodeTypes={nodeTypes}
      edgeTypes={edgeTypes}
      fitView
      fitViewOptions={{ padding: 0.2 }}
      minZoom={0.15}
      maxZoom={2.5}
      connectionMode={ConnectionMode.Loose}
      nodesConnectable={editable}
      nodesDraggable={editable}
      elementsSelectable
      selectionOnDrag={false}
      multiSelectionKeyCode="Shift"
      deleteKeyCode={null}
      zoomOnDoubleClick={false}
      onConnect={(c) => c.source && c.target && onConnect(c.source, c.target)}
      onNodeDragStop={(_, n, dragged) => {
        const all = (dragged?.length ? dragged : [n]) as Node[];
        onMove(Object.fromEntries(all.map((x) => [x.id, { x: Math.round(x.position.x + NODE_W / 2), y: Math.round(x.position.y + NODE_H / 2) }])));
      }}
      onSelectionChange={(p: OnSelectionChangeParams) => {
        const locations = p.nodes.map((n) => n.id);
        const es = p.edges.map((x) => x.id);
        const cur = selRef.current;
        if (locations.join() !== cur.locations.join() || es.join() !== cur.edges.join()) onSelection({ locations, edges: es });
      }}
      onDoubleClick={(ev) => {
        if (!editable) return;
        const t = ev.target as HTMLElement;
        if (!t.classList.contains('react-flow__pane')) return;
        onAddLocation(rf.screenToFlowPosition({ x: ev.clientX, y: ev.clientY }));
      }}
      proOptions={{ hideAttribution: true }}
    >
      <Background gap={24} />
      <Controls showInteractive={false} />
      <MiniMap pannable zoomable ariaLabel="Automaton overview" bgColor="var(--surface)" nodeColor={(n) => (n.selected ? 'var(--accent)' : 'var(--border-strong)')} nodeBorderRadius={20} style={{ border: '1px solid var(--border)', width: 160, height: 110 }} />
    </ReactFlow>
  );
}

export function TaCanvas(props: TaCanvasProps) {
  return (
    <ReactFlowProvider>
      <Inner {...props} />
    </ReactFlowProvider>
  );
}
