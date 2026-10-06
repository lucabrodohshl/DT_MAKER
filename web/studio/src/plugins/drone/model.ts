/**
 * Data model of the drone mission view. Pure functions only: decoding the
 * runtime's JSON (grids, map updates, plans, ledger records) into what the map
 * and panels draw.
 *
 * Nothing here decides behaviour. Locations, transitions, plan acceptance and
 * admissibility come from the runtime (kernel, ledger, planner verdicts); this
 * module only reshapes them for display.
 */
import type { LedgerRecord } from '@/runtime/types';

/** Occupancy characters of the world API (see include/twin/geo/grid.hpp). */
export type CellChar = '?' | '.' | '#' | 'o' | 'D' | 'd' | '!';

export interface Grid {
  width: number;
  height: number;
  cellSizeMm: number;
  /** Row-major occupancy characters. */
  cells: string[];
}

export interface GridJson {
  width: number;
  height: number;
  cell_size_mm: number;
  rows: string[];
}

export interface CellChange {
  cell: [number, number];
  occupancy: string;
}

export interface MapUpdateJson {
  seq: number;
  at: number;
  kind: string;
  description: string;
  cells: CellChange[];
  unknown_cells?: number;
}

export interface Point {
  /** Millimetres east. */
  x: number;
  /** Millimetres south. */
  y: number;
}

export interface PlanJson {
  id: number;
  episode?: number;
  candidate?: string;
  profile?: string;
  goal: string;
  status: string;
  reason: string;
  start_mm: [number, number];
  waypoints_mm: [number, number][];
  cost_mm: number;
  objective_mm?: number;
  length_mm: number;
  unknown_mm: number;
  energy_mwh: number;
  expanded: number;
  created_at: number;
  ended_at: number;
}

export interface CandidateJson {
  label: string;
  profile: string;
  found: boolean;
  failure: string;
  duplicate_of: string;
  plan: PlanJson | Record<string, never>;
  geometric: { ok: boolean; issues: string[] };
  behavioural: { checked: boolean; ok: boolean; detail: string; schedule: { label: string; at: number }[] };
  selected: boolean;
}

export interface EpisodeJson {
  id: number;
  at: number;
  goal: string;
  reason: string;
  decision: string;
  candidates: CandidateJson[];
  selected: string;
}

export interface MissionTargetJson {
  id: string;
  name: string;
  cell: [number, number];
  status: 'pending' | 'current' | 'inspected' | 'unreachable' | string;
}

export interface MissionJson {
  mission: string;
  home: [number, number];
  goal: string;
  targets: MissionTargetJson[];
  planning: boolean;
  finished: boolean;
  planner: string;
}

/** Telemetry as published by the flight controller (integers; see docs/runtime-api.md). */
export interface TelemetryJson {
  at: number;
  x_mm: number;
  y_mm: number;
  alt_mm: number;
  vx_mm_s?: number;
  vy_mm_s?: number;
  speed_mm_s?: number;
  battery_permille: number;
  energy_mwh: number;
  heading_cdeg: number;
  gimbal_cdeg: number;
  mode: string;
  route_id: number;
  waypoint_index: number;
  goal: string;
  ledger_seq?: number;
}

const CELL_CHARS: Record<string, CellChar> = {
  unknown: '?', free: '.', wall: '#', obstacle: 'o', door_open: 'D', door_closed: 'd', hazard: '!',
};

export function decodeGrid(j: GridJson | null | undefined): Grid | null {
  if (!j || !Array.isArray(j.rows) || j.rows.length === 0) return null;
  const cells: string[] = [];
  for (const row of j.rows) for (const ch of row) cells.push(ch);
  return { width: j.width, height: j.height, cellSizeMm: j.cell_size_mm || 500, cells };
}

export function cellAt(g: Grid, x: number, y: number): string {
  if (x < 0 || y < 0 || x >= g.width || y >= g.height) return '#';
  return g.cells[y * g.width + x] ?? '#';
}

/** A new grid with the update's cells applied (the twin's world model does the same). */
export function applyUpdate(g: Grid, update: Pick<MapUpdateJson, 'cells'>): Grid {
  const cells = g.cells.slice();
  for (const c of update.cells) {
    const [x, y] = c.cell;
    if (x < 0 || y < 0 || x >= g.width || y >= g.height) continue;
    cells[y * g.width + x] = CELL_CHARS[c.occupancy] ?? '?';
  }
  return { ...g, cells };
}

export function unknownCount(g: Grid | null): number {
  return g ? g.cells.reduce((n, c) => n + (c === '?' ? 1 : 0), 0) : 0;
}

/** Plan polyline (start + waypoints) in millimetres. */
export function planPoints(p: Pick<PlanJson, 'start_mm' | 'waypoints_mm'>): Point[] {
  const pts: Point[] = [];
  if (Array.isArray(p.start_mm)) pts.push({ x: p.start_mm[0], y: p.start_mm[1] });
  for (const w of p.waypoints_mm ?? []) pts.push({ x: w[0], y: w[1] });
  return pts;
}

export function isPlan(p: unknown): p is PlanJson {
  return !!p && typeof p === 'object' && Array.isArray((p as PlanJson).waypoints_mm);
}

export const mm = (v: number | undefined, digits = 1) => (v === undefined ? '—' : (v / 1000).toFixed(digits));

// ------------------------------------------------------------------ timeline

export type EntryTone = 'ok' | 'warning' | 'critical' | 'info' | 'neutral' | 'formal';

/** One timeline entry, always tied to a ledger record (seq) of the execution. */
export interface TimelineEntry {
  seq: number;
  at: number;
  tone: EntryTone;
  title: string;
  detail?: string;
  /** Low-signal entries (waypoints, commands) hidden unless "all records" is chosen. */
  minor: boolean;
  kind: string;
}

type Labeler = (dtLabel: string) => string;
/** Presentation tone of a location (from the twin's presentation metadata, never inferred here). */
type ToneOf = (location: string) => EntryTone;

/** Translate one ledger record into a human timeline entry (null: not shown). */
export function timelineEntry(r: LedgerRecord, eventLabel: Labeler, toneOf: ToneOf): TimelineEntry | null {
  const b = r.body;
  const at = (b.time_after ?? b.at ?? 0) as number;
  const kind = b.kind;
  if (kind === 'genesis') {
    // Replay frames carry only the recomputed fields (no chain/identity block).
    const pkg = (b as { package?: LedgerRecord['body']['package'] }).package;
    const detail = pkg ? `Verified package ${pkg.hash.slice(0, 12)}…, model ${pkg.model_id} ${pkg.model_version}` : 'Initial state of the verified model';
    return { seq: r.seq, at, tone: 'formal', title: 'Twin session started', detail, minor: false, kind };
  }
  if (kind === 'end') return { seq: r.seq, at, tone: 'neutral', title: 'Session ended', detail: String(b.reason ?? ''), minor: false, kind };
  if (kind === 'alarm') return { seq: r.seq, at, tone: 'critical', title: `Monitoring alarm: ${String(b.alarm ?? '')}`, detail: String(b.detail ?? ''), minor: false, kind };
  if (kind === 'reject') {
    const err = (b as { error?: { message?: string } }).error;
    return { seq: r.seq, at, tone: 'critical', title: `Refused by the kernel: ${b.input?.name ?? ''}`, detail: err?.message, minor: false, kind };
  }
  if (kind === 'step') {
    const br = b.outcome?.branches?.[0];
    const label = b.input?.name ?? br?.label ?? '';
    const reason = (b.input?.payload as { reason?: string } | undefined)?.reason;
    const from = br?.source ?? '';
    const to = br?.target ?? '';
    const self = from === to;
    const target = toneOf(to);
    return {
      seq: r.seq,
      at: (b.time_after ?? at) as number,
      tone: target === 'warning' || target === 'critical' ? target : toneOf(from) === 'warning' ? 'ok' : 'info',
      title: self ? eventLabel(label) : `${from} → ${to}`,
      detail: self ? reason : `${eventLabel(label)}${reason ? ` — ${reason}` : ''}`,
      minor: self && label === 'waypoint_reached!',
      kind,
    };
  }
  if (kind === 'context') {
    const data = (b.data ?? {}) as Record<string, unknown>;
    const topic = b.topic;
    if (topic === 'knowledge') {
      const g = decodeGrid(data.map as GridJson);
      const m = data.mission as { targets?: unknown[] } | undefined;
      return { seq: r.seq, at, tone: 'info', title: 'Mission and facility plan received', detail: `${m?.targets?.length ?? 0} inspection targets; ${unknownCount(g)} cells unknown to the twin`, minor: false, kind };
    }
    if (topic === 'map_update') {
      const facility = data.kind === 'facility_notice';
      const desc = String(data.description ?? '');
      const blocking = /closed door|obstacle|wall|hazard|no-fly/i.test(desc);
      return {
        seq: r.seq,
        at: (data.at as number) ?? at,
        tone: blocking ? 'warning' : 'neutral',
        title: facility ? 'Facility notice received' : blocking ? 'New obstacle discovered' : 'Environment observation received',
        detail: desc,
        minor: !blocking && !facility,
        kind: `context:${topic}`,
      };
    }
    if (topic === 'planning') {
      const ep = data as unknown as EpisodeJson;
      const found = ep.candidates?.filter((c) => c.found && !c.duplicate_of).length ?? 0;
      return {
        seq: r.seq,
        at: ep.at ?? at,
        tone: ep.selected ? 'info' : 'critical',
        title: `Planning episode ${ep.id}: ${found} distinct candidate${found === 1 ? '' : 's'} for ${ep.goal}`,
        detail: ep.selected ? `Selected candidate ${ep.selected} (geometrically feasible and admitted by the kernel)` : 'No candidate is both geometrically feasible and behaviourally admissible',
        minor: false,
        kind: `context:${topic}`,
      };
    }
    if (topic === 'command') {
      const c = (data.command ?? {}) as { kind?: string; route_id?: number; target_id?: string };
      const what = c.kind === 'follow_route' ? `follow route ${c.route_id}` : c.kind === 'inspect' ? `inspect ${c.target_id}` : (c.kind ?? '').replace('_', ' ');
      return { seq: r.seq, at: (b.at as number) ?? at, tone: data.accepted ? 'neutral' : 'critical', title: `Command to drone: ${what}`, detail: data.accepted ? undefined : String(data.error ?? 'refused by the flight controller'), minor: true, kind: `context:${topic}` };
    }
  }
  return null;
}

// ------------------------------------------------------------------ replay reconstruction

export interface ReplayFrameJson {
  seq: number;
  kind: string;
  hash: string;
  fields: Record<string, unknown>;
}

export interface Reconstruction {
  known: Grid | null;
  recent: Set<number>;
  activePlan: PlanJson | null;
  endedPlans: PlanJson[];
  episode: EpisodeJson | null;
  location: string;
  targets: MissionTargetJson[];
  home: [number, number] | null;
}

/**
 * What the twin knew, planned and decided up to (and including) ledger record
 * @p seq, reconstructed only from the recorded execution (replay frames).
 */
export function reconstruct(frames: ReplayFrameJson[], seq: number): Reconstruction {
  let known: Grid | null = null;
  let recent = new Set<number>();
  const plans = new Map<number, PlanJson>();
  let active: PlanJson | null = null;
  const ended: PlanJson[] = [];
  let episode: EpisodeJson | null = null;
  let location = '';
  let targets: MissionTargetJson[] = [];
  let home: [number, number] | null = null;
  const inspected = new Set<string>();
  let reached = '';
  for (const f of frames) {
    if (f.seq > seq) break;
    const fields = f.fields;
    const state = fields.state_after as { location: string }[] | undefined;
    if (state?.[0]) location = state[0].location;
    if (f.kind === 'context') {
      const data = (fields.data ?? {}) as Record<string, unknown>;
      if (fields.topic === 'knowledge') {
        known = decodeGrid(data.map as GridJson);
        const m = data.mission as { home?: [number, number]; targets?: { id: string; name: string; cell: [number, number] }[] } | undefined;
        home = m?.home ?? null;
        targets = (m?.targets ?? []).map((t) => ({ ...t, status: 'pending' }));
      } else if (fields.topic === 'map_update' && known) {
        const u = data as unknown as MapUpdateJson;
        known = applyUpdate(known, u);
        recent = new Set(u.cells.map((c) => c.cell[1] * (known as Grid).width + c.cell[0]));
      } else if (fields.topic === 'planning') {
        episode = data as unknown as EpisodeJson;
        for (const c of episode.candidates ?? []) if (isPlan(c.plan)) plans.set(c.plan.id, c.plan);
      } else if (fields.topic === 'command') {
        const c = (data.command ?? {}) as { kind?: string; route_id?: number; goal?: string; waypoints_mm?: [number, number][] };
        if (c.kind === 'follow_route' && data.accepted && c.route_id !== undefined) {
          const p = plans.get(c.route_id);
          if (active && active.id !== c.route_id && !ended.some((e) => e.id === active!.id)) ended.push({ ...active, status: 'superseded' });
          active = p ? { ...p, status: 'active' } : null;
        }
      }
    } else if (f.kind === 'step') {
      const input = fields.input as { name?: string; payload?: { plan_id?: number; reason?: string } } | undefined;
      const label = input?.name ?? '';
      if ((label === 'path_invalidated!' || label === 'replan_requested!') && active) {
        ended.push({ ...active, status: label === 'path_invalidated!' ? 'invalidated' : 'superseded', reason: input?.payload?.reason ?? '' });
        active = null;
      }
      if (label === 'target_reached!' && active) {
        if (active.goal.startsWith('target:')) reached = active.goal.slice(7);
        active = null;
      }
      if (label === 'inspection_complete!' && reached) inspected.add(reached);
      if (label === 'home_reached!') active = null;
    }
  }
  targets = targets.map((t) => ({ ...t, status: inspected.has(t.id) ? 'inspected' : t.status }));
  return { known, recent, activePlan: active, endedPlans: ended, episode, location, targets, home };
}
