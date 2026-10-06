/**
 * Predict › What-if & simulation: a scenario-engineering workspace for timed behaviour.
 *
 * Every formal answer comes from the runtime's POST /runtime/what-if (kernel semantics on
 * copies of the state): which events can occur, by which transitions, WHEN (exact admissible
 * delay windows, possibly several per event), why (the guard / invariant atoms that bound each
 * window), how far time may advance, what each scenario step does, and exactly where and why
 * a scenario becomes impossible. The browser only composes requests and displays results;
 * it computes no window, no enablement and no successor.
 */
import { useQuery } from '@tanstack/react-query';
import { AlertTriangle, ArrowDown, ArrowUp, CheckCircle2, CircleDot, Clock, Copy, FlaskConical, GitFork, Info, Plus, RotateCcw, Trash2, XCircle } from 'lucide-react';
import { useEffect, useMemo, useState } from 'react';
import { Link } from 'react-router-dom';
import { runtimeApi, useExecutions, useRuntimeModel, useRuntimePackage } from '@/runtime/client';
import { ticksToText } from '@/runtime/time';
import type {
  DelayInterval,
  EventAlternative,
  EventAvailability,
  LedgerRecord,
  RuntimeState,
  ScenarioStart,
  ScenarioStep,
  TwinIr,
  WhatIfResult,
  WhatIfStateSet,
  WhatIfStep,
} from '@/runtime/types';
import { Button, Callout, EmptyState, ErrorBlock, ModeBanner, Panel, RuntimeUnavailable, Segmented, StatusBadge } from '@/design';
import { useTwinScope } from '@/app/twinScope';
import { BehaviorGraph } from '@/features/behavior/BehaviorGraph';
import './whatif.css';

// ------------------------------------------------------------------ scenarios (viewer drafts)
interface Scenario {
  id: string;
  name: string;
  start: ScenarioStart;
  steps: ScenarioStep[];
}

const storeKey = (twinId: string) => `vts.scenarios.${twinId}`;
const newId = () => Math.random().toString(36).slice(2, 9);
const blank = (name: string): Scenario => ({ id: newId(), name, start: { kind: 'current' }, steps: [] });

function loadScenarios(twinId: string): Scenario[] {
  try {
    const v = JSON.parse(localStorage.getItem(storeKey(twinId)) ?? '[]');
    return Array.isArray(v) && v.length ? (v as Scenario[]) : [blank('Scenario A')];
  } catch {
    return [blank('Scenario A')];
  }
}

function useWhatIf(twinId: string, s: Scenario | undefined) {
  return useQuery({
    queryKey: ['runtime', twinId, 'what-if', s?.start, s?.steps],
    queryFn: () => runtimeApi(twinId).post<WhatIfResult>('/runtime/what-if', { start: s!.start, steps: s!.steps }),
    enabled: !!s,
    staleTime: Infinity,
    retry: false,
  });
}

// ------------------------------------------------------------------ presentation helpers
const rel = (t: { text: string }) => `+${t.text}`;
function intervalText(i: DelayInterval, unit: string) {
  return i.latest
    ? `${rel(i.earliest)} … ${rel(i.latest)} ${unit}`
    : `${rel(i.earliest)} ${unit} … no upper bound`;
}
function intervalAbs(i: DelayInterval, unit: string) {
  return i.latest_at ? `t = ${i.earliest_at.text} … ${i.latest_at.text} ${unit}` : `t ≥ ${i.earliest_at.text} ${unit}`;
}
const ORIGIN: Record<string, string> = { source_invariant: 'Source invariant', guard: 'Guard', target_invariant: 'Target invariant (after resets)' };

/** Horizontal timing bars of an event's windows (the text alongside is exact; the bar is a picture). */
function TimingBar({ intervals, horizon, maxDelay }: { intervals: DelayInterval[]; horizon: number; maxDelay: number | null }) {
  return (
    <div className="vts-tbar" aria-hidden="true">
      {intervals.map((i, k) => {
        const lo = Math.min(1, i.earliest.ticks / horizon);
        const hi = i.latest ? Math.min(1, i.latest.ticks / horizon) : 1;
        return <span key={k} className={`vts-tbar__band${i.latest ? '' : ' is-open'}`} style={{ left: `${lo * 100}%`, width: `${Math.max(0.6, (hi - lo) * 100)}%` }} />;
      })}
      {maxDelay !== null && maxDelay <= horizon && <span className="vts-tbar__deadline" style={{ left: `${(maxDelay / horizon) * 100}%` }} title="Deadline: the location invariant forbids waiting longer" />}
    </div>
  );
}

function Axis({ horizon, tpu, unit }: { horizon: number; tpu: number; unit: string }) {
  const steps = 4;
  return (
    <div className="vts-tbar-axis" aria-hidden="true">
      {Array.from({ length: steps + 1 }, (_, k) => (
        <span key={k} style={{ left: `${(k / steps) * 100}%` }}>+{Number(((horizon * k) / steps / tpu).toFixed(3))}{k === steps ? ` ${unit}` : ''}</span>
      ))}
    </div>
  );
}

/** "Why this window?": the derivation from the backend's factors for one alternative. */
function WindowDerivation({ alt, unit, clocks }: { alt: EventAlternative; unit: string; clocks: Record<string, { text: string }> }) {
  return (
    <div className="stack-sm">
      <div className="small">
        <span className="mono">{alt.transition}</span>: {alt.source} → <strong>{alt.target}</strong>
        {alt.resets.length > 0 && <span className="muted"> · resets {alt.resets.join(', ')}</span>}
      </div>
      {Object.keys(clocks).length > 0 && (
        <div className="xsmall muted">Current clocks: {Object.entries(clocks).map(([k, v]) => `${k} = ${v.text}`).join(', ')}</div>
      )}
      {alt.factors.length === 0 ? (
        <p className="xsmall muted">No clock constraint: the transition can fire at any time while it stays in this location.</p>
      ) : (
        <table className="vts-table vts-table--compact">
          <thead><tr><th>Constraint</th><th>Atom</th><th>Now</th><th>Allows a delay of</th></tr></thead>
          <tbody>
            {alt.factors.map((f, k) => (
              <tr key={k}>
                <td className="xsmall">{ORIGIN[f.origin] ?? f.origin}</td>
                <td className="mono small">{f.atom}</td>
                <td className="mono xsmall">{f.value_now.text}</td>
                <td className="small">
                  {f.never ? <span className="vts-neg">never</span>
                    : !f.depends_on_delay ? 'any (holds regardless of waiting)'
                    : f.min_delay && f.max_delay ? `${rel(f.min_delay)} … ${rel(f.max_delay)} ${unit}`
                    : f.min_delay ? `at least ${rel(f.min_delay)} ${unit}`
                    : f.max_delay ? `at most ${rel(f.max_delay)} ${unit}`
                    : 'any delay'}
                </td>
              </tr>
            ))}
          </tbody>
        </table>
      )}
      <div className="small">
        <strong>Therefore:</strong>{' '}
        {alt.window ? (
          <>earliest {rel(alt.window.earliest)}, {alt.window.latest ? <>latest {rel(alt.window.latest)}</> : 'no upper bound'} {unit}
            <span className="muted"> (t = {alt.window.earliest_at.text}{alt.window.latest_at ? ` … ${alt.window.latest_at.text}` : ' onwards'})</span></>
        ) : <span className="vts-neg">this transition cannot fire from this state</span>}
      </div>
    </div>
  );
}

/** Schedule an event: now, after a delay, or at an absolute logical time. */
function ScheduleForm({ event, now, unit, onAdd }: { event: EventAvailability; now: string; unit: string; onAdd: (s: ScenarioStep) => void }) {
  const [mode, setMode] = useState<'now' | 'after' | 'at'>(event.status === 'now' ? 'now' : 'after');
  const [value, setValue] = useState(event.status === 'later' && event.intervals[0] ? event.intervals[0].earliest.text : '0');
  const [via, setVia] = useState<string>('');
  const timeField = mode === 'now' ? {} : mode === 'after' ? { delay: value } : { at: value };
  const step: ScenarioStep = via ? { kind: 'event', transition: via, ...timeField, ...(mode === 'now' ? { delay: '0' } : {}) } : { kind: 'event', label: event.label, ...timeField, ...(mode === 'now' ? { delay: '0' } : {}) };
  return (
    <div className="vts-schedule">
      <Segmented label="When" value={mode} onChange={setMode} options={[{ id: 'now', label: 'Fire now' }, { id: 'after', label: 'After a delay' }, { id: 'at', label: 'At logical time' }]} />
      {mode !== 'now' && (
        <label className="vts-field">
          <span>{mode === 'after' ? `Delay from t = ${now} (${unit})` : `Absolute logical time (${unit})`}</span>
          <input className="vts-input mono" inputMode="decimal" value={value} onChange={(e) => setValue(e.target.value.trim())} />
        </label>
      )}
      <span className="xsmall muted">Legal: {event.intervals.length ? event.intervals.map((i) => intervalText(i, unit)).join('  ∪  ') : 'never from this state'}. The runtime checks the exact time when the step is evaluated.</span>
      {event.alternatives.length > 1 && (
        <label className="vts-field">
          <span>Transition (several can carry this event)</span>
          <select className="vts-select" value={via} onChange={(e) => setVia(e.target.value)}>
            <option value="">Any matching transition (keep every branch)</option>
            {event.alternatives.map((a) => <option key={`${a.member}-${a.transition}`} value={a.transition}>{a.transition} → {a.target}</option>)}
          </select>
        </label>
      )}
      <div className="row"><div className="grow" /><Button variant="primary" size="sm" icon={<Plus size={13} />} onClick={() => onAdd(step)}>Add to scenario</Button></div>
    </div>
  );
}

// ------------------------------------------------------------------ timeline
function stepTitle(s: ScenarioStep) {
  if (s.kind === 'delay') return `Advance time +${s.delay}`;
  const when = s.at !== undefined ? `at t = ${s.at}` : `after +${s.delay ?? '0'}`;
  return `${s.transition ?? s.label} ${when}`;
}

function TimelineStep({ i, step, result, unit, onRemove, onMove, onEdit, onPick }: {
  i: number; step: ScenarioStep; result: WhatIfStep | undefined; unit: string;
  onRemove: () => void; onMove: (d: -1 | 1) => void; onEdit: (s: ScenarioStep) => void; onPick: (transition: string) => void;
}) {
  const status = result?.status ?? 'not_evaluated';
  const Icon = status === 'ok' ? CheckCircle2 : status === 'invalid' ? XCircle : CircleDot;
  const timeKey = step.kind === 'delay' ? 'delay' : step.at !== undefined ? 'at' : 'delay';
  const timeValue = step.kind === 'delay' ? step.delay : step.at ?? step.delay ?? '0';
  return (
    <li className={`vts-tl__step is-${status}`}>
      <Icon size={16} className="vts-tl__icon" aria-label={status === 'ok' ? 'Valid' : status === 'invalid' ? 'Invalid' : 'Not evaluated'} />
      <div className="vts-tl__body">
        <div className="row-between">
          <strong className="small">{i + 1}. {stepTitle(step)}</strong>
          <span className="row" style={{ gap: 2 }}>
            <Button size="sm" variant="ghost" iconOnly icon={<ArrowUp size={13} />} onClick={() => onMove(-1)}>Move up</Button>
            <Button size="sm" variant="ghost" iconOnly icon={<ArrowDown size={13} />} onClick={() => onMove(1)}>Move down</Button>
            <Button size="sm" variant="ghost" iconOnly icon={<Trash2 size={13} />} onClick={onRemove}>Remove step</Button>
          </span>
        </div>
        <label className="row xsmall" style={{ gap: 6 }}>
          {timeKey === 'at' ? 'at t =' : 'delay +'}
          <input className="vts-input vts-input--xs mono" defaultValue={timeValue} aria-label="Step time"
            onBlur={(e) => { const v = e.target.value.trim(); if (v && v !== timeValue) onEdit({ ...step, [timeKey]: v } as ScenarioStep); }} /> {unit}
        </label>
        {result?.status === 'ok' && result.after && (
          <div className="xsmall muted">
            {result.branches?.length ? <>→ {result.branches.map((b) => b.target).join(' or ')} · </> : null}
            t = {result.after.time.text} {unit} · {result.after.configurations.map((c) => c.location).join(', ')}
            {result.propositions && result.propositions.length > 0 && <> · {result.propositions.map((p) => p.id).join(', ')}</>}
          </div>
        )}
        {result?.status === 'ok' && (result.branches?.length ?? 0) > 1 && (
          <div className="vts-branches">
            <span className="xsmall"><GitFork size={12} aria-hidden="true" /> {result.branches!.length} possible successors (kept as a set). Choose one:</span>
            {result.branches!.map((b) => <button key={b.transition} type="button" className="vts-chip vts-chip--sm" onClick={() => onPick(b.transition)}>{b.transition} → {b.target}</button>)}
          </div>
        )}
        {result?.status === 'invalid' && (
          <div className="vts-tl__invalid" role="alert">
            <strong>Step {i + 1} is invalid.</strong> {result.error?.message}
            {result.explanation?.reasons?.map((r, k) => (
              <div key={k} className="small">{r.transition ? <span className="mono">{r.transition}{r.target ? ` → ${r.target}` : ''}: </span> : null}{r.reason}.</div>
            ))}
            {result.explanation?.max_delay !== undefined && (
              <div className="small">
                {result.explanation.max_delay ? <>Latest permitted time: t = {result.explanation.max_delay_at?.text} {unit} (maximum delay +{result.explanation.max_delay.text}).</> : 'No finite deadline.'}
                {result.explanation.invariants?.map((v) => <div key={v.location} className="mono xsmall">{v.location}: {v.invariant}</div>)}
              </div>
            )}
          </div>
        )}
        {status === 'not_evaluated' && result?.note && <div className="xsmall muted">Not evaluated: {result.note}.</div>}
      </div>
    </li>
  );
}

// ------------------------------------------------------------------ start state
/** Where a scenario starts: the live state (default), the initial state, a recorded state, or a chosen one. */
function StartPicker({ twinId, ir, start, onChange, runningPackage }: {
  twinId: string; ir: TwinIr | undefined; start: ScenarioStart; onChange: (s: ScenarioStart) => void; runningPackage: string | undefined;
}) {
  const tpu = ir?.time.ticks_per_unit ?? 1000;
  const [kind, setKind] = useState<'current' | 'initial' | 'recorded' | 'chosen'>(start.kind === 'configurations' ? 'recorded' : start.kind);
  const execs = useExecutions(kind === 'recorded' ? twinId : null);
  const [session, setSession] = useState('');
  const [seq, setSeq] = useState('');
  const [loc, setLoc] = useState('');
  const [clocks, setClocks] = useState<Record<string, string>>({});
  const [time, setTime] = useState('0');
  const [note, setNote] = useState<string | null>(null);
  const exec = execs.data?.find((e) => e.session === session);

  const loadRecorded = async () => {
    setNote(null);
    const page = await runtimeApi(twinId).get<{ records: LedgerRecord[] }>('/runtime/ledger', { session, since: Number(seq), limit: 1 });
    const r = page.records[0];
    const st = r?.body.state_after;
    if (!r || !st?.length) { setNote(`Record #${seq} of this execution carries no semantic state.`); return; }
    onChange({
      kind: 'configurations',
      configurations: st.map((c) => ({
        location: c.location,
        clocks: Object.fromEntries(Object.entries(c.clocks).map(([k, v]) => [k, ticksToText(v, tpu)])),
        time: ticksToText(c.time, tpu),
      })),
    });
    if (exec && runningPackage && exec.package_hash !== runningPackage) {
      setNote('This execution ran a different package than the one running now; the scenario is evaluated with the running model.');
    } else {
      setNote(`Starting from record #${r.seq} (${r.body.kind}) of execution ${session.slice(0, 8)}…`);
    }
  };

  return (
    <div className="stack-sm">
      <label className="vts-field"><span>Start from</span>
        <select className="vts-select" value={kind} onChange={(e) => {
          const k = e.target.value as typeof kind;
          setKind(k);
          setNote(null);
          if (k === 'current' || k === 'initial') onChange({ kind: k });
        }}>
          <option value="current">Current live state (fork)</option>
          <option value="initial">Initial model state</option>
          <option value="recorded">A recorded state (execution ledger)</option>
          <option value="chosen">A chosen model state</option>
        </select>
      </label>
      {kind === 'recorded' && (
        <div className="stack-sm">
          <select className="vts-select" value={session} onChange={(e) => setSession(e.target.value)} aria-label="Execution">
            <option value="">Choose an execution…</option>
            {execs.data?.map((e) => <option key={e.session} value={e.session}>{e.session.slice(0, 10)}… · {e.records} records{e.current ? ' (current)' : ''}</option>)}
          </select>
          <div className="row" style={{ gap: 6 }}>
            <input className="vts-input mono" placeholder="record #" value={seq} onChange={(e) => setSeq(e.target.value.replace(/\D/g, ''))} aria-label="Ledger record number" style={{ width: 110 }} />
            <Button size="sm" disabled={!session || !seq} onClick={() => void loadRecorded()}>Use this state</Button>
          </div>
        </div>
      )}
      {kind === 'chosen' && ir && (
        <div className="stack-sm">
          <select className="vts-select" value={loc} onChange={(e) => setLoc(e.target.value)} aria-label="Location">
            <option value="">Choose a location…</option>
            {ir.locations.map((l) => <option key={l.id} value={l.id}>{l.id}</option>)}
          </select>
          <div className="row-wrap" style={{ gap: 6 }}>
            {ir.clocks.map((c) => (
              <label key={c} className="row xsmall" style={{ gap: 4 }}><span className="mono">{c} =</span>
                <input className="vts-input vts-input--xs mono" value={clocks[c] ?? '0'} onChange={(e) => setClocks({ ...clocks, [c]: e.target.value.trim() })} />
              </label>
            ))}
            <label className="row xsmall" style={{ gap: 4 }}>t =<input className="vts-input vts-input--xs mono" value={time} onChange={(e) => setTime(e.target.value.trim())} /></label>
          </div>
          <Button size="sm" disabled={!loc} onClick={() => { setNote('The runtime checks that the state satisfies its location invariant.'); onChange({ kind: 'configurations', configurations: [{ location: loc, clocks: Object.fromEntries(ir.clocks.map((c) => [c, clocks[c] ?? '0'])), time }] }); }}>Use this state</Button>
        </div>
      )}
      {note && <p className="xsmall muted">{note}</p>}
    </div>
  );
}

// ------------------------------------------------------------------ main
function graphState(ir: TwinIr, s: WhatIfStateSet, enabled: EventAvailability[]): RuntimeState {
  return {
    session: '',
    time: s.time,
    configurations: s.configurations.map((c) => ({ location: c.location, clocks: c.clocks, time: c.time })),
    deterministic: s.deterministic,
    propositions: [],
    enabled: enabled.flatMap((e) => e.alternatives.filter((a) => a.window).map((a) => ({
      member: a.member, transition: a.transition, label: e.label, source: a.source, target: a.target, guard: a.guard, resets: a.resets,
      window: { earliest: a.window!.earliest, latest: a.window!.latest }, enabled_now: a.enabled_now,
    }))),
    deadline: null,
    ledger: { records: 0, head: '' },
    failed: false,
    closed: false,
    model: { id: ir.model.id, version: ir.model.version, ir_sha256: '' },
  } as RuntimeState;
}

export default function WhatIfPage() {
  const { twin, runtimeConnected } = useTwinScope()!;
  const unit = twin.presentation.timeUnit ?? '';
  const model = useRuntimeModel(runtimeConnected ? twin.id : null);
  const pkgHash = useRuntimePackage(runtimeConnected ? twin.id : null).data?.package_hash;
  const [scenarios, setScenarios] = useState<Scenario[]>(() => loadScenarios(twin.id));
  const [activeId, setActiveId] = useState(scenarios[0]!.id);
  const [openEvent, setOpenEvent] = useState<string | null>(null);
  const [delay, setDelay] = useState('1');
  const [showCompare, setShowCompare] = useState(false);
  const active = scenarios.find((s) => s.id === activeId) ?? scenarios[0]!;
  const q = useWhatIf(twin.id, runtimeConnected ? active : undefined);
  const r = q.data;

  useEffect(() => {
    try { localStorage.setItem(storeKey(twin.id), JSON.stringify(scenarios)); } catch { /* drafts only */ }
  }, [scenarios, twin.id]);

  const update = (patch: Partial<Scenario>) => setScenarios((all) => all.map((s) => (s.id === active.id ? { ...s, ...patch } : s)));
  const setSteps = (steps: ScenarioStep[]) => update({ steps });
  const addStep = (s: ScenarioStep) => { setSteps([...active.steps, s]); setOpenEvent(null); };
  const fork = () => {
    const name = `Scenario ${String.fromCharCode(65 + scenarios.length)}`;
    const copy = { ...active, id: newId(), name, steps: [...active.steps] };
    setScenarios((all) => [...all, copy]);
    setActiveId(copy.id);
  };
  const remove = () => {
    if (scenarios.length === 1) { update({ steps: [], start: { kind: 'current' } }); return; }
    const rest = scenarios.filter((s) => s.id !== active.id);
    setScenarios(rest);
    setActiveId(rest[0]!.id);
  };

  const final = r?.final;
  const tpu = model.data?.time.ticks_per_unit ?? 1000;
  const horizon = useMemo(() => {
    if (!final) return 10 * tpu;
    const xs = [final.max_delay?.ticks ?? 0, ...final.availability.flatMap((e) => e.intervals.flatMap((i) => [i.earliest.ticks, i.latest?.ticks ?? 0]))];
    return Math.max(tpu, Math.max(...xs) * 1.15 || 10 * tpu);
  }, [final, tpu]);
  const groups = {
    now: final?.availability.filter((e) => e.status === 'now') ?? [],
    later: final?.availability.filter((e) => e.status === 'later') ?? [],
    blocked: final?.availability.filter((e) => e.status === 'blocked') ?? [],
  };
  const clocks = final?.state.configurations[0]?.clocks ?? {};

  if (!runtimeConnected) return <div className="vts-page"><Panel title="What-if & simulation"><RuntimeUnavailable what="Scenarios are evaluated by the twin's kernel." /></Panel></div>;

  const eventRow = (e: EventAvailability) => {
    const open = openEvent === e.label;
    return (
      <li key={e.label} className={`vts-ev${open ? ' is-open' : ''}`}>
        <button type="button" className="vts-ev__head" onClick={() => setOpenEvent(open ? null : e.label)} aria-expanded={open}>
          <span className="vts-ev__name mono">{e.label}</span>
          <span className="vts-ev__target xsmall muted">→ {[...new Set(e.alternatives.map((a) => a.target))].join(' / ')}</span>
          <span className="vts-ev__when small">
            {e.status === 'now' && e.intervals[0]?.earliest.ticks === 0 && !e.intervals[0]?.latest ? 'allowed now, no deadline'
              : e.intervals.length ? e.intervals.map((i) => intervalText(i, unit)).join(' ∪ ') : 'not from this state'}
          </span>
        </button>
        {e.intervals.length > 0 && <TimingBar intervals={e.intervals} horizon={horizon} maxDelay={final?.max_delay?.ticks ?? null} />}
        {open && (
          <div className="vts-ev__detail stack">
            {e.intervals.length > 0 && <div className="xsmall muted">Absolute logical time: {e.intervals.map((i) => intervalAbs(i, unit)).join('  ∪  ')}</div>}
            {e.alternatives.length > 1 && <Callout tone="info" title={`${e.alternatives.length} transitions carry this event`}>Each has its own guard and window; the runtime keeps every matching branch unless you choose one.</Callout>}
            {e.alternatives.map((a) => <WindowDerivation key={`${a.member}-${a.transition}`} alt={a} unit={unit} clocks={clocks} />)}
            {e.status !== 'blocked' && <ScheduleForm event={e} now={final!.state.time.text} unit={unit} onAdd={addStep} />}
          </div>
        )}
      </li>
    );
  };

  return (
    <div className="vts-page stack">
      <ModeBanner mode="simulation" actions={<Link className="small" to="../predictions">Bounded predictions</Link>}>
        This is a simulation on a copy of the twin's state. Nothing here changes the live twin or its ledger.
      </ModeBanner>

      <div className="vts-scenarios" role="tablist" aria-label="Scenarios">
        {scenarios.map((s) => (
          <button key={s.id} type="button" role="tab" aria-selected={s.id === active.id} className="vts-chip" aria-pressed={s.id === active.id} onClick={() => setActiveId(s.id)}>
            <FlaskConical size={13} aria-hidden="true" /> {s.name} <span className="xsmall muted">{s.steps.length} step(s)</span>
          </button>
        ))}
        <Button size="sm" icon={<Copy size={13} />} onClick={fork}>Fork</Button>
        <Button size="sm" icon={<Plus size={13} />} onClick={() => { const n = blank(`Scenario ${String.fromCharCode(65 + scenarios.length)}`); setScenarios((a) => [...a, n]); setActiveId(n.id); }}>New</Button>
        {scenarios.length > 1 && <Button size="sm" onClick={() => setShowCompare((v) => !v)}>{showCompare ? 'Hide comparison' : 'Compare scenarios'}</Button>}
        <div className="grow" />
        <Button size="sm" icon={<RotateCcw size={13} />} onClick={() => update({ steps: [] })}>Reset steps</Button>
        <Button size="sm" variant="ghost" icon={<Trash2 size={13} />} onClick={remove}>Delete scenario</Button>
      </div>

      {showCompare && <CompareScenarios twinId={twin.id} scenarios={scenarios} unit={unit} />}

      <div className="vts-whatif">
        <Panel title="Scenario" subtitle="Ordered by logical time; edit a step and later steps are re-checked" className="vts-whatif__left">
          <div className="stack">
            <StartPicker key={active.id} twinId={twin.id} ir={model.data} start={active.start} onChange={(start) => update({ start })} runningPackage={pkgHash} />
            {r && (
              <div className="vts-tl__start xsmall">
                <strong>t = {r.start.state.time.text} {unit}</strong> · starts in {r.start.state.configurations.map((c) => c.location).join(' or ')}
              </div>
            )}
            {active.steps.length === 0 ? (
              <p className="small muted">No steps yet. Choose an event on the right, or advance time.</p>
            ) : (
              <ol className="vts-tl">
                {active.steps.map((s, i) => (
                  <TimelineStep
                    key={i}
                    i={i}
                    step={s}
                    result={r?.steps[i]}
                    unit={unit}
                    onRemove={() => setSteps(active.steps.filter((_, k) => k !== i))}
                    onMove={(d) => { const j = i + d; if (j < 0 || j >= active.steps.length) return; const next = [...active.steps]; [next[i], next[j]] = [next[j]!, next[i]!]; setSteps(next); }}
                    onEdit={(n) => setSteps(active.steps.map((x, k) => (k === i ? n : x)))}
                    onPick={(t) => setSteps(active.steps.map((x, k) => (k === i && x.kind === 'event' ? { kind: 'event', transition: t, ...(x.at !== undefined ? { at: x.at } : { delay: x.delay ?? '0' }) } : x)))}
                  />
                ))}
              </ol>
            )}
            <div className="vts-advance">
              <label className="vts-field"><span>Advance time by ({unit})</span>
                <input className="vts-input mono" inputMode="decimal" value={delay} onChange={(e) => setDelay(e.target.value.trim())} />
              </label>
              <Button size="sm" icon={<Clock size={13} />} disabled={!delay} onClick={() => addStep({ kind: 'delay', delay })}>Advance</Button>
            </div>
            {final && (
              <p className="xsmall muted">
                {final.max_delay ? <>Maximum legal delay from the scenario's state: <strong>+{final.max_delay.text} {unit}</strong> (until t = {final.max_delay_at?.text}).</> : 'No finite upper bound on waiting from current model constraints.'}
              </p>
            )}
          </div>
        </Panel>

        <Panel title="Resulting state" subtitle={r ? `t = ${r.final.state.time.text} ${unit}${r.first_invalid !== null ? ` · stops before step ${r.first_invalid + 1}` : ''}` : undefined} className="vts-whatif__center">
          {q.error ? <ErrorBlock error={q.error} /> : model.data && final ? (
            <BehaviorGraph
              ir={model.data}
              state={graphState(model.data, final.state, final.availability)}
              presentation={twin.presentation}
              recent={(r?.steps.filter((s) => s.status === 'ok').slice(-1)[0]?.branches ?? []).map((b) => b.transition)}
              selection={null}
              onSelect={() => undefined}
              height={460}
            />
          ) : <p className="small muted">Evaluating…</p>}
        </Panel>

        <div className="vts-whatif__right stack">
          <Panel title="State">
            {final ? (
              <div className="stack-sm small">
                <div><span className="muted">Location</span> <strong>{final.state.configurations.map((c) => c.location).join(' or ')}</strong>{!final.state.deterministic && <StatusBadge tone="info" label="several possible" />}</div>
                <div className="vts-clocks">
                  <span className="muted xsmall">CLOCKS</span>
                  {final.state.configurations.map((c, k) => (
                    <div key={k} className="mono small">{Object.entries(c.clocks).map(([n, v]) => <span key={n} className="vts-clock">{n} = {v.text}</span>)}</div>
                  ))}
                  {final.invariants.map((v) => <div key={v.location} className="xsmall muted">invariant of {v.location}: <span className="mono">{v.invariant}</span></div>)}
                </div>
                <div><span className="muted xsmall">PROPOSITIONS</span> <div className="mono xsmall">{final.propositions.map((p) => p.id).join(', ') || 'none'}</div></div>
              </div>
            ) : <p className="small muted">…</p>}
          </Panel>
          <Panel title="Events" subtitle="When each event may occur from this state (exact, from the kernel)" flush>
            {final ? (
              <div className="vts-events">
                <Axis horizon={horizon} tpu={tpu} unit={unit} />
                {groups.now.length > 0 && <><h3 className="vts-ev-group">Available now</h3><ul className="vts-ev-list">{groups.now.map(eventRow)}</ul></>}
                {groups.later.length > 0 && <><h3 className="vts-ev-group">Available later</h3><ul className="vts-ev-list">{groups.later.map(eventRow)}</ul></>}
                {(groups.blocked.length > 0 || final.unavailable.length > 0) && (
                  <>
                    <h3 className="vts-ev-group">Not available from this state</h3>
                    <ul className="vts-ev-list">
                      {groups.blocked.map(eventRow)}
                      {final.unavailable.map((u) => (
                        <li key={u.label} className="vts-ev is-off">
                          <div className="vts-ev__head" style={{ cursor: 'default' }}>
                            <span className="vts-ev__name mono">{u.label}</span>
                            <span className="vts-ev__when small muted">needs another event first: only from {u.from_locations.join(', ')}</span>
                          </div>
                        </li>
                      ))}
                    </ul>
                  </>
                )}
                {final.availability.length === 0 && final.unavailable.length === 0 && <EmptyState compact title="No events" />}
              </div>
            ) : <p className="small muted" style={{ padding: 12 }}>…</p>}
          </Panel>
          <p className="xsmall muted row" style={{ gap: 6, alignItems: 'flex-start' }}><Info size={12} aria-hidden="true" style={{ flex: 'none', marginTop: 2 }} /> Property monitors are not evaluated in what-if scenarios in this version; propositions are shown for every step.</p>
        </div>
      </div>
    </div>
  );
}

/** Scenario comparison (A vs B …): where each ends, how, and what it can do next. */
function CompareScenarios({ twinId, scenarios, unit }: { twinId: string; scenarios: Scenario[]; unit: string }) {
  return (
    <Panel title="Scenario comparison" flush>
      <div className="vts-table-wrap">
        <table className="vts-table">
          <thead><tr><th>Scenario</th><th>Valid</th><th>Ends at</th><th>Location</th><th>Propositions</th><th>Available now</th><th>Deadline</th></tr></thead>
          <tbody>{scenarios.map((s) => <CompareRow key={s.id} twinId={twinId} s={s} unit={unit} />)}</tbody>
        </table>
      </div>
    </Panel>
  );
}

function CompareRow({ twinId, s, unit }: { twinId: string; s: Scenario; unit: string }) {
  const q = useWhatIf(twinId, s);
  const r = q.data;
  return (
    <tr>
      <td><strong>{s.name}</strong> <span className="xsmall muted">{s.steps.length} step(s)</span></td>
      <td>{!r ? '…' : r.first_invalid === null ? <StatusBadge tone="ok" label="Valid" /> : <StatusBadge tone="critical" icon={AlertTriangle} label={`Invalid at step ${r.first_invalid + 1}`} />}</td>
      <td className="mono small">{r ? `t = ${r.final.state.time.text} ${unit}` : ''}</td>
      <td className="small">{r?.final.state.configurations.map((c) => c.location).join(' or ')}</td>
      <td className="mono xsmall">{r?.final.propositions.map((p) => p.id).join(', ')}</td>
      <td className="mono xsmall">{r?.final.availability.filter((e) => e.status === 'now').map((e) => e.label).join(', ')}</td>
      <td className="small">{r ? (r.final.max_delay ? `+${r.final.max_delay.text} ${unit}` : 'none') : ''}</td>
    </tr>
  );
}
