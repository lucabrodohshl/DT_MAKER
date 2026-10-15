/**
 * Behaviour view: the state machine of one asset type. States are rounded nodes (name, meaning,
 * time limit; a dot marks the initial state); transitions are edges labelled with their trigger
 * (▸ event, ◦ action(), ⧗ after d, τ), their time window and restarts; groups are dotted frames.
 * Double-click adds a state; dragging from a state to another adds a transition and asks what
 * triggers it. The generated timed automaton is in the Generated view.
 */
import {
  Background,
  BackgroundVariant,
  BaseEdge,
  ConnectionMode,
  Controls,
  EdgeLabelRenderer,
  Handle,
  MarkerType,
  Position,
  ReactFlow,
  ReactFlowProvider,
  applyNodeChanges,
  getBezierPath,
  useInternalNode,
  useReactFlow,
  type Connection,
  type Edge,
  type EdgeProps,
  type Node,
  type NodeChange,
  type NodeProps,
} from '@xyflow/react';
import '@xyflow/react/dist/style.css';
import Dagre from '@dagrejs/dagre';
import clsx from 'clsx';
import { memo, useCallback, useMemo, useState } from 'react';
import { safeLayout } from '@/design/graphLayout';
import { addAction, addEvent, addState, addTransition, moveNodes, remove, triggerText, typeByName, windowText } from './ops';
import { TriggerPicker } from './QuickAdd';
import { useModel } from './store';
import type { AssetType, TwinModel, Trigger } from './types';

const W = 160;
const H = 56;

type StateData = { name: string; meaning?: string; limit?: string; initial: boolean; tone: string; selected: boolean; error: boolean };
type GroupData = { name: string; selected: boolean; onSelect: () => void };

const StateNode = memo(function StateNode({ data }: NodeProps<Node<StateData>>) {
  return (
    <div className={clsx('mg-state', data.selected && 'is-selected', data.error && 'has-error', `tone-${data.tone}`)}>
      <Handle id="in" type="target" position={Position.Left} className="mg-in" isConnectableStart={false} />
      <div className="mg-state__name">
        {data.initial && <span className="mg-state__init" title="Initial state" />}
        {data.name}
      </div>
      {data.meaning && <div className="mg-state__meaning" title={data.meaning}>{data.meaning}</div>}
      {data.limit && <span className="mg-state__limit">{data.limit}</span>}
      <Handle id="out" type="source" position={Position.Right} title="Drag to another state to add a transition" />
    </div>
  );
});

const GroupNode = memo(function GroupNode({ data }: NodeProps<Node<GroupData>>) {
  return (
    <div className={clsx('mg-group', data.selected && 'is-selected')} style={{ width: '100%', height: '100%' }}>
      <span className="mg-group__name" onClick={(e) => { e.stopPropagation(); data.onSelect(); }}>{data.name}</span>
      <Handle id="out" type="source" position={Position.Right} style={{ opacity: 0 }} isConnectable={false} />
    </div>
  );
});

type TrData = { trigger: string; window: string; restart: string; selected: boolean; error: boolean; transition: string };

function TransitionEdge({ id, source, target, sourceX, sourceY, targetX, targetY, sourcePosition, targetPosition, data, markerEnd }: EdgeProps<Edge<TrData>>) {
  const { select } = useModel();
  const s = useInternalNode(source);
  let path: string;
  let lx: number;
  let ly: number;
  if (source === target && s) {
    // Self-loop: an arc above the state.
    const x = s.internals.positionAbsolute.x;
    const y = s.internals.positionAbsolute.y;
    const w = s.measured.width ?? W;
    const x1 = x + w * 0.35;
    const x2 = x + w * 0.65;
    path = `M ${x1} ${y} C ${x1 - 30} ${y - 70}, ${x2 + 30} ${y - 70}, ${x2} ${y}`;
    lx = x + w / 2;
    ly = y - 58;
  } else {
    [path, lx, ly] = getBezierPath({ sourceX, sourceY, targetX, targetY, sourcePosition, targetPosition, curvature: 0.3 });
  }
  return (
    <>
      <BaseEdge id={id} path={path} markerEnd={markerEnd} className={clsx('mg-edge', data?.selected && 'is-selected', data?.error && 'is-error')} />
      <EdgeLabelRenderer>
        <div
          className={clsx('mg-label', data?.selected && 'is-selected')}
          style={{ left: lx, top: ly }}
          onClick={(e) => {
            e.stopPropagation();
            if (data) select(data.transition);
          }}
        >
          {data?.trigger}
          {data?.window && <span className="mg-label__sub"> · {data.window}</span>}
          {data?.restart && <span className="mg-label__sub"> · ⟲ {data.restart}</span>}
        </div>
      </EdgeLabelRenderer>
    </>
  );
}

const nodeTypes = { state: StateNode, group: GroupNode };
const edgeTypes = { transition: TransitionEdge };

/** Types that have (or inherit) a behaviour, in model order. */
export function machines(doc: TwinModel): AssetType[] {
  return doc.types.filter((t) => !!t.behavior);
}

/** The behaviour shown: the chosen type, else the selection's, else the system's, else the first. */
export function currentMachine(doc: TwinModel, chosen: string | null, selectionType: string | null): AssetType | undefined {
  if (chosen) return typeByName(doc, chosen);
  if (selectionType && typeByName(doc, selectionType)?.behavior) return typeByName(doc, selectionType);
  const root = typeByName(doc, doc.root);
  if (root?.behavior) return root;
  return machines(doc)[0] ?? root;
}

function limitText(atoms: { timer: string; op: string; bound: number | string }[] | undefined, unit: string) {
  if (!atoms || atoms.length === 0) return undefined;
  return atoms.map((a) => `${a.timer} ${a.op === '<=' ? '≤' : a.op} ${a.bound} ${unit}`).join(', ');
}

function Inner({ type }: { type: AssetType }) {
  const m = useModel();
  const { doc } = m;
  const flow = useReactFlow();
  const b = type.behavior;
  const unit = doc.time?.unit ?? 's';
  const [connect, setConnect] = useState<{ x: number; y: number; from: string; to: string } | null>(null);

  const errors = useMemo(() => new Set(m.diagnostics.filter((d) => d.severity === 'error' && d.element).map((d) => d.element!)), [m.diagnostics]);

  const derived = useMemo(() => {
    const states = b?.states ?? [];
    const positions = doc.layout?.behavior?.[type.name] ?? {};
    const nodes: Node<StateData | GroupData>[] = states.map((s) => ({
      id: s.id,
      type: 'state',
      position: positions[s.id] ?? { x: NaN, y: NaN },
      data: {
        name: s.label && s.label !== s.name ? `${s.name}` : s.name,
        meaning: s.meaning,
        limit: limitText(s.limits, unit),
        initial: !!s.initial,
        tone: s.tone ?? 'normal',
        selected: m.selection === s.id,
        error: errors.has(s.id),
      },
    }));
    if (nodes.some((n) => Number.isNaN(n.position.x))) {
      const g = new Dagre.graphlib.Graph();
      g.setGraph({ rankdir: 'LR', nodesep: 50, ranksep: 110, marginx: 40, marginy: 60 });
      g.setDefaultEdgeLabel(() => ({}));
      for (const n of nodes) g.setNode(n.id, { width: W + 20, height: H + 10 });
      const idOf = new Map(states.map((s) => [s.name, s.id]));
      for (const tr of b?.transitions ?? []) {
        for (const f of tr.from) {
          const a = idOf.get(f);
          const c = idOf.get(tr.to);
          if (a && c) g.setEdge(a, c);
        }
      }
      safeLayout(g);
      for (const n of nodes) {
        if (Number.isNaN(n.position.x)) {
          const p = g.node(n.id) as unknown as { x: number; y: number; width: number; height: number } | undefined;
          n.position = p ? { x: p.x - p.width / 2, y: p.y - p.height / 2 } : { x: 0, y: 0 };
        }
      }
    }
    // Groups: dotted frames around their members.
    const byName = new Map(states.map((s) => [s.name, s.id]));
    const pos = new Map(nodes.map((n) => [n.id, n.position]));
    for (const g of b?.groups ?? []) {
      const members = g.states.map((n) => pos.get(byName.get(n) ?? '')).filter(Boolean) as { x: number; y: number }[];
      if (members.length === 0) continue;
      const x0 = Math.min(...members.map((p) => p.x)) - 18;
      const y0 = Math.min(...members.map((p) => p.y)) - 28;
      const x1 = Math.max(...members.map((p) => p.x)) + W + 18;
      const y1 = Math.max(...members.map((p) => p.y)) + H + 22;
      nodes.unshift({
        id: g.id,
        type: 'group',
        position: { x: x0, y: y0 },
        style: { width: x1 - x0, height: y1 - y0 },
        draggable: false,
        selectable: false,
        zIndex: -1,
        data: { name: g.label ?? g.name, selected: m.selection === g.id, onSelect: () => m.select(g.id) },
      });
    }
    const edges: Edge<TrData>[] = [];
    const groupId = new Map((b?.groups ?? []).map((g) => [g.name, g.id]));
    for (const tr of b?.transitions ?? []) {
      const target = byName.get(tr.to);
      if (!target) continue;
      for (const f of tr.from) {
        const source = byName.get(f) ?? groupId.get(f);
        if (!source) continue;
        edges.push({
          id: `${tr.id}::${f}`,
          source,
          target,
          sourceHandle: 'out',
          targetHandle: 'in',
          type: 'transition',
          markerEnd: { type: MarkerType.ArrowClosed, width: 14, height: 14, color: m.selection === tr.id ? 'var(--cobalt)' : 'var(--ink-3)' },
          data: {
            transition: tr.id,
            trigger: triggerText(tr.trigger as Trigger, unit),
            window: windowText(tr.window),
            restart: (tr.restart ?? []).join(', '),
            selected: m.selection === tr.id,
            error: errors.has(tr.id),
          },
        });
      }
    }
    return { nodes, edges };
  }, [b, doc.layout, type.name, unit, m, errors]);

  const [nodeState, setNodeState] = useState(() => ({ base: derived.nodes, nodes: derived.nodes }));
  const nodes = nodeState.base === derived.nodes ? nodeState.nodes : derived.nodes;
  const onNodesChange = useCallback((changes: NodeChange<Node<StateData | GroupData>>[]) =>
    setNodeState((s) => ({ base: derived.nodes, nodes: applyNodeChanges(changes, s.base === derived.nodes ? s.nodes : derived.nodes) })), [derived.nodes]);
  const stateName = (id: string) => b?.states.find((s) => s.id === id)?.name ?? (b?.groups ?? []).find((g) => g.id === id)?.name;

  const onConnect = (c: Connection) => {
    if (!c.source || !c.target) return;
    const from = stateName(c.source);
    const to = b?.states.find((s) => s.id === c.target)?.name;
    if (!from || !to) return;
    const rect = document.querySelector(`[data-id="${c.target}"]`)?.getBoundingClientRect();
    setConnect({ x: rect ? rect.left : 300, y: rect ? rect.bottom + 6 : 300, from, to });
  };

  const pick = (t: Trigger) => {
    if (!connect) return;
    const err = m.apply((d) => {
      // A trigger that does not exist yet is declared on the type first.
      if ('event' in t && t.event.startsWith('__new__:')) {
        const e = addEvent(d, type.name, t.event.slice(8));
        const name = typeByName(e.doc, type.name)!.events!.at(-1)!.name;
        const tr = addTransition(e.doc, type.name, connect.from, connect.to, { event: name });
        return { doc: tr.doc, label: `${e.label}; ${tr.label}`, select: tr.select };
      }
      if ('action' in t && t.action.startsWith('__new__:')) {
        const a = addAction(d, type.name, t.action.slice(8));
        const name = typeByName(a.doc, type.name)!.actions!.at(-1)!.name;
        const tr = addTransition(a.doc, type.name, connect.from, connect.to, { action: name });
        return { doc: tr.doc, label: `${a.label}; ${tr.label}`, select: tr.select };
      }
      return addTransition(d, type.name, connect.from, connect.to, t);
    });
    if (err) window.alert(err);
    setConnect(null);
  };

  const onKeyDown = (e: React.KeyboardEvent) => {
    const target = e.target as HTMLElement;
    if (target.tagName === 'INPUT' || target.tagName === 'TEXTAREA') return;
    if ((e.key === 'Delete' || e.key === 'Backspace') && m.selection) {
      e.preventDefault();
      const err = m.apply((d) => remove(d, m.selection!));
      if (err) window.alert(err);
      else m.select(null);
    }
  };

  return (
    <div
      className="ms-canvas"
      tabIndex={0}
      onKeyDown={onKeyDown}
      onDoubleClick={(e) => {
        if (!(e.target as HTMLElement).classList.contains('react-flow__pane')) return;
        const p = flow.screenToFlowPosition({ x: e.clientX, y: e.clientY });
        const err = m.apply((d) => addState(d, type.name, 'State', { x: Math.round(p.x - W / 2), y: Math.round(p.y - H / 2) }));
        if (err) window.alert(err);
      }}
      aria-label={`Behaviour of ${type.name}`}
    >
      <ReactFlow
        nodes={nodes}
        edges={derived.edges}
        nodeTypes={nodeTypes}
        edgeTypes={edgeTypes}
        onNodesChange={onNodesChange}
        onNodeClick={(_, n) => m.select(n.id)}
        onEdgeClick={(_, e) => m.select((e.data as TrData).transition)}
        onPaneClick={() => m.select(null)}
        onNodeDragStop={(_, n) => {
          m.apply((d) => moveNodes(d, 'behavior', type.name, { [n.id]: { x: Math.round(n.position.x), y: Math.round(n.position.y) } }), { key: `move:${n.id}` });
        }}
        onConnect={onConnect}
        connectionMode={ConnectionMode.Strict}
        fitView
        fitViewOptions={{ padding: 0.3, maxZoom: 1.1 }}
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
      </ReactFlow>
      {(b?.states.length ?? 0) === 0 && (
        <div className="ms-empty">
          <div>
            <strong>{type.label ?? type.name} has no behaviour yet.</strong>
            <span>Double-click to add its first state (it becomes the initial state). Drag from a state's right edge to another state to add a transition.</span>
          </div>
        </div>
      )}
      <div className="ms-hint">Double-click: add state · drag from a state's edge: add transition · Delete: remove</div>
      {connect && <TriggerPicker at={connect} type={type.name} onPick={pick} onClose={() => setConnect(null)} />}
    </div>
  );
}

export function BehaviorCanvas({ type }: { type: AssetType }) {
  return (
    <ReactFlowProvider key={type.name}>
      <Inner type={type} />
    </ReactFlowProvider>
  );
}
