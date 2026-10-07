/**
 * Test → Scenario Builder: scenarios are executable test cases of the twin — events of the
 * plant or the Digital Twin View at logical times, waits, world (ground-truth) changes the twin
 * observes, telemetry, and expectations (state, transition, availability windows, monitors,
 * semantics, world). Every timing statement comes from the semantic kernel on the compiled
 * Digital Twin View: availability (now / later / not reachable) with all legal intervals,
 * why a window is what it is, and why a step is refused. Runs are tests of example runs, not
 * proofs; a full run is stored as scenario evidence for exactly these inputs.
 */
import { useQuery } from '@tanstack/react-query';
import { ArrowDown, ArrowUp, CheckCircle2, CircleDashed, Copy, Eye, FlaskConical, Globe2, HelpCircle, ListChecks, MonitorPlay, Plus, Radio, Timer, Trash2, XCircle } from 'lucide-react';
import { useMemo, useState, type ReactNode } from 'react';
import { Link, useNavigate, useParams } from 'react-router-dom';
import { blueprintApi, blueprintRoute } from '@/api/blueprints';
import type { ScenarioDef, ScenarioResult, ScenarioRun, ScenarioStep, ScenarioStepResult, TimingResult } from '@/api/types';
import { Button, Callout, EmptyState, StatusBadge } from '@/design';
import { useEditor, useSection } from '../editor';
import { ConfirmDelete, EdPage, Pane, SelectList, TextArea, TextField } from '../ui';
import { useWorkspace } from '../workspace';
import { AddStepPanel } from '../test/AddStep';
import { refusalFixes, reasonsOf, runByStep, scenarioId as newScenarioId, stepText, stepTime, timingByStep } from '../test/scenario';
import { ScenarioTimeline, type MarkState } from '../test/Timeline';

const KIND_ICON: Record<ScenarioStep['kind'], ReactNode> = {
  event: <Radio size={13} aria-hidden="true" />,
  delay: <Timer size={13} aria-hidden="true" />,
  world: <Globe2 size={13} aria-hidden="true" />,
  observe: <Eye size={13} aria-hidden="true" />,
  expect: <FlaskConical size={13} aria-hidden="true" />,
};
const KIND_LABEL: Record<ScenarioStep['kind'], string> = { event: 'Event', delay: 'Wait', world: 'World', observe: 'Telemetry', expect: 'Expect' };

function useScenarioTiming(steps: ScenarioStep[], enabled = true) {
  const e = useEditor();
  const key = JSON.stringify(steps);
  return useQuery({
    queryKey: ['blueprints', 'timing', e.id, e.version, key],
    queryFn: () => blueprintApi.timing(e.id, e.version, { scenarioSteps: steps } as Record<string, unknown>),
    enabled,
    placeholderData: (prev) => prev,
    staleTime: 60_000,
    retry: false,
  });
}

function RunStatus({ results, timing }: { results: ScenarioStepResult[] | undefined; timing: { status: string } | undefined }) {
  const r = results?.[results.length - 1];
  if (r) {
    const s = r.status;
    if (s === 'pass') return <StatusBadge tone="ok" icon={CheckCircle2} label="PASS" />;
    if (s === 'ok') return <StatusBadge tone="ok" label="OK" />;
    if (s === 'fail') return <StatusBadge tone="critical" icon={XCircle} label="FAIL" />;
    if (s === 'invalid') return <StatusBadge tone="critical" label="REFUSED" />;
    if (s === 'not_evaluated') return <StatusBadge tone="neutral" label="NOT EVALUATED" />;
    if (s === 'inconclusive') return <StatusBadge tone="warning" label="INCONCLUSIVE" />;
    return <StatusBadge tone="critical" label={s.toUpperCase()} />;
  }
  if (timing?.status === 'invalid') return <StatusBadge tone="critical" label="ILLEGAL" title="The kernel refuses this step (see why)" />;
  if (timing?.status === 'not_evaluated') return <StatusBadge tone="neutral" label="after an illegal step" />;
  if (timing?.status === 'ok') return <StatusBadge tone="ok" label="legal" />;
  return <StatusBadge tone="neutral" icon={CircleDashed} label="not run" />;
}

function StepRow({
  step,
  index,
  count,
  selected,
  insertHere,
  onSelect,
  onChange,
  onMove,
  onDelete,
  onInsertAfter,
  runResults,
  timing,
  editable,
}: {
  step: ScenarioStep;
  index: number;
  count: number;
  selected: boolean;
  insertHere: boolean;
  onSelect: () => void;
  onChange: (s: ScenarioStep, label: string) => void;
  onMove: (d: -1 | 1) => void;
  onDelete: () => void;
  onInsertAfter: () => void;
  runResults: ScenarioStepResult[] | undefined;
  timing: { status: string; explanation?: Record<string, unknown>; error?: { message: string }; requested?: { at?: { ticks: number } } } | undefined;
  editable: boolean;
}) {
  const [open, setOpen] = useState(false);
  const [time, setTime] = useState(stepTime(step) ?? '');
  const [prev, setPrev] = useState(stepTime(step) ?? '');
  if (prev !== (stepTime(step) ?? '')) {
    setPrev(stepTime(step) ?? '');
    setTime(stepTime(step) ?? '');
  }
  const hasTime = step.kind !== 'delay';
  const commitTime = (t: string) => {
    if (t === (stepTime(step) ?? '') || !/^\d+(\.\d+)?$/.test(t)) {
      setTime(stepTime(step) ?? '');
      return;
    }
    const next: ScenarioStep = { ...step, at: t };
    if (step.kind === 'world' && step.observation) next.observation = { ...(step.observation as Record<string, unknown>), at: t };
    onChange(next, `Move ${step.id} to t = ${t}`);
  };
  const failed = runResults?.find((r) => r.status === 'fail' || r.status === 'invalid' || r.status === 'error') ?? null;
  const timingBad = timing?.status === 'invalid' ? timing : null;
  const explanation = failed?.explanation ?? timingBad?.explanation;
  const reasons = explanation ? reasonsOf(explanation) : [];
  const fixes = timingBad && step.kind === 'event' ? refusalFixes(timingBad.explanation, timingBad.requested?.at?.ticks) : [];
  const detail = failed ? (failed.detail ?? failed.error?.message ?? (failed.actual !== undefined ? `actual: ${failed.actual}` : '')) : timingBad?.error?.message;
  const okDetail = runResults?.[0]?.status === 'pass' && step.expectRefused ? runResults[0].detail : null;
  return (
    <>
      <tr className={`${selected ? 'is-selected' : ''} ${insertHere ? 'is-insert' : ''}`} onClick={onSelect} style={{ cursor: 'pointer' }}>
        <td className="mono xsmall subtle">{step.id}</td>
        <td style={{ width: 92 }}>
          {hasTime ? (
            <input
              className="vts-input mono"
              style={{ width: 80, minHeight: 28 }}
              value={time}
              disabled={!editable}
              aria-label={`Time of ${step.id}`}
              onClick={(ev) => ev.stopPropagation()}
              onChange={(ev) => setTime(ev.target.value.trim())}
              onBlur={() => commitTime(time)}
              onKeyDown={(ev) => {
                if (ev.key === 'Enter') commitTime(time);
                if (ev.key === 'Escape') setTime(stepTime(step) ?? '');
              }}
            />
          ) : (
            <span className="mono small">+{step.delay}</span>
          )}
        </td>
        <td>
          <span className="row" style={{ gap: 6 }}>
            {KIND_ICON[step.kind]} <span className="xsmall subtle">{KIND_LABEL[step.kind]}</span>
          </span>
        </td>
        <td className="small" style={{ overflowWrap: 'anywhere' }}>
          <span className={step.kind === 'event' ? 'mono' : undefined}>{stepText(step)}</span>
          {runResults?.some((r) => r.translated) && (
            <span className="xsmall subtle"> → DT {runResults.find((r) => r.translated)?.translated?.dt}</span>
          )}
          {(detail || okDetail) && <div className="xsmall" style={{ color: detail ? 'var(--crit)' : 'var(--text-muted)' }}>{detail || okDetail}</div>}
        </td>
        <td>
          <RunStatus results={runResults} timing={timing} />
        </td>
        <td onClick={(ev) => ev.stopPropagation()} style={{ whiteSpace: 'nowrap' }}>
          {(reasons.length > 0 || fixes.length > 0) && (
            <Button size="sm" variant="ghost" iconOnly icon={<HelpCircle size={13} />} onClick={() => setOpen((o) => !o)} aria-expanded={open}>
              Why?
            </Button>
          )}
          {editable && (
            <>
              <Button size="sm" variant="ghost" iconOnly icon={<Plus size={13} />} onClick={onInsertAfter}>
                Insert a step after {step.id}
              </Button>
              <Button size="sm" variant="ghost" iconOnly icon={<ArrowUp size={13} />} disabled={index === 0} onClick={() => onMove(-1)}>
                Move {step.id} up
              </Button>
              <Button size="sm" variant="ghost" iconOnly icon={<ArrowDown size={13} />} disabled={index === count - 1} onClick={() => onMove(1)}>
                Move {step.id} down
              </Button>
              <Button size="sm" variant="ghost" iconOnly icon={<Trash2 size={13} />} onClick={onDelete}>
                Delete {step.id}
              </Button>
            </>
          )}
        </td>
      </tr>
      {open && (
        <tr>
          <td />
          <td colSpan={5}>
            <div className="stack-sm" style={{ padding: '4px 0 8px' }}>
              {reasons.length > 0 && (
                <ul className="small" style={{ margin: 0, paddingLeft: 16 }}>
                  {reasons.map((r) => (
                    <li key={r}>{r}</li>
                  ))}
                </ul>
              )}
              {failed?.expected && (
                <span className="xsmall">
                  expected <code>{JSON.stringify(failed.expected)}</code>, actual <code>{failed.actual}</code>
                </span>
              )}
              {editable && fixes.length > 0 && (
                <div className="row-wrap">
                  {fixes.map((f) => (
                    <Button key={f.kind} size="sm" variant={f.kind === 'earliest' ? 'primary' : 'secondary'} onClick={() => onChange({ ...step, at: f.at }, `Move ${step.id} to its ${f.kind} legal time`)}>
                      Move to {f.kind} legal time (t = {f.at})
                    </Button>
                  ))}
                  <Button size="sm" onClick={() => onChange({ ...step, expectRefused: true }, `Make ${step.id} a negative test`)}>
                    Expect it to be refused
                  </Button>
                </div>
              )}
            </div>
          </td>
        </tr>
      )}
    </>
  );
}

function ScenarioEditor({ sc, index, lastRun, onRun }: { sc: ScenarioDef; index: number; lastRun: { result: ScenarioResult; steps: string } | null; onRun: (r: ScenarioResult, steps: string) => void }) {
  const e = useEditor();
  const [, setScenarios] = useSection('scenarios');
  const navigate = useNavigate();
  const unit = e.doc.identity.timeUnit || 's';
  const [selected, setSelected] = useState<string | null>(null);
  const [insertAfter, setInsertAfter] = useState<string | null | undefined>(undefined);
  const [label, setLabel] = useState<string | null>(null);
  const [running, setRunning] = useState(false);
  const [runError, setRunError] = useState<string | null>(null);
  const [confirm, setConfirm] = useState(false);
  const steps = sc.steps;
  const insertIdx = insertAfter === undefined ? steps.length : insertAfter === null ? 0 : steps.findIndex((s) => s.id === insertAfter) + 1;
  const prefix = useMemo(() => steps.slice(0, insertIdx), [steps, insertIdx]);
  const full = useScenarioTiming(steps);
  const pre = useScenarioTiming(prefix, insertIdx !== steps.length);
  const prefixTiming: TimingResult | undefined = insertIdx === steps.length ? full.data : pre.data;
  const prefixError = insertIdx === steps.length ? full.error : pre.error;
  const timing = useMemo(() => timingByStep(full.data), [full.data]);
  const stepsKey = JSON.stringify(steps);
  const fresh = lastRun && lastRun.steps === stepsKey ? lastRun.result : null;
  const runs = useMemo(() => runByStep(fresh?.steps), [fresh]);
  const states = useMemo(() => {
    const m = new Map<string, MarkState>();
    for (const s of steps) {
      const r = runs.get(s.id);
      const t = timing.get(s.id);
      if (r?.length) {
        const bad = r.find((x) => x.status !== 'pass' && x.status !== 'ok');
        m.set(s.id, bad ? (bad.status === 'invalid' ? 'invalid' : 'fail') : 'pass');
      } else if (t) m.set(s.id, t.status === 'ok' ? 'ok' : t.status === 'invalid' ? 'invalid' : 'pending');
      else m.set(s.id, 'pending');
    }
    return m;
  }, [steps, runs, timing]);
  const setSc = (fn: (s: ScenarioDef) => ScenarioDef, lbl: string, key?: string) => setScenarios((list) => list.map((x, i) => (i === index ? fn(x) : x)), { label: lbl, key });
  const setSteps = (fn: (s: ScenarioStep[]) => ScenarioStep[], lbl: string) => setSc((x) => ({ ...x, steps: fn(x.steps) }), lbl);
  const window = prefixTiming?.final.availability.find((a) => a.label === label);
  const run = async () => {
    setRunning(true);
    setRunError(null);
    try {
      await e.saveNow();
      const r: ScenarioRun = await blueprintApi.runScenario(e.id, e.version, sc.id);
      if (r.results[0]) onRun(r.results[0], stepsKey);
    } catch (err) {
      setRunError(err instanceof Error ? err.message : String(err));
    } finally {
      setRunning(false);
    }
  };
  const invalidAt = full.data?.first_invalid;
  return (
    <div className="stack">
      <Pane
        title={
          <span className="row">
            <ListChecks size={14} aria-hidden="true" /> {sc.name || sc.id}
          </span>
        }
        actions={
          <>
            <Button size="sm" variant="primary" icon={<FlaskConical size={13} />} loading={running} onClick={() => void run()}>
              Run scenario
            </Button>
            <Link to={blueprintRoute(e.id, e.version, `test/preview?scenario=${encodeURIComponent(sc.id)}`)} className="vts-btn vts-btn--ghost vts-btn--sm">
              <MonitorPlay size={13} aria-hidden="true" /> Preview
            </Link>
            {e.editable && (
              <>
                <Button
                  size="sm"
                  variant="ghost"
                  iconOnly
                  icon={<Copy size={13} />}
                  onClick={() => {
                    const id = newScenarioId(e.doc.scenarios, `${sc.id}_copy`);
                    setScenarios((list) => [...list, { ...sc, id, name: `${sc.name} (copy)` }], { label: 'Duplicate scenario' });
                    navigate(blueprintRoute(e.id, e.version, `test/scenarios/${encodeURIComponent(id)}`));
                  }}
                >
                  Duplicate scenario
                </Button>
                <Button size="sm" variant="ghost" iconOnly icon={<Trash2 size={13} />} onClick={() => setConfirm(true)}>
                  Delete scenario
                </Button>
              </>
            )}
          </>
        }
      >
        <div className="stack-sm">
          <div className="vts-fgrid">
            <TextField label="Name" value={sc.name} onChange={(v) => setSc((x) => ({ ...x, name: v }), 'Rename scenario', `sc.${sc.id}.name`)} />
            <TextField label="Id" value={sc.id} mono disabled onChange={() => undefined} hint="Referenced by tests and evidence" />
          </div>
          <TextArea label="What it shows" value={sc.description ?? ''} rows={2} onChange={(v) => setSc((x) => ({ ...x, description: v }), 'Describe scenario', `sc.${sc.id}.desc`)} />
          <span className="xsmall subtle">Starts in the initial state of the Digital Twin View at t = 0.</span>
        </div>
      </Pane>
      {fresh && (
        <Callout tone={fresh.outcome === 'pass' ? 'ok' : 'critical'} title={`${fresh.outcome === 'pass' ? 'PASS' : 'FAIL'} — ${fresh.passed} expectation(s) passed, ${fresh.failed} failed${fresh.refused ? ' · a step was refused' : ''}`}>
          <span className="small">{fresh.outcome === 'pass' ? 'Test of this example run (not a proof).' : fresh.firstFailure}</span>
        </Callout>
      )}
      {lastRun && !fresh && <p className="xsmall subtle">The scenario changed since its last run; run it again to refresh the results.</p>}
      {runError && <Callout tone="critical" title="Could not run">{runError}</Callout>}
      {invalidAt !== null && invalidAt !== undefined && !fresh && (
        <Callout tone="warning" title="A step is illegal">
          The kernel refuses step {(full.data as TimingResult & { origins?: string[] })?.origins?.[invalidAt] ?? invalidAt + 1}; later steps are not evaluated. Open “Why?” on it to see the legal times.
        </Callout>
      )}
      <ScenarioTimeline steps={steps} states={states} selected={selected} onSelect={setSelected} now={prefixTiming?.final.state.time.text ?? null} windows={window?.intervals ?? null} windowLabel={label} unit={unit} />
      <div className="vts-sc-main">
        <Pane title={`Steps (${steps.length})`} flush>
          {steps.length === 0 ? (
            <div style={{ padding: 12 }}>
              <EmptyState compact title="No steps yet">Pick an event the kernel says is available, or add a world change, telemetry or an expectation.</EmptyState>
            </div>
          ) : (
            <table className="vts-table vts-sc-steps">
              <caption className="sr-only">Scenario steps in execution order</caption>
              <thead>
                <tr>
                  <th scope="col">#</th>
                  <th scope="col">t ({unit})</th>
                  <th scope="col">Kind</th>
                  <th scope="col">Step</th>
                  <th scope="col">Result</th>
                  <th scope="col" aria-label="Actions" />
                </tr>
              </thead>
              <tbody>
                {insertAfter === null && (
                  <tr className="is-insert">
                    <td colSpan={6} className="xsmall subtle">
                      New steps are inserted at the start.
                    </td>
                  </tr>
                )}
                {steps.map((s, i) => (
                  <StepRow
                    key={s.id}
                    step={s}
                    index={i}
                    count={steps.length}
                    selected={selected === s.id}
                    insertHere={insertAfter !== undefined && insertAfter === s.id}
                    onSelect={() => setSelected(s.id)}
                    onChange={(n, lbl) => setSteps((list) => list.map((x) => (x.id === s.id ? n : x)), lbl)}
                    onMove={(d) =>
                      setSteps((list) => {
                        const next = [...list];
                        const j = i + d;
                        [next[i], next[j]] = [next[j]!, next[i]!];
                        return next;
                      }, `Move ${s.id}`)
                    }
                    onDelete={() => setSteps((list) => list.filter((x) => x.id !== s.id), `Delete step ${s.id}`)}
                    onInsertAfter={() => setInsertAfter(insertAfter === s.id ? undefined : s.id)}
                    runResults={runs.get(s.id)}
                    timing={s.kind === 'expect' && timing.get(s.id)?.status === 'ok' ? undefined : (timing.get(s.id) as Parameters<typeof StepRow>[0]['timing'])}
                    editable={e.editable}
                  />
                ))}
              </tbody>
            </table>
          )}
          {e.editable && steps.length > 0 && (
            <div className="row" style={{ padding: 8 }}>
              <span className="xsmall subtle">
                {insertAfter === undefined ? 'New steps are added at the end.' : insertAfter === null ? 'Inserting at the start.' : `Inserting after ${insertAfter}.`}
              </span>
              {insertAfter !== null && (
                <button type="button" className="vts-linkbtn xsmall" onClick={() => setInsertAfter(null)}>
                  insert at the start
                </button>
              )}
              {insertAfter !== undefined && (
                <button type="button" className="vts-linkbtn xsmall" onClick={() => setInsertAfter(undefined)}>
                  add at the end
                </button>
              )}
            </div>
          )}
        </Pane>
        {e.editable ? (
          <AddStepPanel
            key={sc.id}
            prefix={prefix}
            timing={prefixTiming}
            timingError={prefixError}
            timingPending={insertIdx === steps.length ? full.isPending : pre.isPending}
            unit={unit}
            selectedLabel={label}
            onSelectLabel={setLabel}
            onAdd={(s) => {
              setSteps((list) => [...list.slice(0, insertIdx), s, ...list.slice(insertIdx)], `Add ${s.kind} step`);
              if (insertAfter !== undefined) setInsertAfter(s.id);
              setSelected(s.id);
              setLabel(null);
            }}
          />
        ) : (
          <Pane title="Published version">
            <p className="small muted">Published scenarios are read-only; run them, or create a draft to change them.</p>
          </Pane>
        )}
      </div>
      <ConfirmDelete
        open={confirm}
        onOpenChange={setConfirm}
        title={`Delete scenario ${sc.name || sc.id}?`}
        dependencies={[]}
        consequence="Scenario evidence already recorded stays in the evidence history."
        onConfirm={() => {
          setScenarios((list) => list.filter((x) => x.id !== sc.id), { label: `Delete scenario ${sc.id}` });
          navigate(blueprintRoute(e.id, e.version, 'test/scenarios'), { replace: true });
        }}
      />
    </div>
  );
}

export default function ScenariosPage() {
  const e = useEditor();
  const ws = useWorkspace();
  const [scenarios, setScenarios] = useSection('scenarios');
  const { scenarioId } = useParams();
  const navigate = useNavigate();
  const [runs, setRuns] = useState<Record<string, { result: ScenarioResult; steps: string }>>({});
  const [runAll, setRunAll] = useState<ScenarioRun | null>(null);
  const index = scenarioId ? scenarios.findIndex((s) => s.id === scenarioId) : scenarios.length > 0 ? 0 : -1;
  const sc = index >= 0 ? scenarios[index]! : null;
  const open = (id: string) => navigate(blueprintRoute(e.id, e.version, `test/scenarios/${encodeURIComponent(id)}`));
  const add = () => {
    const id = newScenarioId(scenarios);
    setScenarios((list) => [...list, { id, name: 'New scenario', description: '', start: { kind: 'initial' }, steps: [] }], { label: 'Add scenario' });
    open(id);
  };
  const runEverything = async () => {
    await e.saveNow();
    try {
      const r = (await ws.runCheck('scenarios')) as unknown as ScenarioRun;
      setRunAll(r);
      const next: Record<string, { result: ScenarioResult; steps: string }> = {};
      for (const res of r.results ?? []) {
        const def = e.doc.scenarios.find((s) => s.id === res.id);
        if (def) next[res.id] = { result: res, steps: JSON.stringify(def.steps) };
      }
      setRuns((x) => ({ ...x, ...next }));
    } catch {
      /* logged in Output */
    }
  };
  const items = scenarios.map((s) => {
    const r = runs[s.id];
    const fresh = r && r.steps === JSON.stringify(s.steps) ? r.result : null;
    return {
      id: s.id,
      label: s.name || s.id,
      search: `${s.id} ${s.name} ${s.description ?? ''}`,
      meta: fresh ? (fresh.outcome === 'pass' ? 'PASS' : 'FAIL') : `${s.steps.length} steps`,
      tone: fresh && fresh.outcome !== 'pass' ? ('error' as const) : undefined,
    };
  });
  return (
    <EdPage
      title="Scenario Builder"
      wide
      description="Executable test scenarios with timing windows from the verified kernel."
      actions={
        <>
          <Button size="sm" icon={<ListChecks size={14} />} loading={ws.running.has('scenarios')} disabled={scenarios.length === 0} onClick={() => void runEverything()}>
            Run all tests
          </Button>
          {e.editable && (
            <Button size="sm" variant="primary" icon={<Plus size={14} />} onClick={add}>
              Scenario
            </Button>
          )}
        </>
      }
      guide={
        <>
          Build a scenario step by step: the kernel tells you which events are available <strong>now</strong>, <strong>later</strong> (with every legal interval) or <strong>not reachable</strong> from the state reached so far, and why. An event outside
          its window is refused, with the earliest or latest legal time to move it to. Expectations check the state, transitions, windows, monitors and semantics. Results are tests of example runs, not proofs.
        </>
      }
    >
      {runAll && (
        <Callout tone={runAll.outcome === 'pass' ? 'ok' : 'critical'} title={`Regression ${runAll.outcome === 'pass' ? 'PASS' : 'FAIL'}: ${runAll.passed} expectation(s) passed, ${runAll.failed} failed in ${runAll.durationMs} ms`}>
          {runAll.evidenceId ? (
            <span className="small">
              Recorded as scenario evidence <Link to={`/studio/verification/${encodeURIComponent(runAll.evidenceId)}`}>{runAll.evidenceId}</Link> for exactly these inputs.
            </span>
          ) : null}
        </Callout>
      )}
      <div className="vts-sc-layout">
        <Pane title="Scenarios" flush>
          <SelectList
            items={items}
            selected={sc?.id ?? null}
            onSelect={open}
            label="Scenarios"
            empty={
              <EmptyState compact title="No scenarios" action={e.editable ? <Button size="sm" onClick={add}>New scenario</Button> : undefined}>
                A scenario is a timed test case of the twin.
              </EmptyState>
            }
          />
        </Pane>
        {sc ? (
          <ScenarioEditor key={sc.id} sc={sc} index={index} lastRun={runs[sc.id] ?? null} onRun={(r, steps) => setRuns((x) => ({ ...x, [sc.id]: { result: r, steps } }))} />
        ) : scenarioId ? (
          <Callout tone="warning" title="No such scenario">
            <Link to={blueprintRoute(e.id, e.version, 'test/scenarios')}>Back to the scenarios</Link>
          </Callout>
        ) : (
          <EmptyState title="Create the first scenario" action={e.editable ? <Button variant="primary" onClick={add}>New scenario</Button> : undefined}>
            Scenarios replay the twin&apos;s behaviour on chosen runs: nominal missions, faults, deadlines.
          </EmptyState>
        )}
      </div>
    </EdPage>
  );
}
