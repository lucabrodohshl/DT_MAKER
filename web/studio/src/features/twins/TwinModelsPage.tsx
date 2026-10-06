/**
 * Engineering › Models: the twin's PT and DT views as graphical timed automata.
 *
 * Diagram (default) · Details · Source (UPPAAL XML, advanced) · Verification.
 * View: PT only · DT only · Side by side · Alignment (correspondences from the aligner's
 * evidence in the deployed package, never from name matching).
 *
 * The DT view is drawn from the Twin IR the kernel executes, so live highlighting is exact;
 * the PT view from the canonical model produced by the engine's importer. Read-only:
 * dragging rearranges the picture only; behaviour changes go through "Edit new version in Studio".
 */
import { useQuery } from '@tanstack/react-query';
import { ExternalLink, PencilRuler } from 'lucide-react';
import { useMemo, useState } from 'react';
import { Link, useSearchParams } from 'react-router-dom';
import { api } from '@/api/client';
import { useVersion } from '@/api/queries';
import type { TwinDetail } from '@/api/types';
import { runtimeApi, useLedger, useRuntimeModel, useRuntimePackage, useRuntimeState } from '@/runtime/client';
import type { LedgerRecord, RuntimeState, TwinIr, WhatIfResult } from '@/runtime/types';
import { Callout, EmptyState, ErrorBlock, HashChip, KeyValue, PageHeader, Panel, QueryState, Segmented, StatusBadge, Tabs, TrustBadge } from '@/design';
import { CodeEditor } from '@/editor/CodeEditor';
import { useTwinScope } from '@/app/twinScope';
import { TaDiagram, type TaOverlay, type TaSelection } from '@/features/engineering/ta/TaDiagram';
import { fromCanonical, fromIr, layoutFrom, type ImportResult, type TaGraph } from '@/features/engineering/ta/taModel';

type Role = 'pt' | 'dt';
type View = Role | 'side' | 'align';
type Tab = 'diagram' | 'details' | 'source' | 'verification';

const binding = (twin: TwinDetail, role: Role) => twin.bindings.find((b) => b.role === `${role}_model`);

/** The model artefact's content, imported into the canonical form (and its file layout). */
function useImported(twin: TwinDetail, role: Role) {
  const b = binding(twin, role);
  const v = useVersion(b?.artifactId ?? '', b?.version ?? 0);
  const imported = useQuery({
    queryKey: ['authoring', 'import', b?.ref, v.data?.contentSha256],
    queryFn: () => api.post<ImportResult>('/authoring/import', { filename: `${b!.artifactId}.xml`, content: v.data!.content }),
    enabled: !!b && !!v.data,
    staleTime: Infinity,
  });
  return { binding: b, version: v, imported };
}

/** DT structure: the IR the runtime executes, or the deployed package's IR when it is not connected. */
function useDtIr(twin: TwinDetail, connected: boolean) {
  const live = useRuntimeModel(connected ? twin.id : null);
  const pkg = useQuery({
    queryKey: ['packages', 'ir', twin.deployment?.packageId],
    queryFn: () => api.get<TwinIr>(`/packages/${encodeURIComponent(twin.deployment!.packageId)}/ir`),
    enabled: !!twin.deployment && (!connected || live.isError),
    staleTime: Infinity,
  });
  return live.data ? { ir: live.data, source: 'runtime' as const } : pkg.data ? { ir: pkg.data, source: 'package' as const } : null;
}

function stepRecords(records: LedgerRecord[] | undefined) {
  return (records ?? []).filter((r) => r.body.kind === 'step');
}
type Branch = { transition: string; source: string; target: string };
const branchOf = (r: LedgerRecord) => ((r.body as unknown as { outcome?: { branches?: Branch[] } }).outcome?.branches ?? [])[0];

function Inspector({ role, graph, selection, state, ir, ledger, availability, onSelect, unit }: {
  role: Role; graph: TaGraph; selection: TaSelection | null; state: RuntimeState | null; ir: TwinIr | null;
  ledger: LedgerRecord[]; availability: WhatIfResult | undefined; onSelect: (s: TaSelection) => void; unit: string;
}) {
  if (!selection) {
    return <p className="small muted">Select a state or a transition in the diagram to inspect it.</p>;
  }
  if (selection.kind === 'location') {
    const l = graph.locations.find((x) => x.id === selection.id);
    if (!l) return null;
    const out = graph.edges.filter((e) => e.source === l.id);
    const inc = graph.edges.filter((e) => e.target === l.id);
    const live = role === 'dt' ? state?.configurations.filter((c) => c.location === l.id) ?? [] : [];
    const props = role === 'dt' ? ir?.propositions.filter((p) => p.location === l.id) ?? [] : [];
    const entered = role === 'dt' ? ledger.filter((r) => branchOf(r)?.target === l.id) : [];
    const last = entered[entered.length - 1];
    return (
      <div className="stack">
        <div><div className="xsmall subtle">LOCATION</div><strong className="mono">{l.id}</strong> {l.initial && <StatusBadge tone="info" label="initial" />} {live.length > 0 && <StatusBadge tone="ok" label="current" />}</div>
        <KeyValue compact items={[
          ['Invariant', l.invariant ? <span className="mono small">{l.invariant}</span> : <span className="muted small">none (time may pass freely)</span>],
          ...(props.length ? [['Propositions', <div key="p" className="stack-sm">{props.map((p) => <div key={p.id} className="small"><span className="mono">{p.id}</span><div className="xsmall muted mono">{p.interpretation}</div></div>)}</div>] as [string, React.ReactNode]] : []),
          ...(live.length ? [['Clocks now', <span key="c" className="mono small">{Object.entries(live[0]!.clocks).map(([k, v]) => `${k} = ${v.text}`).join(', ')}</span>] as [string, React.ReactNode]] : []),
          ...(live.length && state?.deadline ? [['Must leave by', `t = ${state.deadline.text} ${unit}`] as [string, React.ReactNode]] : []),
          ...(role === 'dt' ? [['Entered', last ? `${entered.length}× in this execution; last at t = ${last.body.time_after ?? '?'} ticks (record #${last.seq})` : 'not in this execution'] as [string, React.ReactNode]] : []),
        ]} />
        <div>
          <div className="xsmall subtle">OUTGOING</div>
          <ul className="vts-list">{out.map((e) => <li key={e.id} className="small"><button type="button" className="vts-linkbtn mono" onClick={() => onSelect({ kind: 'edge', id: e.id })}>{e.label}</button> → {e.target}</li>)}{out.length === 0 && <li className="small muted">none</li>}</ul>
        </div>
        <div>
          <div className="xsmall subtle">INCOMING</div>
          <ul className="vts-list">{inc.map((e) => <li key={e.id} className="small">{e.source} → <button type="button" className="vts-linkbtn mono" onClick={() => onSelect({ kind: 'edge', id: e.id })}>{e.label}</button></li>)}{inc.length === 0 && <li className="small muted">none</li>}</ul>
        </div>
      </div>
    );
  }
  const e = graph.edges.find((x) => x.id === selection.id);
  if (!e) return null;
  const meaning = role === 'dt' ? ir?.event_interpretations.find((x) => x.label === e.label)?.formula : undefined;
  const enabled = role === 'dt' ? state?.enabled.find((x) => x.transition === e.id) : undefined;
  const alt = role === 'dt' ? availability?.final.availability.flatMap((a) => a.alternatives).find((a) => a.transition === e.id) : undefined;
  const fired = role === 'dt' ? ledger.filter((r) => branchOf(r)?.transition === e.id) : [];
  const current = state?.configurations.map((c) => c.location) ?? [];
  return (
    <div className="stack">
      <div><div className="xsmall subtle">TRANSITION</div><strong className="mono">{e.label}</strong> <span className="small">{e.source} → {e.target}</span></div>
      <KeyValue compact items={[
        ['Id', <span key="i" className="mono xsmall">{e.id}</span>],
        ['Guard', e.guard ? <span key="g" className="mono small">{e.guard}</span> : <span key="g" className="muted small">true</span>],
        ['Resets', e.resets.length ? <span key="r" className="mono small">{e.resets.map((x) => `${x} := 0`).join(', ')}</span> : <span key="r" className="muted small">none</span>],
        ['Updates', <span key="u" className="muted small">none (clock-only timed automaton)</span>],
        ...(meaning !== undefined ? [['Meaning (I_D)', <span key="m" className="mono xsmall">{meaning}</span>] as [string, React.ReactNode]] : []),
      ]} />
      {role === 'dt' && state && (
        enabled ? (
          <Callout tone={enabled.enabled_now ? 'ok' : 'info'} title={enabled.enabled_now ? 'Enabled now' : 'Enabled after a delay'}>
            Admissible delay: +{enabled.window.earliest.text}{enabled.window.latest ? ` … +${enabled.window.latest.text}` : ' or later, no upper bound'} {unit} (kernel window).
          </Callout>
        ) : !current.includes(e.source) ? (
          <Callout tone="neutral" title="Not enabled">The twin is in {current.join(' or ')}; this transition leaves {e.source}.</Callout>
        ) : (
          <Callout tone="warning" title="Not enabled from the current state">
            {alt?.factors.filter((f) => f.never).map((f) => <div key={f.atom} className="small">{f.origin.replace('_', ' ')} <span className="mono">{f.atom}</span> cannot hold (now {f.value_now.text}).</div>)}
            {alt && !alt.factors.some((f) => f.never) && <div className="small">No admissible delay satisfies every constraint at once.</div>}
          </Callout>
        )
      )}
      {role === 'dt' && (
        <div>
          <div className="xsmall subtle">RECENT EXECUTIONS (this execution's ledger)</div>
          {fired.length === 0 ? <p className="small muted">Not taken in this execution.</p> : (
            <ul className="vts-list">{fired.slice(-5).reverse().map((r) => <li key={r.seq} className="small mono">#{r.seq} · t = {r.body.time_after} ticks</li>)}</ul>
          )}
        </div>
      )}
      {role === 'dt' && <Link className="small" to="../../predict/what-if">Explore when it can fire in What-if</Link>}
    </div>
  );
}

function ModelPane({ role, graph, layout, overlay, state, ir, ledger, availability, layoutKey, unit, height, withInspector }: {
  role: Role; graph: TaGraph; layout?: ReturnType<typeof layoutFrom>; overlay?: TaOverlay; state: RuntimeState | null; ir: TwinIr | null;
  ledger: LedgerRecord[]; availability: WhatIfResult | undefined; layoutKey: string; unit: string; height: number; withInspector: boolean;
}) {
  const [sel, setSel] = useState<TaSelection | null>(null);
  const diagram = <TaDiagram graph={graph} layout={layout} layoutKey={layoutKey} overlay={overlay} selection={sel} onSelect={setSel} height={height} />;
  if (!withInspector) return diagram;
  return (
    <div className="grid-main-side" style={{ gridTemplateColumns: 'minmax(0, 1fr) 340px' }}>
      <div>{diagram}</div>
      <Panel title="Inspector">
        <Inspector role={role} graph={graph} selection={sel} state={state} ir={ir} ledger={ledger} availability={availability} onSelect={setSel} unit={unit} />
      </Panel>
    </div>
  );
}

function DetailsTables({ graph }: { graph: TaGraph }) {
  return (
    <div className="grid-2">
      <Panel title={`Locations (${graph.locations.length})`} flush>
        <table className="vts-table"><thead><tr><th>Location</th><th>Invariant</th></tr></thead>
          <tbody>{graph.locations.map((l) => <tr key={l.id}><td className="mono small">{l.id}{l.initial ? ' (initial)' : ''}</td><td className="mono small">{l.invariant || '—'}</td></tr>)}</tbody>
        </table>
      </Panel>
      <Panel title={`Transitions (${graph.edges.length})`} flush>
        <table className="vts-table"><thead><tr><th>Event</th><th>From → to</th><th>Guard</th><th>Resets</th></tr></thead>
          <tbody>{graph.edges.map((e) => <tr key={e.id}><td className="mono small">{e.label}</td><td className="small">{e.source} → {e.target}</td><td className="mono small">{e.guard || 'true'}</td><td className="mono small">{e.resets.join(', ') || '—'}</td></tr>)}</tbody>
        </table>
      </Panel>
    </div>
  );
}

export default function TwinModelsPage({ behaviourOnly = false }: { behaviourOnly?: boolean } = {}) {
  const { twin, runtimeConnected } = useTwinScope()!;
  const [params, setParams] = useSearchParams();
  const view: View = behaviourOnly ? 'dt' : ((params.get('view') as View) || 'dt');
  const tab: Tab = behaviourOnly ? 'diagram' : ((params.get('tab') as Tab) || 'diagram');
  const set = (k: string, v: string) => { const p = new URLSearchParams(params); p.set(k, v); setParams(p, { replace: true }); };
  const unit = twin.presentation.timeUnit ?? '';

  const pt = useImported(twin, 'pt');
  const dt = useImported(twin, 'dt');
  const dtIr = useDtIr(twin, runtimeConnected);
  const state = useRuntimeState(runtimeConnected ? twin.id : null);
  const ledger = useLedger(runtimeConnected ? twin.id : null, '', 0, 'step', 200);
  const pkg = useRuntimePackage(runtimeConnected ? twin.id : null);
  const availability = useQuery({
    queryKey: ['runtime', twin.id, 'what-if', 'availability', state.data?.ledger.records],
    queryFn: () => runtimeApi(twin.id).post<WhatIfResult>('/runtime/what-if', { start: { kind: 'current' }, steps: [] }),
    enabled: runtimeConnected && !!state.data,
  });

  const ptGraph = useMemo(() => (pt.imported.data?.model ? fromCanonical(pt.imported.data.model) : null), [pt.imported.data]);
  const dtGraph = useMemo(() => (dtIr ? fromIr(dtIr.ir) : dt.imported.data?.model ? fromCanonical(dt.imported.data.model) : null), [dtIr, dt.imported.data]);
  const s = state.data ?? null;
  const lastTaken = s?.last_transition?.transition;
  const dtOverlay: TaOverlay = useMemo(() => ({
    current: new Set(s?.configurations.map((c) => c.location) ?? []),
    enabledNow: new Set(s?.enabled.filter((e) => e.enabled_now).map((e) => e.transition) ?? []),
    enabledLater: new Set(s?.enabled.filter((e) => !e.enabled_now).map((e) => e.transition) ?? []),
    recent: new Set(lastTaken ? [lastTaken] : []),
  }), [s, lastTaken]);

  // Alignment: label correspondences recorded by the aligner in the running package.
  const pairs = pkg.data?.alignment?.label_equivalence ?? [];
  const [pair, setPair] = useState<number | null>(null);
  const chosen = pair !== null ? pairs[pair] : undefined;
  const dtLabels = new Set(chosen ? (Array.isArray(chosen.dt) ? chosen.dt : [chosen.dt]) : []);
  const ptEmph: TaOverlay = { emphasis: new Set(ptGraph?.edges.filter((e) => chosen && e.label === chosen.pt).map((e) => e.id) ?? []) };
  const dtEmph: TaOverlay = { ...dtOverlay, emphasis: new Set(dtGraph?.edges.filter((e) => dtLabels.has(e.label)).map((e) => e.id) ?? []) };

  const roleBinding = binding(twin, view === 'pt' ? 'pt' : 'dt');
  const studio = roleBinding ? `/studio/models/${encodeURIComponent(roleBinding.artifactId)}/versions/${roleBinding.version}` : '/studio/models';
  const ledgerRecords = stepRecords(ledger.data?.records);
  const paneProps = { state: s, ir: dtIr?.ir ?? null, ledger: ledgerRecords, availability: availability.data, unit };

  const pane = (role: Role, opts: { height: number; withInspector: boolean; overlay?: TaOverlay }) => {
    const g = role === 'pt' ? ptGraph : dtGraph;
    const src = role === 'pt' ? pt : dt;
    if (!g) {
      return src.imported.error ? <ErrorBlock error={src.imported.error} />
        : src.imported.data && !src.imported.data.model ? (
          <Callout tone="warning" title="The model uses constructs the viewer cannot show">
            {src.imported.data.diagnostics.slice(0, 5).map((d, i) => <div key={i} className="small">{d.code}: {d.message}{d.line ? ` (line ${d.line})` : ''}</div>)}
            <div className="small">The source is available in the Source tab.</div>
          </Callout>
        ) : <p className="small muted">Loading {role.toUpperCase()} view…</p>;
    }
    return (
      <ModelPane role={role} graph={g} layout={layoutFrom(src.imported.data)} layoutKey={`${twin.id}:${src.binding?.ref ?? role}`} overlay={opts.overlay ?? (role === 'dt' ? dtOverlay : undefined)} height={opts.height} withInspector={opts.withInspector} {...paneProps} />
    );
  };

  if (behaviourOnly) {
    return (
      <div className="vts-page stack">
        <PageHeader title="Behavioural model" meta={<span>The DT view executed by the kernel{dtIr?.source === 'package' ? ' (from the deployed package: runtime not connected)' : ', with live state'}</span>}
          actions={<Link className="vts-btn" to={`../engineering/models?view=dt`}>Models, source & verification</Link>} />
        {pane('dt', { height: 600, withInspector: true })}
      </div>
    );
  }

  return (
    <div className="vts-page stack">
      <PageHeader
        title="Models"
        meta={<span>PT and DT views of {twin.name} as timed automata. Read-only: deployed behaviour changes only through a new verified version.</span>}
        actions={<Link className="vts-btn" to={studio}><PencilRuler size={14} /> Edit new version in Studio <ExternalLink size={12} /></Link>}
      />
      <div className="row-wrap" style={{ gap: 12 }}>
        <Segmented label="Model view" value={view} onChange={(v) => set('view', v)} options={[{ id: 'pt', label: 'PT view' }, { id: 'dt', label: 'DT view' }, { id: 'side', label: 'Side by side' }, { id: 'align', label: 'Alignment' }]} />
        <div className="grow" />
        {roleBinding && <span className="small muted">{roleBinding.ref} <HashChip value={roleBinding.sha256} label="content hash" /></span>}
      </div>
      <Tabs label="Model tabs" value={tab} onChange={(t) => set('tab', t)} tabs={[{ id: 'diagram', label: 'Diagram' }, { id: 'details', label: 'Details' }, { id: 'source', label: 'Source' }, { id: 'verification', label: 'Verification' }]} />

      {tab === 'diagram' && (view === 'pt' || view === 'dt') && pane(view, { height: 600, withInspector: true })}
      {tab === 'diagram' && view === 'side' && (
        <div className="grid-2">
          <Panel title="PT view (physical behaviour)">{pane('pt', { height: 520, withInspector: false })}</Panel>
          <Panel title="DT view (executed by the kernel)">{pane('dt', { height: 520, withInspector: false })}</Panel>
        </div>
      )}
      {tab === 'diagram' && view === 'align' && (
        <div className="stack">
          <Panel title="Label correspondences" subtitle="Recorded by SemPTDTAlignmentICSE in the deployed package's alignment evidence. Select one to highlight it in both views." flush>
            <QueryState query={pkg} isEmpty={() => pairs.length === 0} empty={<EmptyState compact title="No correspondences recorded">The running package's alignment evidence lists no label equivalences.</EmptyState>}>
              {() => (
                <table className="vts-table">
                  <thead><tr><th>PT event</th><th>DT event(s)</th><th /></tr></thead>
                  <tbody>{pairs.map((p, i) => (
                    <tr key={i} className={pair === i ? 'is-selected' : ''}>
                      <td className="mono small">{p.pt}</td>
                      <td className="mono small">{Array.isArray(p.dt) ? p.dt.join(', ') : p.dt}</td>
                      <td><button type="button" className="vts-linkbtn small" onClick={() => setPair(pair === i ? null : i)}>{pair === i ? 'Clear' : 'Highlight'}</button></td>
                    </tr>
                  ))}</tbody>
                </table>
              )}
            </QueryState>
          </Panel>
          <div className="grid-2">
            <Panel title="PT view">{pane('pt', { height: 480, withInspector: false, overlay: ptEmph })}</Panel>
            <Panel title="DT view">{pane('dt', { height: 480, withInspector: false, overlay: dtEmph })}</Panel>
          </div>
        </div>
      )}

      {tab === 'details' && (
        <div className="stack">
          {(view === 'pt' || view === 'side' || view === 'align') && ptGraph && <><h2 className="vts-section-title">PT view</h2><DetailsTables graph={ptGraph} /></>}
          {(view === 'dt' || view === 'side' || view === 'align') && dtGraph && <><h2 className="vts-section-title">DT view</h2><DetailsTables graph={dtGraph} /></>}
        </div>
      )}

      {tab === 'source' && (
        <Panel title={`Source: ${roleBinding?.ref ?? ''}`} subtitle="The artefact exactly as stored (advanced view)">
          {(() => {
            const v = (view === 'pt' ? pt : dt).version;
            return v.data ? <CodeEditor value={v.data.content} resetKey={v.data.ref} readOnly language="xml" ariaLabel="Model source" height={560} /> : <QueryState query={v}>{() => null}</QueryState>;
          })()}
        </Panel>
      )}

      {tab === 'verification' && (
        <div className="grid-2">
          <Panel title="Alignment & compilation">
            <div className="stack-sm">
              <TrustBadge state={twin.trust.alignment.state} label="Semantic alignment" />
              <p className="small muted">{twin.trust.alignment.detail}</p>
              <TrustBadge state={twin.trust.compilation.state} label="Compilation" />
              <p className="small muted">{twin.trust.compilation.detail}</p>
            </div>
          </Panel>
          <Panel title="Running package">
            <QueryState query={pkg}>
              {(p) => (
                <KeyValue compact items={[
                  ['Aligned', p.alignment.aligned ? 'yes' : 'no'],
                  ['Lint clean', p.manifest.verification.lint_clean ? 'yes' : 'no'],
                  ['Translation validated', p.manifest.verification.translation_validated ? 'yes' : 'no'],
                  ['Event deterministic', p.manifest.verification.event_deterministic ? 'yes' : 'no'],
                  ['IR', <HashChip key="ir" value={p.ir_sha256} />],
                ]} />
              )}
            </QueryState>
          </Panel>
        </div>
      )}
    </div>
  );
}
