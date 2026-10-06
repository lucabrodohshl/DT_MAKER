/**
 * Behavioural model graph (timed automaton from the Twin IR).
 *
 * Structure comes from the IR (runtime /runtime/model or the deployed package's IR);
 * highlighting comes only from kernel output: current location(s) and the
 * enabled transitions reported by GET /runtime/state, and transitions the runtime
 * reported as taken. Nothing about enabledness is computed here.
 */
import Dagre from '@dagrejs/dagre';
import { safeLayout } from '@/design/graphLayout';
import {
  Background,
  Controls,
  MarkerType,
  MiniMap,
  ReactFlow,
  ReactFlowProvider,
  useReactFlow,
  type Edge,
  type Node,
} from '@xyflow/react';
import '@xyflow/react/dist/style.css';
import { Crosshair, Focus, Maximize2 } from 'lucide-react';
import { useEffect, useMemo, useState } from 'react';
import type { TwinPresentation } from '@/api/types';
import type { RuntimeState, TwinIr } from '@/runtime/types';
import { Button, Segmented, toneOf } from '@/design';

export interface Selection {
  kind: 'location' | 'transition';
  id: string;
}

export function constraintText(c: { clock: string; op: string; bound: number }): string {
  return `${c.clock} ${c.op} ${c.bound}`;
}
export function conjunction(cs: { clock: string; op: string; bound: number }[]): string {
  return cs.length === 0 ? 'true' : cs.map(constraintText).join(' ∧ ');
}
export function actionLabel(a: TwinIr['transitions'][number]['action']): string {
  if (!a.channel) return 'τ (internal)';
  return a.kind === 'receive' ? `${a.channel}?` : `${a.channel}!`;
}

const TONE_VAR: Record<string, string> = {
  ok: '--ok',
  warning: '--warn',
  critical: '--crit',
  info: '--info',
  neutral: '--neutral',
  formal: '--formal',
};

function layout(nodes: Node[], edges: Edge[]): Node[] {
  const g = new Dagre.graphlib.Graph({ multigraph: true }).setDefaultEdgeLabel(() => ({}));
  g.setGraph({ rankdir: 'LR', nodesep: 50, ranksep: 120, marginx: 20, marginy: 20 });
  nodes.forEach((n) => g.setNode(n.id, { width: 170, height: 56 }));
  edges.forEach((e) => g.setEdge(e.source, e.target, {}, e.id));
  safeLayout(g);
  return nodes.map((n) => {
    const p = g.node(n.id);
    return { ...n, position: { x: p.x - 85, y: p.y - 28 } };
  });
}

interface Props {
  ir: TwinIr;
  state: RuntimeState | null;
  presentation: TwinPresentation;
  recent: string[];
  selection: Selection | null;
  onSelect: (s: Selection | null) => void;
  height?: number;
}

function GraphInner({ ir, state, presentation, recent, selection, onSelect, height = 520 }: Props) {
  const [mode, setMode] = useState<'all' | 'neighborhood'>(ir.locations.length > 24 ? 'neighborhood' : 'all');
  const [query, setQuery] = useState('');
  const flow = useReactFlow();
  const current = useMemo(() => new Set(state?.configurations.map((c) => c.location) ?? []), [state]);
  const enabledNow = useMemo(() => new Set(state?.enabled.filter((e) => e.enabled_now).map((e) => e.transition) ?? []), [state]);
  const enabledLater = useMemo(() => new Set(state?.enabled.filter((e) => !e.enabled_now).map((e) => e.transition) ?? []), [state]);

  const focusIds = useMemo(() => {
    if (mode === 'all') return null;
    const anchors = new Set<string>(current);
    if (selection?.kind === 'location') anchors.add(selection.id);
    if (anchors.size === 0 && ir.initial) anchors.add(ir.initial);
    const ids = new Set(anchors);
    ir.transitions.forEach((t) => {
      if (anchors.has(t.source)) ids.add(t.target);
      if (anchors.has(t.target)) ids.add(t.source);
    });
    return ids;
  }, [mode, current, selection, ir]);

  const { nodes, edges } = useMemo(() => {
    const locs = ir.locations.filter((l) => !focusIds || focusIds.has(l.id));
    const ids = new Set(locs.map((l) => l.id));
    const q = query.trim().toLowerCase();
    const ns: Node[] = locs.map((l) => {
      const p = presentation.states?.[l.id];
      const tone = toneOf(p?.tone);
      const isCurrent = current.has(l.id);
      const matches = q && (l.id.toLowerCase().includes(q) || p?.label?.toLowerCase().includes(q));
      const selected = selection?.kind === 'location' && selection.id === l.id;
      return {
        id: l.id,
        position: { x: 0, y: 0 },
        data: {
          label: (
            <div style={{ textAlign: 'left', lineHeight: 1.25 }}>
              <div style={{ fontWeight: 600, fontSize: 12 }}>
                {isCurrent ? '● ' : ''}
                {p?.label ?? l.id}
              </div>
              <div style={{ fontSize: 10, opacity: 0.75, fontFamily: 'var(--font-mono)' }}>
                {l.id}
                {l.invariant.length > 0 ? ` · ${conjunction(l.invariant)}` : ''}
              </div>
            </div>
          ),
        },
        style: {
          width: 170,
          borderRadius: 8,
          padding: '6px 10px',
          border: `${isCurrent ? 3 : selected ? 2 : 1}px solid var(${isCurrent ? TONE_VAR[tone] : selected ? '--focus' : '--border-strong'})`,
          background: isCurrent ? `var(${tone === 'neutral' ? '--neutral-soft' : `--${tone === 'warning' ? 'warn' : tone === 'critical' ? 'crit' : tone}-soft`})` : 'var(--surface)',
          color: 'var(--text)',
          boxShadow: matches ? '0 0 0 3px var(--focus)' : undefined,
        },
        ariaLabel: `${p?.label ?? l.id}${isCurrent ? ', current state' : ''}`,
      } satisfies Node;
    });
    const es: Edge[] = ir.transitions
      .filter((t) => ids.has(t.source) && ids.has(t.target))
      .map((t) => {
        const now = enabledNow.has(t.id);
        const later = enabledLater.has(t.id);
        const wasRecent = recent.includes(t.id);
        const selected = selection?.kind === 'transition' && selection.id === t.id;
        const color = now ? 'var(--ok)' : later ? 'var(--info)' : wasRecent ? 'var(--formal)' : 'var(--border-strong)';
        return {
          id: t.id,
          source: t.source,
          target: t.target,
          label: `${presentation.events?.[t.action.channel ?? '']?.label ?? actionLabel(t.action)}${t.guard.length ? ` [${conjunction(t.guard)}]` : ''}`,
          labelStyle: { fontSize: 10, fill: 'var(--text-muted)', fontWeight: now ? 700 : 400 },
          labelBgStyle: { fill: 'var(--surface)' },
          style: { stroke: color, strokeWidth: now || selected ? 2.6 : wasRecent ? 2 : 1.2, strokeDasharray: later && !now ? '5 4' : undefined },
          markerEnd: { type: MarkerType.ArrowClosed, color },
          animated: false,
        } satisfies Edge;
      });
    return { nodes: layout(ns, es), edges: es };
  }, [ir, focusIds, presentation, current, enabledNow, enabledLater, recent, selection, query]);

  const centerOnCurrent = (zoom = 1.2, duration = 200) => {
    const first = [...current][0];
    const n = nodes.find((x) => x.id === first);
    if (n) flow.setCenter(n.position.x + 85, n.position.y + 28, { zoom, duration });
    return !!n;
  };

  // Fit the model when it stays readable; a large model opens centred on the current state
  // instead of as an unreadable whole ("Fit" still shows everything on request).
  useEffect(() => {
    let check: ReturnType<typeof setTimeout> | undefined;
    const fit = setTimeout(() => {
      void flow.fitView({ padding: 0.15, duration: 0, maxZoom: 1.2 });
      check = setTimeout(() => {
        if (flow.getZoom() < 0.7) centerOnCurrent(0.9, 0);
      }, 60);
    }, 30);
    return () => {
      clearTimeout(fit);
      clearTimeout(check);
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [nodes.length, flow]);

  return (
    <div className="stack-sm">
      <div className="row-wrap">
        <label className="sr-only" htmlFor="bg-search">Search states</label>
        <input id="bg-search" className="vts-input" placeholder="Search states…" value={query} onChange={(e) => setQuery(e.target.value)} style={{ width: 200 }} />
        <Segmented label="Graph scope" value={mode} onChange={setMode} options={[{ id: 'all', label: 'Whole model' }, { id: 'neighborhood', label: 'Neighbourhood' }]} />
        <Button size="sm" icon={<Crosshair size={13} />} onClick={() => centerOnCurrent()} disabled={current.size === 0}>
          Center on current
        </Button>
        <Button size="sm" icon={<Maximize2 size={13} />} onClick={() => flow.fitView({ padding: 0.15, duration: 200 })}>
          Fit
        </Button>
        {selection && (
          <Button size="sm" variant="ghost" icon={<Focus size={13} />} onClick={() => onSelect(null)}>
            Clear selection
          </Button>
        )}
        <span className="grow" />
        <span className="xsmall subtle row-wrap" aria-hidden="true">
          <span style={{ color: 'var(--ok)' }}>━ enabled now</span>
          <span style={{ color: 'var(--info)' }}>┅ enabled after a delay</span>
          <span style={{ color: 'var(--formal)' }}>━ recently taken</span>
          <span>● current state</span>
        </span>
      </div>
      <div style={{ height, border: '1px solid var(--border)', borderRadius: 'var(--radius)', background: 'var(--bg)' }}>
        <ReactFlow
          nodes={nodes}
          edges={edges}
          nodesDraggable={false}
          nodesConnectable={false}
          elementsSelectable
          onNodeClick={(_e, n) => onSelect({ kind: 'location', id: n.id })}
          onEdgeClick={(_e, e) => onSelect({ kind: 'transition', id: e.id })}
          onPaneClick={() => onSelect(null)}
          minZoom={0.2}
          proOptions={{ hideAttribution: true }}
          aria-label="Behavioural model graph"
        >
          <Background gap={20} color="var(--divider)" />
          {nodes.length > 20 && <MiniMap pannable zoomable ariaLabel="Model overview" bgColor="var(--surface)" nodeColor={(n) => (current.has(n.id) ? 'var(--accent)' : 'var(--border-strong)')} maskColor="color-mix(in srgb, var(--text) 8%, transparent)" style={{ background: 'var(--surface)', border: '1px solid var(--border)' }} />}
          <Controls showInteractive={false} />
        </ReactFlow>
      </div>
    </div>
  );
}

export function BehaviorGraph(props: Props) {
  return (
    <ReactFlowProvider>
      <GraphInner {...props} />
    </ReactFlowProvider>
  );
}
