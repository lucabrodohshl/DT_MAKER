/**
 * Structure view: the assets (parts) of one type — the system by default — with their
 * relationships and links. Visual grammar (06-studio-ux.md 6.3): an asset is a node (name, type,
 * behaviour line), events are ports on the right (▸), actions ports on the left (◦);
 * relationships are solid labelled edges, links (event → action) dashed. Properties, events and
 * actions are edited in the inspector, never as nodes.
 */
import {
  BaseEdge,
  ConnectionMode,
  EdgeLabelRenderer,
  Handle,
  MarkerType,
  Position,
  ReactFlow,
  ReactFlowProvider,
  getBezierPath,
  useReactFlow,
  type Connection,
  type Edge,
  type EdgeProps,
  type Node,
  type NodeChange,
  type NodeProps,
  applyNodeChanges,
  Background,
  BackgroundVariant,
  Controls,
  MiniMap,
} from '@xyflow/react';
import '@xyflow/react/dist/style.css';
import Dagre from '@dagrejs/dagre';
import clsx from 'clsx';
import { memo, useCallback, useMemo, useState } from 'react';
import { safeLayout } from '@/design/graphLayout';
import { addLink, addRelationship, effective, moveNodes, remove, rootType, typeByName } from './ops';
import { QuickAdd, RelationPicker } from './QuickAdd';
import { useModel } from './store';
import type { AssetType, Part, TwinModel } from './types';

const SELF = '__self';

type AssetData = {
  name: string;
  typeName: string;
  self: boolean;
  external: boolean;
  events: string[];
  actions: string[];
  behavior: string | null;
  errors: number;
  selected: boolean;
};

const AssetNode = memo(function AssetNode({ data }: NodeProps<Node<AssetData>>) {
  return (
    <div className={clsx('mg-asset', data.self && 'is-self', data.external && 'is-external', data.selected && 'is-selected', data.errors > 0 && 'has-error')}>
      <Handle id="in" type="target" position={Position.Left} className="mg-handle mg-handle--rel-in" isConnectableStart={false} />
      <div className="mg-asset__head">
        <div className="mg-asset__name">
          {data.name}
          {data.external && <span title="External: an asset outside the twin, bound when a twin is created">⇢</span>}
        </div>
        <div className="mg-asset__type">{data.self ? `this ${data.typeName}` : data.typeName}</div>
      </div>
      {(data.events.length > 0 || data.actions.length > 0) && (
        <div className="mg-ports">
          {data.actions.map((a) => (
            <div key={`a-${a}`} className="mg-port mg-port--action">
              <Handle id={`ac:${a}`} type="target" position={Position.Left} className="mg-handle mg-handle--action" style={{ zIndex: 2 }} />
              <span className="mg-port__glyph">◦</span>
              {a}()
            </div>
          ))}
          {data.events.map((e) => (
            <div key={`e-${e}`} className="mg-port mg-port--event">
              {e}
              <span className="mg-port__glyph">▸</span>
              <Handle id={`ev:${e}`} type="source" position={Position.Right} className="mg-handle mg-handle--event" />
            </div>
          ))}
        </div>
      )}
      {data.behavior && <div className="mg-asset__beh">◑ {data.behavior}</div>}
      <Handle id="rel" type="source" position={Position.Right} className="mg-handle mg-handle--rel" style={{ top: 14 }} title="Drag to relate" />
      {data.errors > 0 && <span className="mg-err" title={`${data.errors} problem(s)`}>{data.errors}</span>}
    </div>
  );
});

type RelData = { label: string; kind: 'relationship' | 'link'; selected: boolean; error: boolean; symmetric: boolean };

function RelEdge({ id, sourceX, sourceY, targetX, targetY, sourcePosition, targetPosition, data, markerEnd }: EdgeProps<Edge<RelData>>) {
  const { select } = useModel();
  const [path, lx, ly] = getBezierPath({ sourceX, sourceY, targetX, targetY, sourcePosition, targetPosition });
  return (
    <>
      <BaseEdge id={id} path={path} markerEnd={data?.symmetric ? undefined : markerEnd} className={clsx('mg-edge', data?.kind === 'link' && 'is-link', data?.selected && 'is-selected', data?.error && 'is-error')} />
      <EdgeLabelRenderer>
        <div
          className={clsx('mg-label', data?.selected && 'is-selected')}
          style={{ left: lx, top: ly }}
          onClick={(e) => {
            e.stopPropagation();
            select(id);
          }}
        >
          {data?.label}
        </div>
      </EdgeLabelRenderer>
    </>
  );
}

const nodeTypes = { asset: AssetNode };
const edgeTypes = { rel: RelEdge };

function behaviorSummary(doc: TwinModel, t: AssetType | undefined): string | null {
  let cur = t;
  const seen = new Set<string>();
  while (cur && !seen.has(cur.name)) {
    seen.add(cur.name);
    if (cur.behavior) {
      const initial = cur.behavior.states.find((s) => s.initial)?.name ?? cur.behavior.states[0]?.name;
      return `${initial ?? 'no states'} · ${cur.behavior.states.length} state${cur.behavior.states.length === 1 ? '' : 's'}`;
    }
    cur = cur.extends ? typeByName(doc, cur.extends) : undefined;
  }
  return null;
}

/** The node a member path ("tank_01.level_high", "start") hangs on, and the port name on it. */
function endpoint(scope: AssetType, doc: TwinModel, path: string): { node: string; port: string } | null {
  const dot = path.indexOf('.');
  if (dot < 0) return { node: SELF, port: path };
  const partName = path.slice(0, dot);
  const part = effective(doc, scope, 'parts').find((p) => p.name === partName);
  return part ? { node: part.id, port: path.slice(dot + 1) } : null;
}

function Inner() {
  const m = useModel();
  const { doc, selection } = m;
  const scope = typeByName(doc, m.scope) ?? rootType(doc)!;
  const parts = useMemo(() => effective(doc, scope, 'parts'), [doc, scope]);
  const flow = useReactFlow();
  const [quick, setQuick] = useState<{ x: number; y: number; flow: { x: number; y: number } } | null>(null);
  const [relate, setRelate] = useState<{ x: number; y: number; from: string; to: string } | null>(null);

  const errorsOf = useMemo(() => {
    const count = new Map<string, number>();
    for (const d of m.diagnostics) if (d.severity === 'error' && d.element) count.set(d.element, (count.get(d.element) ?? 0) + 1);
    return count;
  }, [m.diagnostics]);

  // Ports: own events and actions, plus nested ones that a link uses (so the link has an end).
  const linkedPorts = useMemo(() => {
    const out = new Map<string, { events: Set<string>; actions: Set<string> }>();
    for (const l of scope.links ?? []) {
      for (const [path, kind] of [[l.from, 'events'], [l.to, 'actions']] as const) {
        const ep = endpoint(scope, doc, path);
        if (!ep) continue;
        const entry = out.get(ep.node) ?? { events: new Set<string>(), actions: new Set<string>() };
        entry[kind].add(ep.port);
        out.set(ep.node, entry);
      }
    }
    return out;
  }, [scope, doc]);

  const derived = useMemo(() => {
    const positions = doc.layout?.structure?.[scope.name] ?? {};
    const items: { id: string; part: Part | null; type: AssetType | undefined }[] = [
      { id: SELF, part: null, type: scope },
      ...parts.map((p) => ({ id: p.id, part: p, type: typeByName(doc, p.type) })),
    ];
    const nodes: Node<AssetData>[] = items.map(({ id, part, type }) => {
      const extra = linkedPorts.get(id);
      const events = [...new Set([...(type ? effective(doc, type, 'events').map((e) => e.name) : []), ...(extra?.events ?? [])])];
      const actions = [...new Set([...(type ? effective(doc, type, 'actions').map((a) => a.name) : []), ...(extra?.actions ?? [])])];
      return {
        id,
        type: 'asset',
        position: positions[id] ?? { x: NaN, y: NaN },
        data: {
          name: part ? part.label ?? part.name : scope.label ?? scope.name,
          typeName: part ? part.type : scope.name,
          self: !part,
          external: !!part?.external,
          events,
          actions,
          behavior: behaviorSummary(doc, type),
          errors: (part ? errorsOf.get(part.id) ?? 0 : 0),
          selected: selection === (part ? part.id : scope.id),
        },
      };
    });
    // Unplaced nodes: layered layout.
    if (nodes.some((n) => Number.isNaN(n.position.x))) {
      const g = new Dagre.graphlib.Graph();
      g.setGraph({ rankdir: 'LR', nodesep: 40, ranksep: 90, marginx: 40, marginy: 40 });
      g.setDefaultEdgeLabel(() => ({}));
      for (const n of nodes) g.setNode(n.id, { width: 200, height: 70 + 18 * (n.data.events.length + n.data.actions.length) });
      for (const r of scope.relationships ?? []) {
        const a = endpoint(scope, doc, `${r.from}.x`);
        const b = endpoint(scope, doc, `${r.to}.x`);
        if (a && b) g.setEdge(r.from === 'self' ? SELF : a.node, r.to === 'self' ? SELF : b.node);
      }
      safeLayout(g);
      for (const n of nodes) {
        if (Number.isNaN(n.position.x)) {
          const p = g.node(n.id) as unknown as { x: number; y: number; width: number; height: number } | undefined;
          n.position = p ? { x: p.x - p.width / 2, y: p.y - p.height / 2 } : { x: 0, y: 0 };
        }
      }
    }
    const edges: Edge<RelData>[] = [];
    const nodeOf = (path: string) => (path === 'self' || path === '' ? SELF : parts.find((p) => p.name === path.split('.')[0])?.id);
    for (const r of scope.relationships ?? []) {
      const s = nodeOf(r.from);
      const t = nodeOf(r.to);
      if (!s || !t) continue;
      const rt = doc.relationshipTypes?.find((x) => x.name === r.type);
      edges.push({
        id: r.id,
        source: s,
        target: t,
        sourceHandle: 'rel',
        targetHandle: 'in',
        type: 'rel',
        markerEnd: { type: MarkerType.ArrowClosed, width: 14, height: 14, color: 'var(--ink-3)' },
        data: { label: rt?.label ?? r.type, kind: 'relationship', selected: selection === r.id, error: (errorsOf.get(r.id) ?? 0) > 0, symmetric: !!rt?.symmetric },
      });
    }
    for (const l of scope.links ?? []) {
      const a = endpoint(scope, doc, l.from);
      const b = endpoint(scope, doc, l.to);
      if (!a || !b) continue;
      edges.push({
        id: l.id,
        source: a.node,
        target: b.node,
        sourceHandle: `ev:${a.port}`,
        targetHandle: `ac:${b.port}`,
        type: 'rel',
        markerEnd: { type: MarkerType.ArrowClosed, width: 12, height: 12, color: 'var(--ink-3)' },
        data: { label: `${a.port} → ${b.port}()`, kind: 'link', selected: selection === l.id, error: (errorsOf.get(l.id) ?? 0) > 0, symmetric: false },
      });
    }
    return { nodes, edges };
  }, [doc, scope, parts, linkedPorts, errorsOf, selection]);

  const [nodeState, setNodeState] = useState(() => ({ base: derived.nodes, nodes: derived.nodes }));
  const nodes = nodeState.base === derived.nodes ? nodeState.nodes : derived.nodes;
  const onNodesChange = useCallback((changes: NodeChange<Node<AssetData>>[]) =>
    setNodeState((s) => ({ base: derived.nodes, nodes: applyNodeChanges(changes, s.base === derived.nodes ? s.nodes : derived.nodes) })), [derived.nodes]);

  const pathOf = (nodeId: string) => (nodeId === SELF ? 'self' : parts.find((p) => p.id === nodeId)?.name ?? '');
  const memberPath = (nodeId: string, port: string) => (nodeId === SELF ? port : `${pathOf(nodeId)}.${port}`);

  const onConnect = (c: Connection) => {
    if (!c.source || !c.target) return;
    if (c.sourceHandle?.startsWith('ev:') && c.targetHandle?.startsWith('ac:')) {
      const err = m.apply((d) => addLink(d, scope.name, memberPath(c.source!, c.sourceHandle!.slice(3)), memberPath(c.target!, c.targetHandle!.slice(3))));
      if (err) window.alert(err);
      return;
    }
    if (c.sourceHandle === 'rel' && c.source !== c.target) {
      const rect = document.querySelector(`[data-id="${c.target}"]`)?.getBoundingClientRect();
      setRelate({ x: rect ? rect.left : 200, y: rect ? rect.top - 8 : 200, from: pathOf(c.source), to: pathOf(c.target) });
    }
  };

  const onKeyDown = (e: React.KeyboardEvent) => {
    const target = e.target as HTMLElement;
    if (target.tagName === 'INPUT' || target.tagName === 'TEXTAREA') return;
    if ((e.key === 'Delete' || e.key === 'Backspace') && m.selection) {
      e.preventDefault();
      const err = m.apply((d) => remove(d, m.selection!));
      if (err) window.alert(err);
      else m.select(null);
    } else if (e.key === 'n' || e.key === 'N') {
      e.preventDefault();
      const r = (e.currentTarget as HTMLElement).getBoundingClientRect();
      const at = { x: r.left + r.width / 2 - 150, y: r.top + 80 };
      setQuick({ ...at, flow: flow.screenToFlowPosition({ x: r.left + r.width / 2, y: r.top + r.height / 2 }) });
    }
  };

  const empty = parts.length === 0;
  return (
    <div
      className="ms-canvas"
      tabIndex={0}
      onKeyDown={onKeyDown}
      onDoubleClick={(e) => {
        if (!(e.target as HTMLElement).classList.contains('react-flow__pane')) return;
        setQuick({ x: e.clientX, y: e.clientY, flow: flow.screenToFlowPosition({ x: e.clientX, y: e.clientY }) });
      }}
      aria-label={`Structure of ${scope.name}`}
    >
      <ReactFlow
        nodes={nodes}
        edges={derived.edges}
        nodeTypes={nodeTypes}
        edgeTypes={edgeTypes}
        onNodesChange={onNodesChange}
        onNodeClick={(_, n) => m.select(n.id === SELF ? scope.id : n.id)}
        onNodeDoubleClick={(_, n) => {
          if (n.id === SELF) return;
          const part = parts.find((p) => p.id === n.id);
          if (part && typeByName(doc, part.type)) m.setScope(part.type);
        }}
        onEdgeClick={(_, e) => m.select(e.id)}
        onPaneClick={() => m.select(null)}
        onNodeDragStop={(_, n) => {
          m.apply((d) => moveNodes(d, 'structure', scope.name, { [n.id]: { x: Math.round(n.position.x), y: Math.round(n.position.y) } }), { key: `move:${n.id}` });
        }}
        onConnect={onConnect}
        connectionMode={ConnectionMode.Strict}
        fitView
        fitViewOptions={{ padding: 0.25, maxZoom: 1.1 }}
        minZoom={0.2}
        maxZoom={2}
        zoomOnDoubleClick={false}
        deleteKeyCode={null}
        proOptions={{ hideAttribution: true }}
        nodesConnectable={m.editable}
        nodesDraggable={m.editable}
      >
        <Background variant={BackgroundVariant.Dots} gap={16} size={1} color="var(--rule-strong)" />
        <Controls showInteractive={false} position="bottom-right" />
        <MiniMap pannable zoomable position="top-right" style={{ width: 140, height: 90, border: '1px solid var(--rule)' }} nodeColor="var(--rule-strong)" />
      </ReactFlow>
      {empty && (
        <div className="ms-empty">
          <div>
            <strong>{scope.label ?? scope.name} has no assets yet.</strong>
            <span>Double-click the canvas (or press N) to add one: type a name like “Pump” or “Temperature sensor”.</span>
          </div>
        </div>
      )}
      <div className="ms-hint">Double-click: add · drag ▸ to ◦: link · drag □: relate · double-click an asset: open its type · Delete: remove</div>
      {quick && (
        <QuickAdd
          at={quick}
          container={scope.name}
          onClose={() => setQuick(null)}
          onPlaced={(id) => {
            m.apply((d) => moveNodes(d, 'structure', scope.name, { [id]: { x: Math.round(quick.flow.x), y: Math.round(quick.flow.y) } }), { key: `move:${id}` });
          }}
        />
      )}
      {relate && (
        <RelationPicker
          at={relate}
          container={scope.name}
          from={relate.from}
          to={relate.to}
          onPick={(type) => {
            const err = m.apply((d) => addRelationship(d, scope.name, type, relate.from, relate.to));
            if (err) window.alert(err);
            setRelate(null);
          }}
          onClose={() => setRelate(null)}
        />
      )}
    </div>
  );
}

export function StructureCanvas() {
  return (
    <ReactFlowProvider>
      <Inner />
    </ReactFlowProvider>
  );
}
