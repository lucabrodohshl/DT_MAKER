/**
 * Side panels of the drone mission view: operational state, planner
 * candidates and the event timeline. Every value shown here is read from the
 * runtime (kernel state, ledger, planner episodes, telemetry); the panels only
 * format it.
 */
import { CheckCircle2, CircleSlash, ShieldAlert, ShieldCheck, XCircle } from 'lucide-react';
import { useMemo, useState } from 'react';
import type { TwinDetail } from '@/api/types';
import { Button, HashChip, KeyValue, StatusBadge } from '@/design';
import type { RuntimeApi } from '@/runtime/client';
import type { LedgerRecord, LedgerVerification, RuntimeState } from '@/runtime/types';
import { isPlan, mm, timelineEntry, type EpisodeJson, type MissionJson, type PlanJson, type TelemetryJson } from './model';

const ticksText = (ticks: number | undefined, tpu: number) => (ticks === undefined ? '—' : (ticks / tpu).toFixed(1));

function stateTone(twin: TwinDetail, location: string) {
  return twin.presentation.states?.[location]?.tone ?? 'neutral';
}

function stateLabel(twin: TwinDetail, location: string) {
  return twin.presentation.states?.[location]?.label ?? location;
}

/** Operational panel: semantic state, kernel facts, telemetry, plan, trust. */
export function OpsPanel({
  twin,
  state,
  location,
  telemetry,
  mission,
  activePlan,
  runtime,
  live,
}: {
  twin: TwinDetail;
  state: RuntimeState | null;
  location: string;
  telemetry: TelemetryJson | null;
  mission: MissionJson | null;
  activePlan: PlanJson | null;
  runtime: RuntimeApi;
  live: boolean;
}) {
  const tpu = twin.ticksPerUnit || 1000;
  const [verify, setVerify] = useState<{ result?: LedgerVerification; error?: string; running: boolean }>({ running: false });
  const runVerify = async () => {
    setVerify({ running: true });
    try {
      const result = await runtime.post<LedgerVerification>('/runtime/ledger/verify', { session: state?.session });
      setVerify({ result, running: false });
    } catch (e) {
      setVerify({ error: e instanceof Error ? e.message : String(e), running: false });
    }
  };
  const conformance = state?.conformance;
  const target = mission?.targets.find((t) => `target:${t.id}` === (activePlan?.goal ?? mission?.goal));
  const speed = telemetry?.speed_mm_s ?? Math.hypot(telemetry?.vx_mm_s ?? 0, telemetry?.vy_mm_s ?? 0);
  return (
    <div className="stack-sm">
      <div className="drone-state">
        <span className="small muted">Semantic state (kernel)</span>
        <StatusBadge tone={stateTone(twin, location)} label={location ? `${stateLabel(twin, location)} · ${location}` : '—'} size="lg" />
        {state && (
          <span className="small muted">
            committed at t = {state.time.text} {twin.presentation.timeUnit ?? 's'} (logical time of the last kernel step)
          </span>
        )}
      </div>
      {live && state && (
        <KeyValue
          compact
          items={[
            ['Clocks', <span key="c" className="mono small">{Object.entries(state.configurations[0]?.clocks ?? {}).map(([k, v]) => `${k}=${v.text}`).join('  ')}</span>],
            ['Last transition', state.last_transition ? <span key="t" className="mono small">#{state.last_transition.seq} {state.last_transition.from} → {state.last_transition.to} ({state.last_transition.label})</span> : '—'],
            ['Enabled now', <span key="e" className="mono small">{state.enabled.filter((e) => e.enabled_now).map((e) => e.label).join(', ') || '—'}</span>],
            ['Deadline', state.deadline ? `t ≤ ${state.deadline.text} ${twin.presentation.timeUnit ?? 's'}` : 'none'],
            ['Propositions', <span key="p" className="mono small">{state.propositions.map((p) => p.id).join(', ') || '—'}</span>],
          ]}
        />
      )}
      <KeyValue
        compact
        items={[
          ['Battery', telemetry ? `${(telemetry.battery_permille / 10).toFixed(1)} % (${(telemetry.energy_mwh / 1000).toFixed(2)} Wh)` : '—'],
          ['Position', telemetry ? `x ${mm(telemetry.x_mm)} m · y ${mm(telemetry.y_mm)} m · alt ${mm(telemetry.alt_mm, 2)} m` : '—'],
          ['Speed', telemetry ? `${(speed / 1000).toFixed(2)} m/s` : '—'],
          ['Flight controller', telemetry ? <span key="m" className="mono small">{telemetry.mode}</span> : '—'],
          ['Telemetry time', telemetry ? `t = ${ticksText(telemetry.at, tpu)} s (PT logical time)` : '—'],
          ['Target', target ? `${target.id} — ${target.name}` : (mission?.goal === 'home' || activePlan?.goal === 'home') ? 'Home pad' : '—'],
          ['Current plan', activePlan ? `route ${activePlan.id}${activePlan.candidate ? ` (candidate ${activePlan.candidate}, ${activePlan.profile})` : ''}` : 'none'],
          ['Plan length / cost', activePlan ? `${mm(activePlan.length_mm)} m / ${mm(activePlan.objective_mm ?? activePlan.cost_mm)}` : '—'],
        ]}
      />
      {live && state && (
        <div className="stack-sm">
          <div className="row-wrap">
            {conformance?.status === 'conformant' ? (
              <StatusBadge tone="ok" icon={ShieldCheck} label="Conformant" title={conformance.definition} />
            ) : conformance ? (
              <StatusBadge tone="critical" icon={ShieldAlert} label="Conformance violated" title={conformance.first_violation} />
            ) : (
              <StatusBadge tone="neutral" label="Conformance unknown" />
            )}
            {conformance && (
              <span className="small muted">
                {conformance.observations} observations explained, {conformance.observations_rejected} rejected, {conformance.alarms} alarms
              </span>
            )}
          </div>
          <KeyValue
            compact
            items={[
              ['Package', <HashChip key="p" value={state.package_hash} label="package hash" />],
              ['Model', state.model ? `${state.model.id} ${state.model.version}` : '—'],
              ['Ledger', <span key="l" className="row"><span className="small">{state.ledger.records} records, head</span><HashChip value={state.ledger.head} label="ledger head" /></span>],
            ]}
          />
          <div className="row-wrap">
            <Button size="sm" onClick={() => void runVerify()} loading={verify.running}>Verify ledger</Button>
            {verify.result &&
              (verify.result.valid ? (
                <StatusBadge tone="ok" icon={CheckCircle2} label={`Ledger valid (${verify.result.records} records)`} />
              ) : (
                <StatusBadge tone="critical" icon={XCircle} label={`Ledger INVALID at line ${verify.result.issues[0]?.line ?? '?'}`} title={verify.result.issues[0]?.message} />
              ))}
            {verify.error && <StatusBadge tone="critical" label="Verification unavailable" title={verify.error} />}
          </div>
        </div>
      )}
    </div>
  );
}

/** Candidates of the latest planning episode: geometric feasibility vs behavioural admissibility. */
export function CandidatesPanel({
  episode,
  highlight,
  onHighlight,
}: {
  episode: EpisodeJson | null;
  highlight: string | null;
  onHighlight: (label: string | null) => void;
}) {
  if (!episode) return <p className="small muted">No planning episode yet. Episodes run at mission start, after a route is invalidated and after each inspection.</p>;
  return (
    <div className="stack-sm">
      <p className="small muted">
        Episode {episode.id} at t = {(episode.at / 1000).toFixed(1)} s — goal <span className="mono">{episode.goal}</span>; {episode.reason}.
        {episode.decision && <> Supports the decision <span className="mono">{episode.decision}</span>, which the kernel then decides.</>}
      </p>
      <div className="drone-table-wrap">
        <table className="drone-table">
          <thead>
            <tr>
              <th scope="col">Plan</th>
              <th scope="col">Profile</th>
              <th scope="col">Length</th>
              <th scope="col">Objective</th>
              <th scope="col">Energy</th>
              <th scope="col">Geometrically feasible</th>
              <th scope="col">Behaviourally admissible</th>
              <th scope="col">Status</th>
            </tr>
          </thead>
          <tbody>
            {episode.candidates.map((c) => {
              const plan = c.found && isPlan(c.plan) ? c.plan : null;
              return (
                <tr
                  key={c.label}
                  className={highlight === c.label ? 'is-highlighted' : undefined}
                  onMouseEnter={() => onHighlight(c.label)}
                  onMouseLeave={() => onHighlight(null)}
                  onFocus={() => onHighlight(c.label)}
                  tabIndex={0}
                >
                  <td className="strong">Plan {c.label}{plan ? <span className="muted small"> · route {plan.id}</span> : null}</td>
                  <td className="small">{c.profile}</td>
                  <td>{plan ? `${mm(plan.length_mm)} m` : '—'}</td>
                  <td>{plan ? mm(plan.objective_mm ?? plan.cost_mm) : '—'}</td>
                  <td>{plan ? `${(plan.energy_mwh / 1000).toFixed(2)} Wh` : '—'}</td>
                  <td>
                    {!c.found ? (
                      <StatusBadge tone="neutral" icon={CircleSlash} label="No route" title={c.failure} />
                    ) : c.geometric.ok ? (
                      <StatusBadge tone="ok" label="Feasible" title="Independent dense-sampling validation against the twin-known map" />
                    ) : (
                      <StatusBadge tone="critical" label="Infeasible" title={c.geometric.issues.join('; ')} />
                    )}
                  </td>
                  <td>
                    {!c.behavioural.checked ? (
                      <StatusBadge tone="neutral" label="Not checked" title={c.behavioural.detail || c.failure} />
                    ) : c.behavioural.ok ? (
                      <StatusBadge tone="formal" label="Admitted by kernel" title={c.behavioural.detail} />
                    ) : (
                      <StatusBadge tone="critical" label="Refused by kernel" title={c.behavioural.detail} />
                    )}
                  </td>
                  <td>
                    {c.selected ? (
                      <StatusBadge tone="ok" icon={CheckCircle2} label="Selected" />
                    ) : c.duplicate_of ? (
                      <span className="small muted">same as {c.duplicate_of}</span>
                    ) : (
                      <span className="small muted">not selected</span>
                    )}
                  </td>
                </tr>
              );
            })}
          </tbody>
        </table>
      </div>
      <p className="small muted">
        Geometric feasibility is checked by the runtime's validator against the twin-known map; behavioural admissibility is
        the semantic kernel simulating the plan's event schedule on a copy of the twin state. A plan must pass both.
      </p>
    </div>
  );
}

/** Mission timeline, built from the execution's ledger records (each entry links to its record). */
export function Timeline({
  records,
  twin,
  facility,
  onSelect,
}: {
  records: LedgerRecord[];
  twin: TwinDetail;
  facility: { at: number; text: string }[];
  onSelect?: (seq: number) => void;
}) {
  const [all, setAll] = useState(false);
  const label = (l: string) => twin.presentation.events?.[l]?.label ?? l;
  const tone = (location: string) => twin.presentation.states?.[location]?.tone ?? 'info';
  const entries = useMemo(
    () =>
      records
        .map((r) => timelineEntry(r, label, tone))
        .filter((e): e is NonNullable<typeof e> => !!e && (all || !e.minor))
        .reverse(),
    // eslint-disable-next-line react-hooks/exhaustive-deps
    [records, all, twin],
  );
  return (
    <div className="stack-sm">
      <div className="row-between">
        <span className="small muted">{entries.length} entries from the tamper-evident ledger, newest first</span>
        <label className="row small">
          <input type="checkbox" checked={all} onChange={(e) => setAll(e.target.checked)} /> show waypoints and commands
        </label>
      </div>
      {facility.length > 0 && (
        <div className="drone-facility small">
          <strong>Physical world (observer only):</strong> {facility.map((f) => `t=${(f.at / 1000).toFixed(1)} s: ${f.text}`).join(' · ')}
        </div>
      )}
      <ol className="drone-timeline">
        {entries.map((e) => (
          <li key={e.seq} className={`drone-timeline__item tone-${e.tone}`}>
            <button type="button" className="drone-timeline__btn" onClick={() => onSelect?.(e.seq)} title={`Ledger record #${e.seq}`}>
              <span className="drone-timeline__time mono">t={(e.at / 1000).toFixed(1)}s</span>
              <span className="drone-timeline__title">{e.title}</span>
              <span className="drone-timeline__seq mono">#{e.seq}</span>
            </button>
            {e.detail && <div className="drone-timeline__detail small muted">{e.detail}</div>}
          </li>
        ))}
      </ol>
    </div>
  );
}
