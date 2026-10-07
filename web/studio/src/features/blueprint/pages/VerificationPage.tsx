/**
 * Assurance → Verification: every check that stands behind this version, grouped by what it
 * establishes — formal decisions (artefact validation, semantic alignment, compilation with
 * translation validation), structural validation, tests (scenario regression: tests, not
 * proofs) and release integrity — with its evidence, a way to run it, and "Run all checks".
 *
 * Status words are precise: VALID (an artefact passed its validator), VERIFIED (all formal
 * checks pass for exactly these inputs). A run that could not decide is a check error, never
 * a negative verdict. Every state shown is the backend's status for the pinned inputs.
 */
import { CheckCircle2, CircleDashed, ListChecks, Loader2, PlayCircle, ShieldCheck, XCircle } from 'lucide-react';
import { useState } from 'react';
import { Link } from 'react-router-dom';
import { blueprintRoute, useBlueprintStatus } from '@/api/blueprints';
import { useEvidenceList } from '@/api/queries';
import type { GateItem } from '@/api/types';
import { Button, ErrorBlock, OutcomeBadge, Skeleton, StatusBadge, TimeStamp, humanize } from '@/design';
import { useEditor } from '../editor';
import { GateBadge } from '../status';
import { EdPage, Pane } from '../ui';
import { checkTitle, useWorkspace, type CheckId } from '../workspace';
import { VerdictCard } from './AlignmentPage';

const GROUPS: { id: string; title: string; note: string; items: string[] }[] = [
  {
    id: 'formal',
    title: 'Formal verification',
    note: 'Decisions by the validators, the aligner and the compiler on exactly the pinned artefacts.',
    items: ['pt_model', 'dt_model', 'ontology', 'pt_interpretation', 'dt_interpretation', 'alignment', 'compiler'],
  },
  { id: 'structural', title: 'Structural validation', note: 'Consistency of the Blueprint sections and the monitor definitions.', items: ['structure', 'world', 'data', 'bindings', 'monitors'] },
  { id: 'tests', title: 'Tests (not proofs)', note: 'Scenario regression tests run on the verified kernel; they show behaviour on chosen runs only.', items: ['scenarios'] },
  { id: 'release', title: 'Release integrity', note: 'The Verified Core Package and the Deployment Bundle, hash-protected.', items: ['package'] },
];

/** The check that (re)computes a gate item, when one exists. */
const RUNNER: Record<string, CheckId> = {
  pt_model: 'formal',
  dt_model: 'formal',
  ontology: 'formal',
  pt_interpretation: 'formal',
  dt_interpretation: 'formal',
  alignment: 'alignment',
  compiler: 'compile',
  scenarios: 'scenarios',
};

const SEQUENCE: CheckId[] = ['formal', 'compile', 'alignment', 'scenarios'];

type StepState = 'pending' | 'running' | 'pass' | 'fail' | 'error' | 'skipped';

function StepIcon({ s }: { s: StepState }) {
  if (s === 'running') return <Loader2 size={14} className="vts-spin" aria-label="running" />;
  if (s === 'pass') return <CheckCircle2 size={14} style={{ color: 'var(--ok)' }} aria-label="passed" />;
  if (s === 'fail' || s === 'error') return <XCircle size={14} style={{ color: 'var(--crit)' }} aria-label={s === 'fail' ? 'failed' : 'check error'} />;
  return <CircleDashed size={14} className="subtle" aria-label={s} />;
}

function GateRow({ g, base }: { g: GateItem; base: string }) {
  const ws = useWorkspace();
  const e = useEditor();
  const runner = RUNNER[g.id];
  return (
    <li>
      <span aria-hidden="true" />
      <strong>{g.title}</strong>
      <span className="small muted" style={{ minWidth: 0, overflowWrap: 'anywhere' }}>
        {g.detail}
        {g.evidenceId && (
          <>
            {' '}
            <Link to={`/studio/verification/${encodeURIComponent(g.evidenceId)}`} className="mono xsmall">
              {g.evidenceId}
            </Link>
          </>
        )}
      </span>
      <span className="row" style={{ justifyContent: 'flex-end' }}>
        <GateBadge state={g.state} label={g.state === 'pass' && g.id.endsWith('model') ? 'VALID' : g.state === 'pass' && g.id.endsWith('interpretation') ? 'VALID' : g.state === 'pass' && g.id === 'ontology' ? 'VALID' : undefined} />
        {runner && (
          <Button size="sm" variant="ghost" loading={ws.running.has(runner)} disabled={ws.running.size > 0 && !ws.running.has(runner)} onClick={() => void e.saveNow().then(() => ws.runCheck(runner).catch(() => undefined))}>
            Run
          </Button>
        )}
        {g.state !== 'pass' && g.state !== 'not_applicable' && (
          <Link to={`${base}/${g.fix}`} className="xsmall">
            Fix
          </Link>
        )}
      </span>
    </li>
  );
}

function EvidenceHistory() {
  const e = useEditor();
  const dt = e.detail.pins.dt_model;
  const [artifact, version] = (dt ?? '').split('@');
  const list = useEvidenceList({ artifact: artifact || undefined, version: version ? Number(version) : undefined, limit: 15 });
  if (!dt) return <p className="small muted">No Digital Twin View pinned yet.</p>;
  if (list.isPending) return <Skeleton lines={3} />;
  if (list.isError) return <ErrorBlock error={list.error} compact />;
  const items = list.data.items;
  if (items.length === 0) return <p className="small muted">No evidence recorded for {dt} yet.</p>;
  return (
    <table className="vts-table">
      <caption className="sr-only">Evidence recorded for the pinned Digital Twin View</caption>
      <thead>
        <tr>
          <th scope="col">Check</th>
          <th scope="col">Result</th>
          <th scope="col">Summary</th>
          <th scope="col">When</th>
          <th scope="col">Evidence</th>
        </tr>
      </thead>
      <tbody>
        {items.map((x) => (
          <tr key={x.id}>
            <td className="small">{humanize(x.kind)}</td>
            <td>
              <OutcomeBadge outcome={x.outcome} verdict={x.verdict} />
            </td>
            <td className="xsmall muted" style={{ maxWidth: 420 }}>
              {x.summary}
            </td>
            <td className="xsmall">
              <TimeStamp value={x.createdAt} relative />
            </td>
            <td>
              <Link to={`/studio/verification/${encodeURIComponent(x.id)}`} className="mono xsmall">
                {x.id}
              </Link>
            </td>
          </tr>
        ))}
      </tbody>
    </table>
  );
}

export default function VerificationPage() {
  const e = useEditor();
  const ws = useWorkspace();
  const status = useBlueprintStatus(e.id, e.version);
  const base = blueprintRoute(e.id, e.version);
  const [steps, setSteps] = useState<Record<string, StepState> | null>(null);
  const [runningAll, setRunningAll] = useState(false);
  const hasScenarios = e.doc.scenarios.length > 0;

  const runAll = async () => {
    setRunningAll(true);
    const seq = SEQUENCE.filter((c) => c !== 'scenarios' || hasScenarios);
    const st: Record<string, StepState> = Object.fromEntries(seq.map((c) => [c, 'pending']));
    setSteps({ ...st });
    try {
      await e.saveNow();
      for (const c of seq) {
        st[c] = 'running';
        setSteps({ ...st });
        try {
          const r = await ws.runCheck(c);
          const outcome = (r.outcome as string | undefined) ?? 'pass';
          st[c] = outcome === 'pass' ? 'pass' : outcome === 'fail' ? 'fail' : 'error';
        } catch {
          st[c] = 'error';
        }
        setSteps({ ...st });
      }
    } finally {
      setRunningAll(false);
    }
  };

  const items = status.data?.readiness.items ?? [];
  const formal = items.filter((i) => GROUPS[0]!.items.includes(i.id));
  const formalPass = formal.length > 0 && formal.every((i) => i.state === 'pass');
  const formalOpen = formal.filter((i) => i.state !== 'pass');
  const anyError = formal.some((i) => i.state === 'error');
  const anyFail = formal.some((i) => i.state === 'fail');
  return (
    <EdPage
      title="Verification"
      wide
      description="Every check behind this version, its evidence, and what is still open."
      actions={
        <Button variant="primary" size="sm" icon={<PlayCircle size={14} />} loading={runningAll} disabled={ws.running.size > 0 && !runningAll} onClick={() => void runAll()}>
          Run all checks
        </Button>
      }
      guide={
        <>
          <strong>Run all checks</strong> validates the five formal artefacts, compiles the Digital Twin View (with translation validation), runs the aligner and the scenario tests, in that order. Each result is stored as evidence bound to the exact input
          hashes: change an input and the evidence no longer applies. Packaging is part of Release.
        </>
      }
    >
      {status.isPending ? (
        <Skeleton lines={5} />
      ) : status.isError ? (
        <ErrorBlock error={status.error} onRetry={() => void status.refetch()} />
      ) : (
        <VerdictCard
          word={formalPass ? 'PASS' : anyError ? 'ERROR' : anyFail ? 'FAIL' : 'NOT RUN'}
          title={formalPass ? 'VERIFIED — every formal check passes for these inputs' : `NOT VERIFIED — ${formalOpen.length} formal check(s) open`}
        >
          <div className="row-wrap">
            {formalPass && <StatusBadge tone="formal" icon={ShieldCheck} label="VERIFIED" />}
            <StatusBadge tone={status.data.readiness.verdict === 'ready' ? 'ok' : 'warning'} label={status.data.readiness.verdict === 'ready' ? 'Release gate passes' : `${status.data.readiness.blockers.length} release blocker(s)`} />
            <span className="xsmall subtle">Verified means: artefacts VALID, aligned, compiled with translation validation. Scenario tests and runtime conformance are reported separately.</span>
          </div>
          {!formalPass && formalOpen.length > 0 && (
            <ul className="xsmall" style={{ margin: '6px 0 0', paddingLeft: 16 }}>
              {formalOpen.slice(0, 4).map((i) => (
                <li key={i.id}>
                  <strong>{i.title}</strong>: {i.detail}
                </li>
              ))}
            </ul>
          )}
        </VerdictCard>
      )}
      {steps && (
        <Pane title={<span className="row"><ListChecks size={14} aria-hidden="true" /> Run all checks</span>}>
          <ol className="vts-list" style={{ listStyle: 'none', padding: 0 }}>
            {Object.entries(steps).map(([c, s]) => (
              <li key={c} className="row">
                <StepIcon s={s} /> <span className="small">{checkTitle(c as CheckId)}</span> <span className="xsmall subtle">{s === 'pending' ? 'waiting' : s === 'running' ? 'running…' : s === 'error' ? 'check error (see Output)' : s}</span>
              </li>
            ))}
          </ol>
          {!runningAll && (
            <p className="xsmall subtle">
              Details of each run are in the <button type="button" className="vts-linkbtn" onClick={() => ws.setDrawer('output')}>Output</button> drawer.
            </p>
          )}
        </Pane>
      )}
      {status.data &&
        GROUPS.map((g) => {
          const rows = g.items.map((id) => items.find((i) => i.id === id)).filter((x): x is GateItem => !!x);
          if (rows.length === 0) return null;
          return (
            <Pane key={g.id} title={g.title}>
              <p className="xsmall subtle" style={{ marginTop: 0 }}>{g.note}</p>
              <ul className="vts-gate" aria-label={g.title}>
                {rows.map((r) => (
                  <GateRow key={r.id} g={r} base={base} />
                ))}
              </ul>
            </Pane>
          );
        })}
      <Pane title="Refinement (ontology evolution)" actions={<Button size="sm" variant="ghost" loading={ws.running.has('refinement')} onClick={() => void ws.runCheck('refinement').catch(() => undefined)}>Check vs. published</Button>}>
        <p className="small muted" style={{ margin: 0 }}>
          When the ontology changes, the refinement check decides whether the new axioms preserve every interpretation of the published version (Definition 4). Results open in the{' '}
          <Link to={`${base}/semantics/ontology`}>ontology editor</Link> and the evidence list.
        </p>
      </Pane>
      <Pane title="Evidence history">
        <EvidenceHistory />
      </Pane>
    </EdPage>
  );
}
