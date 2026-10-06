/**
 * Behaviour of a twin: current state (operator view), behavioural model graph,
 * semantic facts (meaning of observations) and conformance. All behavioural
 * conclusions come from the runtime kernel; meaning from the ontology services.
 */
import { HelpCircle } from 'lucide-react';
import { useMemo, useState } from 'react';
import { useSearchParams } from 'react-router-dom';
import type { TwinDetail } from '@/api/types';
import {
  Button,
  Callout,
  EmptyState,
  Formula,
  KeyValue,
  LogicalTimeText,
  Panel,
  QueryState,
  RuntimeUnavailable,
  StatusBadge,
  Tabs,
  TONE_ICON,
  TruthBadge,
  toneOf,
} from '@/design';
import { useAssetContext } from '@/features/assets/AssetLayout';
import { RefLink } from '@/features/common/links';
import { WhyDrawer } from '@/features/semantics/WhyDrawer';
import { useSemanticFacts } from '@/features/semantics/useSemanticFacts';
import type { RuntimeState, TwinIr } from '@/runtime/types';
import { ticksToText } from '@/runtime/time';
import { BehaviorGraph, actionLabel, conjunction, type Selection } from './BehaviorGraph';
import { ConformanceView } from './ConformanceView';
import { useBehavior } from './useBehavior';

type View = 'state' | 'graph' | 'facts' | 'conformance';

function CurrentStateView({ twin, state, ir, onWhy }: { twin: TwinDetail; state: RuntimeState; ir: TwinIr | null; onWhy: (key: string) => void }) {
  const unit = twin.presentation.timeUnit;
  const locations = state.configurations.map((c) => c.location);
  const enabledLabels = new Set(state.enabled.map((e) => e.label));
  const alphabet = [...new Set(ir?.transitions.map((t) => actionLabel(t.action)) ?? [])].filter((l) => !l.startsWith('τ'));
  const unavailable = alphabet.filter((l) => !enabledLabels.has(l));
  const pkg = twin.package;
  return (
    <div className="grid-main-side">
      <div className="stack">
        <Panel title="Current mode" subtitle="Committed semantic state of the verified kernel">
          <div className="stack">
            <div className="row-wrap">
              {state.configurations.map((c) => {
                const p = twin.presentation.states?.[c.location];
                const tone = toneOf(p?.tone);
                return (
                  <div key={c.location} className="row">
                    <StatusBadge tone={tone} icon={TONE_ICON[tone]} label={p?.label ?? c.location} size="lg" />
                    <Button size="sm" variant="ghost" icon={<HelpCircle size={13} />} onClick={() => onWhy(c.location)}>
                      Why?
                    </Button>
                  </div>
                );
              })}
            </div>
            {!state.deterministic && (
              <Callout tone="info" title="Several states are possible">
                The observations so far are consistent with {locations.length} model states. The kernel keeps all of them
                instead of guessing; they are all shown.
              </Callout>
            )}
            {state.failed && <Callout tone="critical" title="Execution failed">The runtime stopped accepting inputs for this execution.</Callout>}
            <KeyValue
              items={[
                ['Logical time', <LogicalTimeText key="t" text={state.time.text} unit={unit} />],
                ...(state.deadline ? [['Must leave the state by', <LogicalTimeText key="d" text={state.deadline.text} unit={unit} />] as [string, React.ReactNode]] : []),
                [
                  'Clocks',
                  <span key="c" className="mono small">
                    {Object.entries(state.configurations[0]?.clocks ?? {}).map(([k, v]) => `${k} = ${v.text}`).join(', ')}
                  </span>,
                ],
                ['Execution', <span key="s" className="mono small">{state.session}</span>],
                ['Ledger', `${state.ledger.records} records`],
              ]}
            />
          </div>
        </Panel>
        <Panel title="Available next actions" subtitle="Admissible transitions reported by the kernel">
          {state.enabled.length === 0 ? (
            <p className="small muted">No transition leaves the current state.</p>
          ) : (
            <table className="vts-table">
              <caption className="sr-only">Enabled transitions</caption>
              <thead>
                <tr>
                  <th scope="col">Action / event</th>
                  <th scope="col">Leads to</th>
                  <th scope="col">Guard</th>
                  <th scope="col">Status</th>
                </tr>
              </thead>
              <tbody>
                {state.enabled.map((t) => (
                  <tr key={`${t.member}-${t.transition}`}>
                    <td>
                      <span className="strong">{twin.presentation.events?.[t.label.replace(/[!?]$/, '')]?.label ?? t.label}</span>
                      <div className="mono xsmall subtle">{t.label}</div>
                    </td>
                    <td>{twin.presentation.states?.[t.target]?.label ?? t.target}</td>
                    <td><span className="mono xsmall">{t.guard}</span></td>
                    <td>
                      {t.enabled_now ? (
                        <StatusBadge tone="ok" label="Admissible now" />
                      ) : (
                        <StatusBadge
                          tone="info"
                          label={`Admissible for t ∈ [${t.window.earliest.text}, ${t.window.latest?.text ?? '∞'}]`}
                        />
                      )}
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          )}
          {unavailable.length > 0 && (
            <div style={{ marginTop: 12 }}>
              <span className="vts-label">Not admissible in this state</span>
              <div className="row-wrap" style={{ marginTop: 4 }}>
                {unavailable.map((l) => (
                  <StatusBadge key={l} tone="neutral" label={twin.presentation.events?.[l.replace(/[!?]$/, '')]?.label ?? l} title="The model has no transition with this label from the current state" />
                ))}
              </div>
            </div>
          )}
        </Panel>
      </div>
      <div className="stack">
        <Panel title="Active propositions" subtitle="Observable labels of the current state">
          <ul className="vts-list">
            {state.propositions.map((p) => (
              <li key={p.id} className="stack-sm" style={{ gap: 2 }}>
                <span className="mono small strong">{p.id}</span>
                <Formula>{p.interpretation}</Formula>
              </li>
            ))}
          </ul>
        </Panel>
        <Panel title="Defined by">
          <KeyValue
            compact
            items={[
              ['Model', twin.bindings.find((b) => b.role === 'dt_model') ? <RefLink refId={twin.bindings.find((b) => b.role === 'dt_model')!.ref} kind="dt_model" /> : '—'],
              ['Ontology', twin.bindings.find((b) => b.role === 'ontology') ? <RefLink refId={twin.bindings.find((b) => b.role === 'ontology')!.ref} kind="ontology" /> : '—'],
              ['Interpretation', twin.bindings.find((b) => b.role === 'dt_interpretation') ? <RefLink refId={twin.bindings.find((b) => b.role === 'dt_interpretation')!.ref} kind="interpretation" /> : '—'],
              ['Package', pkg?.id ?? '—'],
            ]}
          />
        </Panel>
      </div>
    </div>
  );
}

function SelectionDetails({ ir, selection, state, twin, stepRecords }: { ir: TwinIr; selection: Selection | null; state: RuntimeState | null; twin: TwinDetail; stepRecords: { body: { input?: { at: number }; outcome?: { branches?: { transition: string }[] } } }[] }) {
  if (!selection) return <p className="small muted">Select a state or a transition in the graph to inspect it.</p>;
  if (selection.kind === 'location') {
    const loc = ir.locations.find((l) => l.id === selection.id);
    if (!loc) return null;
    const prop = ir.propositions.find((p) => p.location === loc.id);
    const isCurrent = state?.configurations.some((c) => c.location === loc.id);
    const out = ir.transitions.filter((t) => t.source === loc.id);
    return (
      <div className="stack">
        <h3>{twin.presentation.states?.[loc.id]?.label ?? loc.id}</h3>
        <KeyValue
          compact
          items={[
            ['Identifier', <span key="i" className="mono">{loc.id}</span>],
            ['Invariant', <span key="v" className="mono small">{conjunction(loc.invariant)}</span>],
            ['Proposition', <span key="p" className="mono small">{prop?.id ?? '—'}</span>],
            ['Meaning', prop ? <Formula key="f">{prop.interpretation}</Formula> : '—'],
            ['Current', isCurrent ? 'Yes (reported by the kernel)' : 'No'],
            ...(isCurrent && state
              ? [['Clocks', <span key="c" className="mono small">{Object.entries(state.configurations.find((c) => c.location === loc.id)?.clocks ?? {}).map(([k, v]) => `${k} = ${v.text}`).join(', ')}</span>] as [string, React.ReactNode]]
              : []),
          ]}
        />
        <div>
          <span className="vts-label">Immediate successors</span>
          <ul className="vts-list">
            {out.map((t) => (
              <li key={t.id} className="small">
                <span className="mono">{actionLabel(t.action)}</span> → {twin.presentation.states?.[t.target]?.label ?? t.target}
                {t.guard.length > 0 && <span className="mono xsmall subtle"> [{conjunction(t.guard)}]</span>}
              </li>
            ))}
          </ul>
        </div>
      </div>
    );
  }
  const t = ir.transitions.find((x) => x.id === selection.id);
  if (!t) return null;
  const interp = ir.event_interpretations.find((e) => e.label === actionLabel(t.action));
  const enabled = state?.enabled.find((e) => e.transition === t.id);
  const last = [...stepRecords].reverse().find((r) => r.body.outcome?.branches?.some((b) => b.transition === t.id));
  return (
    <div className="stack">
      <h3>{twin.presentation.events?.[t.action.channel ?? '']?.label ?? actionLabel(t.action)}</h3>
      <KeyValue
        compact
        items={[
          ['Event', <span key="e" className="mono">{actionLabel(t.action)}</span>],
          ['Source', twin.presentation.states?.[t.source]?.label ?? t.source],
          ['Target', twin.presentation.states?.[t.target]?.label ?? t.target],
          ['Guard', <span key="g" className="mono small">{conjunction(t.guard)}</span>],
          ['Clock resets', <span key="r" className="mono small">{t.resets.length ? t.resets.join(', ') : 'none'}</span>],
          ['Meaning', interp ? <Formula key="m">{interp.formula}</Formula> : 'Internal (τ) — no domain meaning'],
          [
            'Kernel status',
            enabled ? (
              enabled.enabled_now ? (
                <StatusBadge key="s" tone="ok" label="Admissible now" />
              ) : (
                <StatusBadge key="s" tone="info" label={`Admissible for t ∈ [${enabled.window.earliest.text}, ${enabled.window.latest?.text ?? '∞'}]`} />
              )
            ) : state ? (
              'Not enabled in the current state'
            ) : (
              'Unknown (runtime not connected)'
            ),
          ],
          ['Last executed', last?.body.input ? `t = ${ticksToText(last.body.input.at, ir.time.ticks_per_unit || 1)} ${twin.presentation.timeUnit ?? ''}` : 'Not in the recent ledger'],
        ]}
      />
    </div>
  );
}

function TransitionsTable({ ir, twin }: { ir: TwinIr; twin: TwinDetail }) {
  return (
    <div className="vts-table-wrap" style={{ maxHeight: 320 }}>
      <table className="vts-table">
        <caption className="sr-only">All transitions of the behavioural model (text alternative to the graph)</caption>
        <thead>
          <tr>
            <th scope="col">From</th>
            <th scope="col">Event</th>
            <th scope="col">Guard</th>
            <th scope="col">Resets</th>
            <th scope="col">To</th>
          </tr>
        </thead>
        <tbody>
          {ir.transitions.map((t) => (
            <tr key={t.id}>
              <td>{twin.presentation.states?.[t.source]?.label ?? t.source}</td>
              <td className="mono small">{actionLabel(t.action)}</td>
              <td className="mono small">{conjunction(t.guard)}</td>
              <td className="mono small">{t.resets.join(', ') || '—'}</td>
              <td>{twin.presentation.states?.[t.target]?.label ?? t.target}</td>
            </tr>
          ))}
        </tbody>
      </table>
    </div>
  );
}

function FactsView({ twin, onWhy }: { twin: TwinDetail; onWhy: (key: string) => void }) {
  const facts = useSemanticFacts(twin);
  return (
    <Panel
      title="Semantic facts"
      subtitle="Each interpretation formula evaluated against the latest observations (Z3, three-valued). Unknown means the data does not decide it."
      flush
    >
      <QueryState query={facts}>
        {(f) => (
          <div className="stack">
            {f.observationsConsistent === 'false' && (
              <div style={{ padding: 'var(--s-3) var(--s-4) 0' }}>
                <Callout tone="critical" title="Observations contradict the ontology">
                  The latest readings violate a domain axiom; every fact is reported as inconsistent rather than guessed.
                </Callout>
              </div>
            )}
            <table className="vts-table">
              <caption className="sr-only">Semantic facts</caption>
              <thead>
                <tr>
                  <th scope="col">Label</th>
                  <th scope="col">Meaning</th>
                  <th scope="col">Truth now</th>
                  <th scope="col" />
                </tr>
              </thead>
              <tbody>
                {f.entries.map((e) => (
                  <tr key={e.key}>
                    <td>
                      <span className="strong">{e.isEvent ? twin.presentation.events?.[e.key.slice(0, -1)]?.label ?? e.key : twin.presentation.states?.[e.key]?.label ?? e.key}</span>
                      <div className="mono xsmall subtle">{e.isEvent ? `event ${e.key}` : `state ${e.key}`}</div>
                    </td>
                    <td style={{ maxWidth: 480 }}><Formula>{e.formula}</Formula></td>
                    <td>
                      <TruthBadge truth={e.truth} />
                      {e.truth === 'unknown' && e.unobserved.length > 0 && (
                        <div className="xsmall subtle">needs {e.unobserved.filter((u) => !/limit|trip|rated|max|min|threshold/i.test(u)).slice(0, 3).join(', ') || e.unobserved.slice(0, 3).join(', ')}</div>
                      )}
                    </td>
                    <td>
                      <Button size="sm" variant="ghost" icon={<HelpCircle size={13} />} onClick={() => onWhy(e.key)}>
                        Why?
                      </Button>
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
            <p className="xsmall subtle" style={{ padding: '0 var(--s-4) var(--s-3)' }}>
              Interpretation <RefLink refId={f.interpretationRef} kind="interpretation" /> over <RefLink refId={f.ontologyRef} kind="ontology" />
              {' · '}checker {f.checker}
            </p>
          </div>
        )}
      </QueryState>
    </Panel>
  );
}

/** @param fixed When given (twin workspace), shows only that view and no view tabs. */
export default function AssetBehaviorTab({ view: fixed }: { view?: View } = {}) {
  const { twin, runtimeConnected } = useAssetContext();
  const [params, setParams] = useSearchParams();
  const view = fixed ?? ((params.get('view') as View) || 'state');
  const [selection, setSelection] = useState<Selection | null>(null);
  const [why, setWhy] = useState<string | null>(null);
  const b = useBehavior(twin, runtimeConnected);
  const setView = (v: View) => {
    const p = new URLSearchParams(params);
    p.set('view', v);
    setParams(p, { replace: true });
  };
  const stateData = b.state.data ?? null;
  const runtimeError = useMemo(() => (b.state.isError ? b.state.error : null), [b.state.isError, b.state.error]);
  if (!twin) return <EmptyState title="No digital twin">This asset has no behavioural twin.</EmptyState>;

  return (
    <div className="stack">
      {!fixed && <Tabs
        label="Behaviour views"
        value={view}
        onChange={setView}
        tabs={[
          { id: 'state', label: 'Current state' },
          { id: 'graph', label: 'Behavioural graph' },
          { id: 'facts', label: 'Semantic facts' },
          { id: 'conformance', label: 'Conformance' },
        ]}
      />}
      {view === 'state' &&
        (!runtimeConnected ? (
          <Panel title="Current state"><RuntimeUnavailable what="The current behavioural state is only known to the runtime." /></Panel>
        ) : runtimeError ? (
          <Panel title="Current state"><Callout tone="warning" title="Runtime unreachable">{(runtimeError as Error).message}</Callout></Panel>
        ) : stateData ? (
          <CurrentStateView twin={twin} state={stateData} ir={b.ir} onWhy={setWhy} />
        ) : (
          <Panel title="Current state"><p className="small muted">Loading…</p></Panel>
        ))}
      {view === 'graph' && (
        <div className="grid-main-side">
          <Panel
            title="Behavioural model"
            subtitle={
              b.irSource === 'runtime'
                ? 'Model executed by the runtime; highlighting reflects the live kernel state'
                : b.irSource === 'package'
                  ? 'Model from the deployed package (structure only: runtime not connected)'
                  : undefined
            }
          >
            {b.ir ? (
              <div className="stack">
                <BehaviorGraph ir={b.ir} state={stateData} presentation={twin.presentation} recent={b.recentTransitions} selection={selection} onSelect={setSelection} />
                <details>
                  <summary className="small">Transitions as a table</summary>
                  <TransitionsTable ir={b.ir} twin={twin} />
                </details>
              </div>
            ) : (
              <QueryState query={b.irQuery as never}>{() => null}</QueryState>
            )}
          </Panel>
          <Panel title="Details">{b.ir && <SelectionDetails ir={b.ir} selection={selection} state={stateData} twin={twin} stepRecords={b.stepRecords} />}</Panel>
        </div>
      )}
      {view === 'facts' && <FactsView twin={twin} onWhy={setWhy} />}
      {view === 'conformance' && <ConformanceView twin={twin} connected={runtimeConnected} />}
      <WhyDrawer twin={twin} entryKey={why} state={stateData} open={why !== null} onOpenChange={(o) => !o && setWhy(null)} />
    </div>
  );
}
