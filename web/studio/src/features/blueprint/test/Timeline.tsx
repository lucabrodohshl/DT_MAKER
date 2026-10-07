/**
 * Scenario timeline: the steps of a scenario on the logical time axis, one lane per kind
 * (events, world, telemetry, expectations), coloured by their last run or timing result, plus
 * the legal windows of the event being scheduled (intervals reported by the kernel; an open
 * end is drawn dashed). Positions are presentation only.
 */
import clsx from 'clsx';
import { useMemo } from 'react';
import type { ScenarioStep, TimingInterval } from '@/api/types';
import { stepText, stepTime, timeNum } from './scenario';

export type MarkState = 'pass' | 'fail' | 'invalid' | 'ok' | 'pending' | 'refused-ok';

const LANES: { id: string; label: string; kinds: ScenarioStep['kind'][] }[] = [
  { id: 'event', label: 'Events', kinds: ['event', 'delay'] },
  { id: 'world', label: 'World', kinds: ['world'] },
  { id: 'observe', label: 'Telemetry', kinds: ['observe'] },
  { id: 'expect', label: 'Expect', kinds: ['expect'] },
];

function niceStep(span: number): number {
  const raw = span / 8;
  const p = 10 ** Math.floor(Math.log10(raw || 1));
  const m = raw / p;
  return (m < 1.5 ? 1 : m < 3.5 ? 2 : m < 7.5 ? 5 : 10) * p;
}

export function ScenarioTimeline({
  steps,
  states,
  selected,
  onSelect,
  now,
  windows,
  windowLabel,
  unit,
}: {
  steps: ScenarioStep[];
  states: Map<string, MarkState>;
  selected: string | null;
  onSelect: (id: string) => void;
  /** Logical time at the insertion point (end of the evaluated prefix). */
  now: string | null;
  windows: TimingInterval[] | null;
  windowLabel: string | null;
  unit: string;
}) {
  const { max, ticks } = useMemo(() => {
    let m = 10;
    for (const s of steps) m = Math.max(m, timeNum(stepTime(s)) ?? 0);
    m = Math.max(m, timeNum(now) ?? 0);
    for (const w of windows ?? []) m = Math.max(m, timeNum(w.latest_at?.text) ?? timeNum(w.earliest_at.text) ?? 0);
    const span = m * 1.08;
    const st = niceStep(span);
    const t: number[] = [];
    for (let x = 0; x <= span + 1e-9; x += st) t.push(Number(x.toFixed(6)));
    return { max: span, ticks: t };
  }, [steps, now, windows]);
  const pct = (v: number) => `${Math.min(100, Math.max(0, (v / max) * 100))}%`;
  const nowN = timeNum(now);
  // Steps without a time of their own sit at the time of the previous timed step.
  const placed = useMemo(
    () =>
      steps.reduce<{ s: ScenarioStep; t: number }[]>((acc, s) => {
        const t = timeNum(stepTime(s));
        acc.push({ s, t: t ?? acc[acc.length - 1]?.t ?? 0 });
        return acc;
      }, []),
    [steps],
  );
  return (
    <div className="vts-sc-tl" role="group" aria-label={`Scenario timeline, logical time in ${unit}`}>
      {LANES.map((lane) => (
        <div key={lane.id} className="vts-sc-tl__lane">
          <span className="vts-sc-tl__label">{lane.label}</span>
          <div className="vts-sc-tl__track">
            {placed
              .filter((p) => lane.kinds.includes(p.s.kind))
              .map((p) => (
                <button
                  key={p.s.id}
                  type="button"
                  className={clsx('vts-sc-tl__mark', `is-${p.s.kind}`, `st-${states.get(p.s.id) ?? 'pending'}`, selected === p.s.id && 'is-selected')}
                  style={{ left: pct(p.t) }}
                  title={`t = ${stepTime(p.s) ?? '—'}: ${stepText(p.s)}`}
                  aria-label={`Step ${p.s.id} at t = ${stepTime(p.s) ?? 'previous time'}: ${stepText(p.s)}`}
                  onClick={() => onSelect(p.s.id)}
                />
              ))}
            {nowN !== null && <span className="vts-sc-tl__now" style={{ left: pct(nowN) }} aria-hidden="true" />}
          </div>
        </div>
      ))}
      {windows && windowLabel && (
        <div className="vts-sc-tl__lane">
          <span className="vts-sc-tl__label mono" title={windowLabel}>
            {windowLabel}
          </span>
          <div className="vts-sc-tl__track" aria-label={`Legal times of ${windowLabel}`}>
            {windows.map((w, i) => {
              const a = timeNum(w.earliest_at.text) ?? 0;
              const b = w.latest_at ? (timeNum(w.latest_at.text) ?? a) : max;
              return (
                <span
                  key={i}
                  className={clsx('vts-sc-tl__iv', !w.latest_at && 'is-open-end', a === b && 'is-point')}
                  style={{ left: pct(a), width: a === b ? undefined : `calc(${pct(b)} - ${pct(a)})` }}
                  title={`t ∈ [${w.earliest_at.text}, ${w.latest_at ? w.latest_at.text : '∞'})`}
                />
              );
            })}
            {nowN !== null && <span className="vts-sc-tl__now" style={{ left: pct(nowN) }} aria-hidden="true" />}
          </div>
        </div>
      )}
      <div className="vts-sc-tl__lane vts-sc-tl__axis" aria-hidden="true">
        <span className="vts-sc-tl__label" />
        <div className="vts-sc-tl__track">
          {ticks.map((t) => (
            <span key={t} className="vts-sc-tl__tick" style={{ left: pct(t) }}>
              {t}
            </span>
          ))}
          {nowN !== null && (
            <span className="vts-sc-tl__nowlabel" style={{ left: pct(nowN) }}>
              t = {now}
            </span>
          )}
        </div>
      </div>
    </div>
  );
}
