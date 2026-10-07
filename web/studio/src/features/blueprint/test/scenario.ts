/**
 * Pure helpers of the Scenario Builder: step ids, ordering, short descriptions and the
 * mapping of timing results back to scenario steps. Times stay exact decimal strings; numeric
 * values are used only to position marks on the timeline.
 */
import type { ScenarioDef, ScenarioStep, ScenarioStepResult, TimingResult, TimingStepResult } from '@/api/types';

export function stepId(steps: ScenarioStep[]): string {
  let n = 0;
  for (const s of steps) {
    const m = /^s(\d+)$/.exec(s.id);
    if (m) n = Math.max(n, Number(m[1]));
  }
  return `s${n + 1}`;
}

export function scenarioId(list: ScenarioDef[], base = 'scenario'): string {
  const taken = new Set(list.map((s) => s.id));
  if (!taken.has(base)) return base;
  for (let i = 2; ; i++) if (!taken.has(`${base}_${i}`)) return `${base}_${i}`;
}

/** Display position of a decimal time string (presentation only). */
export function timeNum(text: string | undefined | null): number | null {
  if (text === undefined || text === null || text === '') return null;
  const n = Number(text);
  return Number.isFinite(n) ? n : null;
}

/** Steps are evaluated in list order; this is the time a step states, if any. */
export function stepTime(s: ScenarioStep): string | undefined {
  if (s.kind === 'world' && !s.at) return (s.observation as { at?: string } | undefined)?.at;
  return s.at;
}

export function expectText(e: Record<string, unknown> | undefined): string {
  if (!e) return 'expectation';
  if ('location' in e) return `state is ${String(e.location)}`;
  if ('transition' in e) return `took ${String(e.transition)}`;
  if ('event' in e) return `took event ${String(e.event)}`;
  if ('proposition' in e) return `${String(e.proposition)} holds`;
  if ('semantic' in e) return `semantics entail ${String(e.semantic)}`;
  if ('monitor' in e) {
    const m = e.monitor as { id?: string; status?: string };
    return `monitor ${m.id} is ${m.status ?? 'satisfied'}`;
  }
  if ('window' in e) {
    const w = e.window as { label?: string; status?: string; earliest?: string; latest?: string };
    const range = w.earliest !== undefined || w.latest !== undefined ? ` [+${w.earliest ?? '0'}, ${w.latest !== undefined ? `+${w.latest}` : '∞'}]` : '';
    return `${w.label} is ${w.status === 'now' ? 'available now' : w.status === 'later' ? 'available later' : w.status === 'blocked' ? 'not reachable' : w.status ?? '?'}${range}`;
  }
  if ('world' in e) {
    const w = e.world as { object?: string; property?: string; equals?: unknown; exists?: boolean };
    return w.property ? `${w.object}.${w.property} = ${JSON.stringify(w.equals)}` : `${w.object} ${w.exists === false ? 'is absent' : 'exists'}`;
  }
  return JSON.stringify(e);
}

export function worldText(s: ScenarioStep): string {
  const c = (s.change ?? {}) as { action?: string; objectId?: string; property?: string; value?: unknown; object?: { id?: string; name?: string } };
  const what =
    c.action === 'add_object'
      ? `add ${c.object?.name || c.object?.id || 'object'}`
      : c.action === 'remove_object'
        ? `remove ${c.objectId}`
        : c.action === 'move_object'
          ? `move ${c.objectId}`
          : c.action === 'set_layer'
            ? `move ${c.objectId} to another layer`
            : `${c.objectId}.${c.property} := ${JSON.stringify(c.value)}`;
  const obs = s.observation as { event?: string; label?: string } | undefined;
  return obs ? `${what}; observed as ${obs.event ?? obs.label}` : what;
}

export function stepText(s: ScenarioStep): string {
  switch (s.kind) {
    case 'event':
      return `${s.label ?? s.transition ?? '?'}${s.level === 'pt' ? ' (PT)' : ''}${s.expectRefused ? ' — must be refused' : ''}`;
    case 'delay':
      return `wait ${s.delay ?? '?'}`;
    case 'observe':
      return Object.entries(s.telemetry ?? {})
        .map(([k, v]) => `${k} = ${v}`)
        .join(', ');
    case 'world':
      return worldText(s);
    case 'expect':
      return expectText(s.expect);
    default:
      return s.kind;
  }
}

/** Timing status of each scenario step id (formal steps only), from a timing result over the scenario. */
export function timingByStep(t: TimingResult | undefined): Map<string, TimingStepResult> {
  const out = new Map<string, TimingStepResult>();
  if (!t) return out;
  const origins = (t as TimingResult & { origins?: string[] }).origins ?? [];
  t.steps.forEach((s, i) => {
    const o = origins[i];
    if (o) out.set(o.replace(/\/observed$/, ''), s);
  });
  return out;
}

/** Run result of each scenario step id (generated observation events folded into their world step). */
export function runByStep(results: ScenarioStepResult[] | undefined): Map<string, ScenarioStepResult[]> {
  const out = new Map<string, ScenarioStepResult[]>();
  for (const r of results ?? []) {
    const id = r.id.replace(/\/observed$/, '');
    out.set(id, [...(out.get(id) ?? []), r]);
  }
  return out;
}

export interface RefusalFix {
  kind: 'earliest' | 'latest';
  at: string;
}

interface Explanation {
  alternatives?: { window?: { earliest_at?: { text: string; ticks: number }; latest_at?: { text: string; ticks: number } | null } | null }[];
  reasons?: { reason: string; transition: string | null; target: string | null }[];
  requested_delay?: { text: string; ticks: number };
}

/** The legal times the kernel reported for a refused event: move to the earliest or the latest. */
export function refusalFixes(explanation: unknown, requestedTicks: number | undefined): RefusalFix[] {
  const x = (explanation ?? {}) as Explanation;
  const fixes: RefusalFix[] = [];
  const windows = (x.alternatives ?? []).map((a) => a.window).filter((w): w is NonNullable<typeof w> => !!w && !!w.earliest_at);
  if (windows.length === 0 || requestedTicks === undefined) return fixes;
  // Too early: the nearest window that opens after the requested time; too late: the last one that closed before it.
  const later = windows.filter((w) => w.earliest_at!.ticks > requestedTicks).sort((a, b) => a.earliest_at!.ticks - b.earliest_at!.ticks)[0];
  const earlier = windows
    .filter((w) => w.latest_at && w.latest_at.ticks < requestedTicks)
    .sort((a, b) => b.latest_at!.ticks - a.latest_at!.ticks)[0];
  if (later) fixes.push({ kind: 'earliest', at: later.earliest_at!.text });
  if (earlier?.latest_at) fixes.push({ kind: 'latest', at: earlier.latest_at.text });
  return fixes;
}

export function reasonsOf(explanation: unknown): string[] {
  const x = (explanation ?? {}) as Explanation & { max_delay?: { text: string } | null; max_delay_at?: { text: string } | null; invariants?: { location: string; invariant: string }[] };
  const out = (x.reasons ?? []).map((r) => (r.transition ? `${r.transition}: ${r.reason}` : r.reason));
  if (x.max_delay_at) out.push(`time can advance only until t = ${x.max_delay_at.text}${(x.invariants ?? []).length ? ` (invariant ${(x.invariants ?? []).map((i) => `${i.location}: ${i.invariant}`).join('; ')})` : ''}`);
  return out;
}
