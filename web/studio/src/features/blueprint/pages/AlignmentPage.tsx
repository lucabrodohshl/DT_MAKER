/**
 * Assurance → Alignment: is the Digital Twin View a faithful abstraction of the Physical System
 * View under the ontology and the two interpretations (semantic alignment, Definition 5)? The
 * verdict is the aligner's, run on exactly the artefacts pinned by this version; the page shows
 * PASS / FAIL / UNKNOWN / ERROR, strong vs weak alignment, the label equivalence and location
 * correspondence it found, the counterexample, lint findings, the input and compiled-view hashes,
 * the checker identity and the run duration. "Explain" asks the aligner (Z3 over the ontology)
 * how one PT/DT pair relates.
 */
import { useQuery } from '@tanstack/react-query';
import { ArrowLeftRight, FileSearch, Scale, ShieldCheck } from 'lucide-react';
import { useState, type ReactNode } from 'react';
import { Link } from 'react-router-dom';
import { blueprintRoute, useBlueprintModel, useBlueprintSemantics, useBlueprintStatus } from '@/api/blueprints';
import { api } from '@/api/client';
import { useEvidence } from '@/api/queries';
import type { EvidenceRecord } from '@/api/types';
import { Button, Callout, Dialog, ErrorBlock, HashChip, KeyValue, Skeleton, StatusBadge, TimeStamp } from '@/design';
import { RefLink } from '@/features/common/links';
import { RunCheckButton } from '../BlueprintWorkspace';
import { useEditor } from '../editor';
import { useModelFacts } from '../formalFacts';
import { EdPage, Pane } from '../ui';
import { useWorkspace } from '../workspace';

interface AlignmentDocument {
  format?: string;
  error?: string;
  aligner?: { name: string; source_digest: string; procedure: string; source_files?: string[] };
  compiled_views?: { pt_ir_sha256: string; dt_ir_sha256: string };
  verdict?: {
    aligned: boolean;
    counterexample?: { pt?: string; dt?: string };
    label_pairs?: number;
    final_relation_size?: number;
    smt_calls?: number;
    fixpoint_iterations?: number;
    pt_zones?: number;
    dt_zones?: number;
  };
  syntactic_baseline?: { aligned: boolean };
  modes?: { weak: string; strong: string; strong_reason?: string };
  internal_transitions?: { pt: number; dt: number };
  label_equivalence?: { pt: string; dt: string[] | string }[];
  location_consistency?: { dt: string; pt_equivalents: string[] }[];
  lint?: { clean: boolean; findings: { severity: string; code: string; message: string }[] };
  scope_notes?: string[];
  duration_ms?: number;
}

interface Finding {
  category: string;
  severity: string;
  message: string;
  links: { view: string; kind: string; name: string; index?: number }[];
}

export type VerdictWord = 'PASS' | 'FAIL' | 'UNKNOWN' | 'ERROR' | 'NOT RUN' | 'BLOCKED';

export function verdictTone(w: VerdictWord): 'ok' | 'critical' | 'warning' | 'neutral' {
  return w === 'PASS' ? 'ok' : w === 'FAIL' || w === 'ERROR' ? 'critical' : w === 'UNKNOWN' ? 'warning' : 'neutral';
}

/** The big verdict block shared by the Assurance pages. */
export function VerdictCard({ word, title, children, actions }: { word: VerdictWord; title: ReactNode; children?: ReactNode; actions?: ReactNode }) {
  return (
    <section className="vts-verdict" data-tone={verdictTone(word)} aria-label={`Verdict: ${word}`}>
      <div className="vts-verdict__word">{word}</div>
      <div className="vts-verdict__title">{title}</div>
      <div className="vts-verdict__body">{children}</div>
      {actions && <div className="vts-verdict__actions">{actions}</div>}
    </section>
  );
}

function duration(ms: number | undefined): string {
  if (ms === undefined) return 'not recorded';
  return ms < 1000 ? `${ms} ms` : `${(ms / 1000).toFixed(ms < 10_000 ? 2 : 1)} s`;
}

function ExplainDialog({ open, onOpenChange, initial }: { open: boolean; onOpenChange: (o: boolean) => void; initial: { pt: string; dt: string; kind: 'event' | 'location' } | null }) {
  const e = useEditor();
  const pt = useModelFacts('pt');
  const dt = useModelFacts('dt');
  const ont = useBlueprintSemantics(e.id, e.version, 'ontology');
  const ip = useBlueprintSemantics(e.id, e.version, 'pt_interpretation');
  const id = useBlueprintSemantics(e.id, e.version, 'dt_interpretation');
  const [kind, setKind] = useState<'event' | 'location'>(initial?.kind ?? 'event');
  const [a, setA] = useState(initial?.pt ?? '');
  const [b, setB] = useState(initial?.dt ?? '');
  const [prevInitial, setPrevInitial] = useState(initial);
  if (prevInitial !== initial) {
    setPrevInitial(initial);
    if (initial) {
      setKind(initial.kind);
      setA(initial.pt);
      setB(initial.dt);
    }
  }
  const texts = { ontology: ont.data?.artifact?.content, ptInterpretation: ip.data?.artifact?.content, dtInterpretation: id.data?.artifact?.content };
  const ready = !!texts.ontology && !!texts.ptInterpretation && !!texts.dtInterpretation && !!a && !!b;
  const q = useQuery({
    queryKey: ['blueprints', 'explain', e.id, e.version, kind, a, b, ont.data?.artifact?.contentSha256, ip.data?.artifact?.contentSha256, id.data?.artifact?.contentSha256],
    queryFn: () => api.post<{ equivalent: boolean; ptImpliesDt: boolean; dtImpliesPt: boolean }>('/authoring/alignment/explain', { ...texts, pt: a, dt: b, kind }),
    enabled: open && ready,
    retry: false,
  });
  const entry = (role: 'pt' | 'dt', key: string) => ((role === 'pt' ? ip : id).data?.artifact?.structure as { entries?: { key: string; formula: string }[] } | undefined)?.entries?.find((x) => x.key === key)?.formula;
  const yes = (v: boolean | undefined, label: string) => <StatusBadge tone={v === undefined ? 'neutral' : v ? 'ok' : 'neutral'} label={`${label}: ${v === undefined ? '…' : v ? 'yes' : 'no'}`} />;
  return (
    <Dialog open={open} onOpenChange={onOpenChange} wide title="Explain a PT/DT pair" description="The aligner relates labels whose interpretations are equivalent under the ontology axioms (Δ ⊨ I_P(a) ↔ I_D(b)). Decided by Z3, not by Studio.">
      <div className="stack">
        <div className="row-wrap">
          <select className="vts-select" value={kind} onChange={(x) => setKind(x.target.value as 'event' | 'location')} aria-label="Kind">
            <option value="event">Events (labels)</option>
            <option value="location">States (locations)</option>
          </select>
          <select className="vts-select" value={a} onChange={(x) => setA(x.target.value)} aria-label="Physical System View element">
            <option value="">PT {kind === 'event' ? 'label' : 'state'}…</option>
            {(kind === 'event' ? pt.labels : pt.locations).map((l) => (
              <option key={l}>{l}</option>
            ))}
          </select>
          <ArrowLeftRight size={16} aria-hidden="true" />
          <select className="vts-select" value={b} onChange={(x) => setB(x.target.value)} aria-label="Digital Twin View element">
            <option value="">DT {kind === 'event' ? 'label' : 'state'}…</option>
            {(kind === 'event' ? dt.labels : dt.locations).map((l) => (
              <option key={l}>{l}</option>
            ))}
          </select>
        </div>
        {a && b && (
          <div className="vts-fgrid">
            <div className="stack-sm">
              <span className="vts-label">I_P({a})</span>
              <code className="small">{entry('pt', a) ?? 'not interpreted'}</code>
            </div>
            <div className="stack-sm">
              <span className="vts-label">I_D({b})</span>
              <code className="small">{entry('dt', b) ?? 'not interpreted'}</code>
            </div>
          </div>
        )}
        {q.isError && <ErrorBlock error={q.error} compact />}
        {ready && !q.isError && (
          <div className="row-wrap">
            {q.isFetching ? <StatusBadge tone="info" label="Asking the aligner…" spin /> : <StatusBadge tone={q.data?.equivalent ? 'ok' : 'warning'} icon={Scale} label={q.data?.equivalent ? 'Equivalent: the aligner pairs them' : 'Not equivalent'} />}
            {yes(q.data?.ptImpliesDt, 'I_P ⇒ I_D')}
            {yes(q.data?.dtImpliesPt, 'I_D ⇒ I_P')}
          </div>
        )}
        {!texts.ontology && <Callout tone="info" title="No ontology">Pin an ontology and both interpretations first.</Callout>}
      </div>
    </Dialog>
  );
}

function Details({ ev, doc }: { ev: EvidenceRecord; doc: AlignmentDocument }) {
  const e = useEditor();
  const ptModel = useBlueprintModel(e.id, e.version, 'pt');
  const dtModel = useBlueprintModel(e.id, e.version, 'dt');
  const [explain, setExplain] = useState<{ pt: string; dt: string; kind: 'event' | 'location' } | null>(null);
  const diagnose = useQuery({
    queryKey: ['blueprints', 'align-diagnose', ev.id],
    queryFn: () =>
      api.post<{ diagnostics: Finding[] }>('/authoring/alignment/diagnose', { evidence: doc, ptModel: ptModel.data!.model, dtModel: dtModel.data!.model }),
    enabled: !!ptModel.data?.model && !!dtModel.data?.model && !doc.error,
    retry: false,
    staleTime: Infinity,
  });
  const route = (sub: string) => blueprintRoute(e.id, e.version, sub);
  const linkTo = (l: Finding['links'][number]) => {
    const role = l.view === 'pt' || l.view === 'PT' ? 'pt' : 'dt';
    return l.kind === 'location' ? route(`behavior/${role}?state=${encodeURIComponent(l.name)}`) : route(`behavior/${role}`);
  };
  const v = doc.verdict;
  const eq = doc.label_equivalence ?? [];
  const loc = doc.location_consistency ?? [];
  const lint = doc.lint?.findings ?? [];
  const findings = diagnose.data?.diagnostics ?? [];
  return (
    <div className="grid-main-side">
      <div className="stack">
        {findings.length > 0 && (
          <Pane title="Findings">
            <ul className="vts-findings">
              {findings.map((f, i) => (
                <li key={i}>
                  <span style={{ color: f.severity === 'error' ? 'var(--crit)' : f.severity === 'warning' ? 'var(--warn)' : 'var(--text-subtle)' }}>●</span>
                  <span>
                    {f.message} <span className="xsmall subtle">({f.category})</span>{' '}
                    {f.links.map((l, j) => (
                      <Link key={j} to={linkTo(l)} className="mono xsmall" style={{ marginRight: 6 }}>
                        {l.view.toUpperCase()} {l.name}
                      </Link>
                    ))}
                  </span>
                </li>
              ))}
            </ul>
          </Pane>
        )}
        <Pane title="Label equivalence E" actions={<Button size="sm" variant="ghost" icon={<FileSearch size={13} />} onClick={() => setExplain({ pt: '', dt: '', kind: 'event' })}>Explain a pair</Button>}>
          {eq.length === 0 ? (
            <p className="small muted">No label pairs (the aligner found no PT label equivalent to a DT label).</p>
          ) : (
            <table className="vts-table">
              <caption className="sr-only">PT labels and their equivalent DT labels</caption>
              <thead>
                <tr>
                  <th scope="col">Physical System View label</th>
                  <th scope="col">≡ Digital Twin View label(s)</th>
                  <th scope="col" aria-label="Explain" />
                </tr>
              </thead>
              <tbody>
                {eq.map((r) => {
                  const dts = Array.isArray(r.dt) ? r.dt : [r.dt];
                  return (
                    <tr key={r.pt}>
                      <td className="mono small">{r.pt}</td>
                      <td className="mono small">{dts.length ? dts.join(', ') : <span style={{ color: 'var(--crit)' }}>none</span>}</td>
                      <td>
                        <Button size="sm" variant="ghost" onClick={() => setExplain({ pt: r.pt, dt: dts[0] ?? '', kind: 'event' })}>
                          Why?
                        </Button>
                      </td>
                    </tr>
                  );
                })}
              </tbody>
            </table>
          )}
        </Pane>
        <Pane title="Location correspondence">
          {loc.length === 0 ? (
            <p className="small muted">No location data.</p>
          ) : (
            <table className="vts-table">
              <caption className="sr-only">DT states and the PT states they correspond to</caption>
              <thead>
                <tr>
                  <th scope="col">Digital Twin View state</th>
                  <th scope="col">PT state(s) with an equivalent interpretation</th>
                </tr>
              </thead>
              <tbody>
                {loc.map((r) => (
                  <tr key={r.dt}>
                    <td className="mono small">
                      <Link to={route(`behavior/dt?state=${encodeURIComponent(r.dt)}`)}>{r.dt}</Link>
                    </td>
                    <td className="mono small">{r.pt_equivalents.length ? r.pt_equivalents.join(', ') : <span className="subtle">none (informational)</span>}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          )}
          <p className="xsmall subtle">Condition I (location consistency) is informational: the aligner decides Conditions II/III over the zone graphs.</p>
        </Pane>
        {lint.length > 0 && (
          <Callout tone={doc.lint?.clean ? 'info' : 'warning'} title="Lint findings (constructs the aligner skips)">
            <ul style={{ margin: 0, paddingLeft: 16 }}>
              {lint.map((l, i) => (
                <li key={i}>
                  <span className="mono">{l.code}</span> {l.severity}: {l.message}
                </li>
              ))}
            </ul>
          </Callout>
        )}
        {(doc.scope_notes ?? []).length > 0 && (
          <details className="small">
            <summary>Scope of this verdict</summary>
            <ul className="muted" style={{ paddingLeft: 16 }}>
              {doc.scope_notes!.map((n) => (
                <li key={n}>{n}</li>
              ))}
            </ul>
          </details>
        )}
      </div>
      <div className="stack">
        <Pane title="Inputs (exactly what was checked)">
          <ul className="vts-list">
            {ev.inputs.map((i) => (
              <li key={i.role} className="stack-sm" style={{ gap: 2 }}>
                <span className="small">
                  <strong>{i.role.replace(/_/g, ' ')}</strong> <RefLink refId={i.ref} />
                </span>
                <HashChip value={i.sha256} label={`${i.role} sha256`} />
              </li>
            ))}
          </ul>
        </Pane>
        <Pane title="Compiled views">
          <KeyValue
            compact
            items={[
              ['PT view IR', <HashChip key="p" value={doc.compiled_views?.pt_ir_sha256} label="PT IR sha256" />],
              ['DT view IR', <HashChip key="d" value={doc.compiled_views?.dt_ir_sha256} label="DT IR sha256" />],
              ['Internal (τ) transitions', `PT ${doc.internal_transitions?.pt ?? '?'} · DT ${doc.internal_transitions?.dt ?? '?'}`],
            ]}
          />
        </Pane>
        <Pane title="Checker">
          <KeyValue
            compact
            items={[
              ['Aligner', <span key="a" className="small">{doc.aligner?.name ?? ev.checker}</span>],
              ['Source digest', <HashChip key="s" value={doc.aligner?.source_digest} label="aligner source digest" />],
              ['Procedure', <span key="p" className="mono xsmall">{doc.aligner?.procedure ?? '—'}</span>],
              ['Duration', duration(doc.duration_ms)],
              ['Evidence', <HashChip key="e" value={ev.evidenceSha256} label="evidence sha256" />],
            ]}
          />
        </Pane>
        {v && (
          <Pane title="Statistics">
            <KeyValue
              compact
              items={[
                ['Label pairs |E|', String(v.label_pairs ?? '?')],
                ['Zones PT / DT', `${v.pt_zones ?? '?'} / ${v.dt_zones ?? '?'}`],
                ['Final relation size', String(v.final_relation_size ?? '?')],
                ['Fixpoint iterations', String(v.fixpoint_iterations ?? '?')],
                ['SMT calls', String(v.smt_calls ?? '?')],
                ['Syntactic baseline', doc.syntactic_baseline?.aligned ? 'aligned' : 'not aligned'],
              ]}
            />
          </Pane>
        )}
      </div>
      <ExplainDialog open={!!explain} onOpenChange={(o) => !o && setExplain(null)} initial={explain} />
    </div>
  );
}

export default function AlignmentPage() {
  const e = useEditor();
  const ws = useWorkspace();
  const status = useBlueprintStatus(e.id, e.version);
  const evidenceId = status.data?.alignment.evidenceId ?? undefined;
  const ev = useEvidence(evidenceId);
  const gate = status.data?.readiness.items.find((i) => i.id === 'alignment');
  const lastRun = ws.output.find((o) => o.title === 'Semantic alignment (aligner)');
  const doc = (ev.data?.document ?? null) as AlignmentDocument | null;

  let word: VerdictWord = 'NOT RUN';
  if (gate?.state === 'blocked') word = 'BLOCKED';
  if (ev.data) word = ev.data.outcome === 'pass' ? 'PASS' : ev.data.outcome === 'fail' ? 'FAIL' : ev.data.outcome === 'error' ? 'ERROR' : 'UNKNOWN';
  const modes = doc?.modes;
  const strong = modes?.strong === 'aligned';
  return (
    <EdPage
      title="Semantic alignment"
      wide
      description="Checks that the Digital Twin View is a faithful abstraction of the Physical System View under the ontology and interpretations."
      actions={<RunCheckButton check="alignment" label={ev.data ? 'Re-run aligner' : 'Run aligner'} variant="primary" />}
      guide={
        <>
          The aligner pairs PT and DT events whose interpretations are equivalent under the ontology, then checks that every timed behaviour of one view is matched by the other (weak timed bisimulation). <strong>Strong</strong> alignment also matches internal (τ) steps; it is decided only when neither view has internal transitions.
        </>
      }
    >
      {status.isPending ? (
        <Skeleton lines={4} />
      ) : status.isError ? (
        <ErrorBlock error={status.error} onRetry={() => void status.refetch()} />
      ) : (
        <VerdictCard
          word={word}
          title={
            word === 'PASS'
              ? `Aligned — ${strong ? 'STRONG' : 'WEAK'} alignment`
              : word === 'FAIL'
                ? 'Not aligned'
                : word === 'ERROR'
                  ? 'The aligner could not decide'
                  : word === 'BLOCKED'
                    ? 'Cannot run yet'
                    : word === 'UNKNOWN'
                      ? 'Undecided'
                      : 'Not run for the current inputs'
          }
          actions={ev.data && <Link to={`/studio/verification/${encodeURIComponent(ev.data.id)}`} className="small">Evidence {ev.data.id}</Link>}
        >
          {ev.data ? (
            <div className="stack-sm">
              <p className="small" style={{ margin: 0 }}>{ev.data.summary}</p>
              <div className="row-wrap">
                <StatusBadge tone={modes?.weak === 'aligned' ? 'ok' : 'critical'} icon={ShieldCheck} label={`Weak: ${modes?.weak === 'aligned' ? 'PASS' : 'FAIL'}`} />
                <StatusBadge
                  tone={modes?.strong === 'aligned' ? 'ok' : modes?.strong === 'not_decidable' ? 'warning' : 'critical'}
                  label={`Strong: ${modes?.strong === 'aligned' ? 'PASS' : modes?.strong === 'not_decidable' ? 'UNKNOWN' : 'FAIL'}`}
                  title={modes?.strong_reason}
                />
                <span className="xsmall subtle">
                  checked <TimeStamp value={ev.data.createdAt} relative /> by {ev.data.createdBy} · {duration(doc?.duration_ms)}
                </span>
              </div>
              {modes?.strong_reason && <span className="xsmall muted">{modes.strong_reason}</span>}
              {word === 'FAIL' && doc?.verdict?.counterexample?.pt && (
                <Callout tone="critical" title="Counterexample">
                  The Physical System View can perform <code>{doc.verdict.counterexample.pt}</code> with no matching Digital Twin View behaviour
                  {doc.verdict.counterexample.dt ? (
                    <>
                      {' '}
                      (closest DT label <code>{doc.verdict.counterexample.dt}</code>)
                    </>
                  ) : null}
                  . Check the interpretations of both labels, or the guards and resets that time them.
                </Callout>
              )}
              {doc?.error && <Callout tone="critical" title="Check error">{doc.error}</Callout>}
            </div>
          ) : (
            <div className="stack-sm">
              <p className="small" style={{ margin: 0 }}>{gate?.detail ?? 'Run the aligner on the pinned views, ontology and interpretations.'}</p>
              {lastRun && lastRun.outcome === 'error' && <Callout tone="critical" title="Last attempt failed">{lastRun.summary}</Callout>}
              {gate?.state === 'blocked' && (
                <p className="xsmall muted">
                  The aligner needs all five formal artefacts: <Link to={blueprintRoute(e.id, e.version, 'behavior/pt')}>PT view</Link>, <Link to={blueprintRoute(e.id, e.version, 'behavior/dt')}>DT view</Link>,{' '}
                  <Link to={blueprintRoute(e.id, e.version, 'semantics/ontology')}>ontology</Link> and <Link to={blueprintRoute(e.id, e.version, 'semantics/interpretations')}>both interpretations</Link>.
                </p>
              )}
            </div>
          )}
        </VerdictCard>
      )}
      {ev.isPending && evidenceId && <Skeleton lines={6} />}
      {ev.data && doc && <Details ev={ev.data} doc={doc} />}
    </EdPage>
  );
}
