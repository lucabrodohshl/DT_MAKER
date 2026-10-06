/**
 * Prediction over verified behaviour. Every result is computed by the kernel on a
 * COPY of the committed state (POST /runtime/predict, /runtime/simulate) and the
 * planner's episodes (GET /planner/episodes). The UI uses precise language:
 * "admissible by model", "predicted", "candidate", "selected" — never "safe".
 */
import { useMutation, useQuery } from '@tanstack/react-query';
import { ChevronRight, CopyPlus, Play, Plus, RotateCcw, Trash2 } from 'lucide-react';
import { useEffect, useMemo, useState } from 'react';
import { useSearchParams } from 'react-router-dom';
import { ApiError } from '@/api/client';
import type { TwinDetail } from '@/api/types';
import {
  Button,
  Callout,
  EmptyState,
  ErrorBlock,
  KeyValue,
  LogicalTimeText,
  ModeBanner,
  Panel,
  RuntimeUnavailable,
  StatusBadge,
  Tabs,
} from '@/design';
import { useAssetContext } from '@/features/assets/AssetLayout';
import { predict, runtimeApi, useRuntimeModel, useRuntimeState } from '@/runtime/client';
import type { PlanningEpisode, PredictionStep, SimulateResult, TwinIr } from '@/runtime/types';
import { actionLabel } from '@/features/behavior/BehaviorGraph';

type View = 'future' | 'what-if' | 'planning';

/** Exact decimal string ("17.5") in model units -> integer ticks, without floating point. */
export function decimalToTicks(text: string, ticksPerUnit: number): number | null {
  const m = /^(\d+)(?:\.(\d+))?$/.exec(text.trim());
  if (!m) return null;
  const digits = Math.round(Math.log10(ticksPerUnit));
  if (10 ** digits !== ticksPerUnit) return null;
  const frac = (m[2] ?? '').padEnd(digits, '0');
  if (frac.length > digits) return null;
  return Number(m[1]) * ticksPerUnit + (digits ? Number(frac) : 0);
}

interface TreeNode {
  key: string;
  step: PredictionStep;
  children: TreeNode[];
}

/** Merge trajectories (lists of steps) into a prefix tree. */
export function toTree(trajectories: PredictionStep[][]): TreeNode[] {
  const roots: TreeNode[] = [];
  for (const path of trajectories) {
    let level = roots;
    let prefix = '';
    for (const step of path) {
      prefix += `/${step.transition}`;
      let node = level.find((n) => n.key === prefix);
      if (!node) {
        node = { key: prefix, step, children: [] };
        level.push(node);
      }
      level = node.children;
    }
  }
  return roots;
}

function TreeView({ nodes, twin, depth = 0 }: { nodes: TreeNode[]; twin: TwinDetail; depth?: number }) {
  return (
    <ul role={depth === 0 ? 'tree' : 'group'} aria-label={depth === 0 ? 'Possible futures' : undefined} style={{ listStyle: 'none', margin: 0, paddingLeft: depth ? 18 : 0 }}>
      {nodes.map((n) => {
        const p = twin.presentation.states?.[n.step.state.location];
        return (
          <li key={n.key} role="treeitem" aria-level={depth + 1} aria-expanded={n.children.length ? true : undefined} aria-selected={false}>
            <div className="row-wrap small" style={{ padding: '4px 0', borderLeft: depth ? '2px solid var(--divider)' : undefined, paddingLeft: depth ? 8 : 0 }}>
              <ChevronRight size={12} aria-hidden="true" />
              <span className="mono">{n.step.label}</span>
              <span className="muted">→</span>
              <strong>{p?.label ?? n.step.state.location}</strong>
              <StatusBadge
                tone="info"
                label={`after ${n.step.window.earliest.text}${n.step.window.latest ? `–${n.step.window.latest.text}` : '+'} ${twin.presentation.timeUnit ?? ''}`}
                title="Window of admissible delays for this transition (kernel)"
              />
              <span className="xsmall subtle">at earliest t = {n.step.state.time.text}</span>
            </div>
            {n.children.length > 0 && <TreeView nodes={n.children} twin={twin} depth={depth + 1} />}
          </li>
        );
      })}
    </ul>
  );
}

function FutureView({ twin }: { twin: TwinDetail }) {
  const [depth, setDepth] = useState(3);
  const [horizon, setHorizon] = useState('');
  const [maxNodes, setMaxNodes] = useState(500);
  const tpu = twin.ticksPerUnit || 1000;
  const run = useMutation({
    mutationFn: () => {
      const h = horizon ? decimalToTicks(horizon, tpu) : null;
      return predict(twin.id, { depth, max_nodes: maxNodes, ...(h !== null ? { horizon_ticks: h } : {}) });
    },
  });
  useEffect(() => {
    run.mutate();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);
  const horizonInvalid = horizon !== '' && decimalToTicks(horizon, tpu) === null;
  return (
    <Panel
      title="Possible future states"
      subtitle="Bounded exploration by the kernel from a copy of the current state; every path is admissible by the model"
      actions={
        <>
          <label className="row small">
            Depth
            <input className="vts-input" type="number" min={1} max={8} value={depth} onChange={(e) => setDepth(Math.max(1, Math.min(8, Number(e.target.value) || 1)))} style={{ width: 64 }} />
          </label>
          <label className="row small">
            Horizon ({twin.presentation.timeUnit ?? 'units'})
            <input className="vts-input" value={horizon} onChange={(e) => setHorizon(e.target.value)} placeholder="none" style={{ width: 80 }} aria-invalid={horizonInvalid} />
          </label>
          <label className="row small">
            Max nodes
            <input className="vts-input" type="number" min={1} max={4000} value={maxNodes} onChange={(e) => setMaxNodes(Number(e.target.value) || 500)} style={{ width: 80 }} />
          </label>
          <Button size="sm" variant="primary" icon={<Play size={13} />} loading={run.isPending} disabled={horizonInvalid} onClick={() => run.mutate()}>
            Explore
          </Button>
        </>
      }
    >
      {horizonInvalid && <Callout tone="warning">The horizon must be an exact decimal in model time units (e.g. 60 or 12.5).</Callout>}
      {run.isError ? (
        <ErrorBlock error={run.error} onRetry={() => run.mutate()} />
      ) : !run.data ? (
        <p className="small muted">Exploring…</p>
      ) : (
        <div className="stack">
          {run.data.exploration.map((ex, i) => (
            <div key={i} className="stack-sm">
              <div className="row-wrap small">
                From <strong>{twin.presentation.states?.[ex.root.location]?.label ?? ex.root.location}</strong> at{' '}
                <LogicalTimeText text={ex.root.time.text} unit={twin.presentation.timeUnit} /> · {ex.nodes} states explored
                {ex.truncated && <StatusBadge tone="warning" label="Truncated by limits" />}
              </div>
              {ex.trajectories.length === 0 ? <p className="small muted">No transition can fire within the limits.</p> : <TreeView nodes={toTree(ex.trajectories)} twin={twin} />}
            </div>
          ))}
          {run.data.note && <p className="xsmall subtle">{run.data.note}</p>}
        </div>
      )}
    </Panel>
  );
}

interface ScheduleRow {
  label: string;
  time: string;
}
interface Scenario {
  id: string;
  name: string;
  rows: ScheduleRow[];
}

function WhatIfView({ twin, ir }: { twin: TwinDetail; ir: TwinIr | null }) {
  const live = useRuntimeState(twin.id);
  const labels = useMemo(() => [...new Set(ir?.transitions.map((t) => actionLabel(t.action)).filter((l) => !l.startsWith('τ')) ?? [])].sort(), [ir]);
  const now = live.data?.time.text ?? '0';
  const [scenarios, setScenarios] = useState<Scenario[]>([{ id: 's1', name: 'Scenario 1', rows: [] }]);
  const [active, setActive] = useState('s1');
  const scenario = scenarios.find((s) => s.id === active) ?? scenarios[0]!;
  const [results, setResults] = useState<Record<string, SimulateResult | { error: unknown }>>({});
  const update = (rows: ScheduleRow[]) => setScenarios((all) => all.map((s) => (s.id === scenario.id ? { ...s, rows } : s)));
  const run = useMutation({
    mutationFn: (s: Scenario) => runtimeApi(twin.id).post<SimulateResult>('/runtime/simulate', { schedule: s.rows.map((r) => ({ label: r.label, time: r.time })) }),
    onSuccess: (r, s) => setResults((m) => ({ ...m, [s.id]: r })),
    onError: (e, s) => setResults((m) => ({ ...m, [s.id]: { error: e } })),
  });
  const result = results[scenario.id];
  return (
    <div className="stack">
      <ModeBanner mode="simulation">
        Hypothetical events are evaluated by the kernel on a copy of the current state. Nothing here changes the live twin
        or its ledger.
      </ModeBanner>
      <div className="grid-main-side">
        <Panel
          title={
            <input
              className="vts-input"
              aria-label="Scenario name"
              value={scenario.name}
              onChange={(e) => setScenarios((all) => all.map((s) => (s.id === scenario.id ? { ...s, name: e.target.value } : s)))}
            />
          }
          actions={
            <>
              <Button size="sm" icon={<CopyPlus size={13} />} onClick={() => {
                const id = `s${Date.now()}`;
                setScenarios((all) => [...all, { id, name: `${scenario.name} (fork)`, rows: [...scenario.rows] }]);
                setActive(id);
              }}>Fork</Button>
              <Button size="sm" icon={<RotateCcw size={13} />} onClick={() => update([])}>Reset</Button>
              <Button size="sm" variant="primary" icon={<Play size={13} />} loading={run.isPending} disabled={scenario.rows.length === 0} onClick={() => run.mutate(scenario)}>
                Simulate
              </Button>
            </>
          }
        >
          <div className="stack">
            <p className="small muted">
              Starts from the live state at logical time <strong>{now}</strong> {twin.presentation.timeUnit}. Add events with
              absolute logical times (exact decimals).
            </p>
            {scenario.rows.length === 0 && <EmptyState compact title="No hypothetical events yet" />}
            {scenario.rows.map((r, i) => (
              <div key={i} className="row-wrap">
                <span className="xsmall subtle" style={{ width: 20 }}>{i + 1}.</span>
                <select className="vts-select" aria-label={`Event ${i + 1}`} value={r.label} onChange={(e) => update(scenario.rows.map((x, j) => (j === i ? { ...x, label: e.target.value } : x)))}>
                  {labels.map((l) => <option key={l} value={l}>{twin.presentation.events?.[l.replace(/[!?]$/, '')]?.label ?? l} ({l})</option>)}
                </select>
                <label className="row small">
                  at t =
                  <input className="vts-input" value={r.time} aria-label={`Time of event ${i + 1}`} onChange={(e) => update(scenario.rows.map((x, j) => (j === i ? { ...x, time: e.target.value } : x)))} style={{ width: 90 }} />
                </label>
                <Button size="sm" variant="ghost" iconOnly icon={<Trash2 size={13} />} onClick={() => update(scenario.rows.filter((_x, j) => j !== i))}>
                  Remove event
                </Button>
              </div>
            ))}
            <Button size="sm" icon={<Plus size={13} />} disabled={labels.length === 0} onClick={() => update([...scenario.rows, { label: labels[0] ?? '', time: now }])}>
              Add event
            </Button>
          </div>
        </Panel>
        <Panel title="Outcome" subtitle="Compared with the live state">
          <div className="stack">
            <KeyValue compact items={[['Live state now', live.data ? live.data.configurations.map((c) => twin.presentation.states?.[c.location]?.label ?? c.location).join(', ') : '—']]} />
            {!result ? (
              <p className="small muted">Run the scenario to see whether the model admits it.</p>
            ) : 'error' in result ? (
              result.error instanceof ApiError && result.error.status === 422 ? (
                <Callout tone="critical" title="Not admissible by the model">{result.error.message}</Callout>
              ) : (
                <ErrorBlock error={result.error} compact />
              )
            ) : (
              <div className="stack-sm">
                {result.admissible ? (
                  <StatusBadge tone="ok" label="Admissible by the model" size="lg" />
                ) : (
                  <StatusBadge tone="critical" label="Not admissible by the model" size="lg" />
                )}
                {result.refusal && (
                  <Callout tone="critical" title={`Refused at event ${Number(result.refusal.context.find((c) => c.key === 'step')?.value ?? 0) + 1}`}>
                    {result.refusal.message}
                  </Callout>
                )}
                <ol className="small" style={{ margin: 0, paddingLeft: 18 }}>
                  {result.steps.map((s, i) => {
                    const st = s as { transition?: string; to?: string; from?: string; label?: string; at?: { text: string } };
                    return (
                      <li key={i}>
                        <span className="mono">{st.label ?? st.transition ?? 'step'}</span>
                        {st.to && <> → {twin.presentation.states?.[st.to]?.label ?? st.to}</>}
                        {st.at?.text && <span className="xsmall subtle"> at t = {st.at.text}</span>}
                      </li>
                    );
                  })}
                </ol>
                {result.note && <p className="xsmall subtle">{result.note}</p>}
              </div>
            )}
          </div>
        </Panel>
      </div>
      {scenarios.length > 1 && (
        <Panel title="Scenarios">
          <div className="row-wrap">
            {scenarios.map((s) => (
              <Button key={s.id} size="sm" variant={s.id === scenario.id ? 'primary' : 'secondary'} onClick={() => setActive(s.id)}>
                {s.name}
                {results[s.id] && !('error' in results[s.id]!) ? ((results[s.id] as SimulateResult).admissible ? ' ✓' : ' ✗') : ''}
              </Button>
            ))}
          </div>
        </Panel>
      )}
    </div>
  );
}

function PlanningView({ twin }: { twin: TwinDetail }) {
  const episodes = useQuery({
    queryKey: ['runtime', twin.id, 'planner', 'episodes'],
    queryFn: () => runtimeApi(twin.id).get<{ episodes: PlanningEpisode[] }>('/planner/episodes'),
    refetchInterval: 5000,
    retry: false,
  });
  if (episodes.isError) {
    const e = episodes.error;
    if (e instanceof ApiError && e.status === 404) {
      return <Panel title="Planning"><EmptyState title="No planner integrated">This twin's runtime has no external planner. Planner candidates appear here when one is connected.</EmptyState></Panel>;
    }
    return <Panel title="Planning"><ErrorBlock error={e} onRetry={() => void episodes.refetch()} /></Panel>;
  }
  const list = [...(episodes.data?.episodes ?? [])].reverse();
  return (
    <div className="stack">
      <Callout tone="info" title="How candidates are judged">
        The planner is untrusted. Each candidate is checked geometrically against the twin's <em>known</em> map and
        behaviourally by the kernel (its event schedule simulated on a copy of the state). Only candidates passing both are
        selectable; the lowest common objective is selected.
      </Callout>
      {list.length === 0 && <Panel title="Planning episodes"><EmptyState compact title="No planning episode yet" /></Panel>}
      {list.map((ep) => (
        <Panel key={String(ep.id)} title={`Episode ${ep.id} · ${ep.reason}`} subtitle={`Goal ${ep.goal} · decision: ${ep.decision}`} flush>
          <table className="vts-table">
            <caption className="sr-only">Candidates of episode {String(ep.id)}</caption>
            <thead>
              <tr>
                <th scope="col">Candidate</th>
                <th scope="col" className="num">Objective</th>
                <th scope="col" className="num">Length</th>
                <th scope="col" className="num">Through unknown</th>
                <th scope="col" className="num">Energy</th>
                <th scope="col">Geometric (known map)</th>
                <th scope="col">Admissible by model</th>
                <th scope="col">Status</th>
              </tr>
            </thead>
            <tbody>
              {ep.candidates.map((c) => (
                <tr key={c.label} aria-selected={c.selected}>
                  <td>
                    <strong>{c.label}</strong> <span className="xsmall subtle">{c.profile}</span>
                    {c.duplicate_of && <div className="xsmall subtle">same route as {c.duplicate_of}</div>}
                  </td>
                  <td className="num">{c.plan ? `${(c.plan.objective_mm / 1000).toFixed(1)} m` : '—'}</td>
                  <td className="num">{c.plan ? `${(c.plan.length_mm / 1000).toFixed(1)} m` : '—'}</td>
                  <td className="num">{c.plan ? `${(c.plan.unknown_mm / 1000).toFixed(1)} m` : '—'}</td>
                  <td className="num">{c.plan ? `${(c.plan.energy_mwh / 1000).toFixed(2)} Wh` : '—'}</td>
                  <td>
                    {!c.found ? (
                      <StatusBadge tone="neutral" label="No route" title={c.failure} />
                    ) : c.geometric?.ok ? (
                      <StatusBadge tone="ok" label="Feasible" />
                    ) : (
                      <StatusBadge tone="critical" label="Infeasible" title={c.geometric?.issues.join('; ')} />
                    )}
                  </td>
                  <td>
                    {!c.behavioural?.checked ? (
                      <StatusBadge tone="neutral" label="Not checked" />
                    ) : c.behavioural.ok ? (
                      <StatusBadge tone="ok" label="Admissible" />
                    ) : (
                      <StatusBadge tone="critical" label="Prohibited" title={c.behavioural.detail} />
                    )}
                    {c.behavioural?.checked && !c.behavioural.ok && <div className="xsmall subtle">{c.behavioural.detail}</div>}
                  </td>
                  <td>{c.selected ? <StatusBadge tone="info" label="Selected" /> : <span className="small muted">{c.found ? 'Candidate' : c.failure ?? 'Rejected'}</span>}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </Panel>
      ))}
    </div>
  );
}

/** @param fixed When given (twin workspace), shows only that view and no view tabs. */
export default function AssetPredictTab({ view: fixed }: { view?: View } = {}) {
  const { twin, runtimeConnected } = useAssetContext();
  const [params, setParams] = useSearchParams();
  const view = fixed ?? ((params.get('view') as View) || 'future');
  const model = useRuntimeModel(runtimeConnected ? twin?.id : null);
  if (!twin) return <EmptyState title="No digital twin" />;
  if (!runtimeConnected) {
    return <Panel title="Prediction"><RuntimeUnavailable what="Predictions are computed by the kernel from the live state." /></Panel>;
  }
  return (
    <div className="stack">
      {!fixed && <Tabs
        label="Prediction views"
        value={view}
        onChange={(v) => {
          const p = new URLSearchParams(params);
          p.set('view', v);
          setParams(p, { replace: true });
        }}
        tabs={[
          { id: 'future', label: 'Future states' },
          { id: 'what-if', label: 'What-if simulation' },
          { id: 'planning', label: 'Planning' },
        ]}
      />}
      {view === 'future' && <FutureView twin={twin} />}
      {view === 'what-if' && <WhatIfView twin={twin} ir={model.data ?? null} />}
      {view === 'planning' && <PlanningView twin={twin} />}
    </div>
  );
}
