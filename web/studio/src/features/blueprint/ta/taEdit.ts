/**
 * Pure edits of a canonical timed automaton (twin-ta/1) and its diagram layout
 * (twin-ta-layout/1). Structural validity is decided by the backend validator; these helpers
 * keep references consistent (renames update edges, layout and resets) so an edit never
 * leaves dangling names behind.
 */
import Dagre from '@dagrejs/dagre';
import type { TaAtom, TaEdge, TaLayout, TaLocation, TaModel } from '@/api/types';
import { safeLayout } from '@/design/graphLayout';

export function atomText(a: TaAtom): string {
  return `${a.clock}${a.minus ? ` - ${a.minus}` : ''} ${a.op} ${a.bound}`;
}

export function conjunctionText(c: TaAtom[]): string {
  return c.map(atomText).join(' && ');
}

export function edgeLabel(e: TaEdge): string {
  return e.sync ? `${e.sync.channel}${e.sync.direction}` : 'τ';
}

export function emptyModel(name: string): TaModel {
  return {
    format: 'twin-ta/1',
    name,
    note: '',
    clocks: [{ name: 't', note: 'time in the current mode' }],
    constants: [],
    channels: [],
    locations: [{ name: 'IDLE', initial: true, invariant: [], note: '' }],
    edges: [],
  };
}

export function emptyLayout(): TaLayout {
  return { format: 'twin-ta-layout/1', locations: {}, edges: {} };
}

/** Integer-only layout, restricted to existing locations and edges. */
export function cleanLayout(m: TaModel, l: TaLayout | null): TaLayout {
  const out: TaLayout = { format: 'twin-ta-layout/1', locations: {}, edges: {} };
  for (const loc of m.locations) {
    const p = l?.locations?.[loc.name];
    if (p) out.locations[loc.name] = { x: Math.round(p.x), y: Math.round(p.y), ...(p.label ? { label: { x: Math.round(p.label.x), y: Math.round(p.label.y) } } : {}) };
  }
  for (const e of m.edges) {
    const p = l?.edges?.[e.id];
    if (p) out.edges![e.id] = { nails: (p.nails ?? []).map((n) => ({ x: Math.round(n.x), y: Math.round(n.y) })), ...(p.label ? { label: { x: Math.round(p.label.x), y: Math.round(p.label.y) } } : {}) };
  }
  return out;
}

const IDENT = /^[A-Za-z_][A-Za-z0-9_]*$/;
export const validName = (s: string) => IDENT.test(s);

export function uniqueName(base: string, taken: Iterable<string>): string {
  const set = new Set(taken);
  if (!set.has(base)) return base;
  for (let i = 2; ; i++) if (!set.has(`${base}_${i}`)) return `${base}_${i}`;
}

export function nextEdgeId(m: TaModel): string {
  const taken = new Set(m.edges.map((e) => e.id));
  for (let i = m.edges.length + 1; ; i++) if (!taken.has(`e${i}`)) return `e${i}`;
}

export function addLocation(m: TaModel, l: TaLayout, at: { x: number; y: number }, name?: string): { m: TaModel; l: TaLayout; name: string } {
  const n = name && validName(name) ? uniqueName(name, m.locations.map((x) => x.name)) : uniqueName('STATE', m.locations.map((x) => x.name));
  const loc: TaLocation = { name: n, initial: m.locations.length === 0, invariant: [], note: '' };
  return {
    m: { ...m, locations: [...m.locations, loc] },
    l: { ...l, locations: { ...l.locations, [n]: { x: Math.round(at.x), y: Math.round(at.y) } } },
    name: n,
  };
}

export function renameLocation(m: TaModel, l: TaLayout, from: string, to: string): { m: TaModel; l: TaLayout } {
  if (from === to) return { m, l };
  const locations = { ...l.locations };
  if (locations[from]) {
    locations[to] = locations[from]!;
    delete locations[from];
  }
  return {
    m: {
      ...m,
      locations: m.locations.map((x) => (x.name === from ? { ...x, name: to } : x)),
      edges: m.edges.map((e) => ({ ...e, source: e.source === from ? to : e.source, target: e.target === from ? to : e.target })),
    },
    l: { ...l, locations },
  };
}

export function deleteLocations(m: TaModel, l: TaLayout, names: string[]): { m: TaModel; l: TaLayout } {
  const set = new Set(names);
  const locations = { ...l.locations };
  for (const n of names) delete locations[n];
  const edges = m.edges.filter((e) => !set.has(e.source) && !set.has(e.target));
  const removed = m.locations.filter((x) => set.has(x.name));
  let locs = m.locations.filter((x) => !set.has(x.name));
  if (removed.some((x) => x.initial) && locs.length > 0 && !locs.some((x) => x.initial)) locs = locs.map((x, i) => (i === 0 ? { ...x, initial: true } : x));
  const edgeLayout = { ...(l.edges ?? {}) };
  for (const e of m.edges) if (!edges.includes(e)) delete edgeLayout[e.id];
  return { m: { ...m, locations: locs, edges }, l: { ...l, locations, edges: edgeLayout } };
}

export function setInitial(m: TaModel, name: string): TaModel {
  return { ...m, locations: m.locations.map((x) => ({ ...x, initial: x.name === name })) };
}

export function updateLocation(m: TaModel, name: string, patch: Partial<TaLocation>): TaModel {
  return { ...m, locations: m.locations.map((x) => (x.name === name ? { ...x, ...patch } : x)) };
}

/** Declares the channel of a sync if it is new. */
export function ensureChannel(m: TaModel, channel: string | undefined): TaModel {
  if (!channel || m.channels.some((c) => c.name === channel)) return m;
  return { ...m, channels: [...m.channels, { name: channel, note: '' }] };
}

/** Drops declared channels no edge uses any more. */
export function pruneChannels(m: TaModel): TaModel {
  const used = new Set(m.edges.map((e) => e.sync?.channel).filter((c): c is string => !!c));
  return { ...m, channels: m.channels.filter((c) => used.has(c.name)) };
}

export function addEdge(m: TaModel, source: string, target: string, sync: TaEdge['sync'] = null): { m: TaModel; id: string } {
  const id = nextEdgeId(m);
  const e: TaEdge = { id, source, target, sync, guard: [], resets: [], note: '' };
  return { m: ensureChannel({ ...m, edges: [...m.edges, e] }, sync?.channel), id };
}

export function updateEdge(m: TaModel, id: string, patch: Partial<TaEdge>): TaModel {
  const next = { ...m, edges: m.edges.map((e) => (e.id === id ? { ...e, ...patch } : e)) };
  return pruneChannels(ensureChannel(next, patch.sync?.channel));
}

export function deleteEdges(m: TaModel, l: TaLayout, ids: string[]): { m: TaModel; l: TaLayout } {
  const set = new Set(ids);
  const edges = { ...(l.edges ?? {}) };
  for (const id of ids) delete edges[id];
  return { m: pruneChannels({ ...m, edges: m.edges.filter((e) => !set.has(e.id)) }), l: { ...l, edges } };
}

export function renameClock(m: TaModel, from: string, to: string): TaModel {
  const ren = (a: TaAtom): TaAtom => ({ ...a, clock: a.clock === from ? to : a.clock, ...(a.minus ? { minus: a.minus === from ? to : a.minus } : {}) });
  return {
    ...m,
    clocks: m.clocks.map((c) => (c.name === from ? { ...c, name: to } : c)),
    locations: m.locations.map((x) => ({ ...x, invariant: x.invariant.map(ren) })),
    edges: m.edges.map((e) => ({ ...e, guard: e.guard.map(ren), resets: e.resets.map((r) => (r === from ? to : r)) })),
  };
}

export function clockUses(m: TaModel, clock: string): number {
  let n = 0;
  for (const x of m.locations) n += x.invariant.filter((a) => a.clock === clock || a.minus === clock).length;
  for (const e of m.edges) n += e.guard.filter((a) => a.clock === clock || a.minus === clock).length + (e.resets.includes(clock) ? 1 : 0);
  return n;
}

/** Layered auto-layout (dagre), left to right; positions are diagram coordinates (centres). */
export function autoLayout(m: TaModel, direction: 'LR' | 'TB' = 'LR'): TaLayout {
  const g = new Dagre.graphlib.Graph({ multigraph: true });
  g.setGraph({ rankdir: direction, nodesep: 70, ranksep: 150, marginx: 40, marginy: 40 });
  g.setDefaultEdgeLabel(() => ({}));
  for (const x of m.locations) g.setNode(x.name, { width: 150, height: 56 });
  for (const e of m.edges) if (e.source !== e.target) g.setEdge(e.source, e.target, {}, e.id);
  safeLayout(g);
  const out: TaLayout = { format: 'twin-ta-layout/1', locations: {}, edges: {} };
  for (const x of m.locations) {
    const p = g.node(x.name) as unknown as { x: number; y: number } | undefined;
    out.locations[x.name] = { x: Math.round(p?.x ?? 0), y: Math.round(p?.y ?? 0) };
  }
  return out;
}

/** Positions for every location: layout where present, else an auto-layout. */
export function completeLayout(m: TaModel, l: TaLayout | null): TaLayout {
  const missing = m.locations.some((x) => !l?.locations?.[x.name]);
  if (!l || Object.keys(l.locations ?? {}).length === 0) return autoLayout(m);
  if (!missing) return cleanLayout(m, l);
  const auto = autoLayout(m);
  const out = cleanLayout(m, l);
  for (const x of m.locations) if (!out.locations[x.name]) out.locations[x.name] = auto.locations[x.name]!;
  return out;
}
