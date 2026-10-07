/**
 * Assurance → Monitors and alert policy: what the running twin checks continuously —
 * behavioural conformance to the verified model, temporal properties of the Digital Twin
 * View and data-quality checks on telemetry — and which monitor results raise an alert.
 *
 * Every monitor is validated by the backend against the pinned views and the data contract
 * (twin-monitors/1); what a property can be checked or monitored for is the backend's property
 * analysis, and "Check now" runs the design-time property checker on the DT view.
 */
import { useQuery } from '@tanstack/react-query';
import { Activity, BellRing, Gauge, Plus, ShieldCheck, Trash2, Wand2 } from 'lucide-react';
import { useMemo, useState, type ReactNode } from 'react';
import { Link, useSearchParams } from 'react-router-dom';
import { blueprintRoute, useBlueprintModel, useBlueprintSemantics } from '@/api/blueprints';
import { api } from '@/api/client';
import type { AlertDef, MonitorDef, MonitorKind } from '@/api/types';
import { Button, Callout, EmptyState, StatusBadge, Tabs } from '@/design';
import { nextId, useEditor, useSection } from '../editor';
import { useModelFacts } from '../formalFacts';
import { renameMonitor } from '../refactor';
import { usedByMonitor } from '../refs';
import { ConfirmDelete, DecimalField, EdPage, InspectorSection, IntField, JsonSectionView, Pane, SelectField, SelectList, TextField, UsedBy } from '../ui';
import { useWorkspace } from '../workspace';
import { PropertyAnalysisView } from './RequirementsPage';
import { IdField } from './StructurePage';

const KINDS: { value: MonitorKind; label: string; icon: ReactNode; help: string }[] = [
  { value: 'conformance', label: 'Behavioural conformance', icon: <ShieldCheck size={13} aria-hidden="true" />, help: 'Every observed event must be admitted by the verified model.' },
  { value: 'property', label: 'Temporal property', icon: <Activity size={13} aria-hidden="true" />, help: 'A property over the Digital Twin View, evaluated on the live state.' },
  { value: 'data_quality', label: 'Data quality', icon: <Gauge size={13} aria-hidden="true" />, help: 'Freshness, presence, type and range of a telemetry signal.' },
];
const SEVERITIES = [
  { value: 'info' as const, label: 'Info' },
  { value: 'warning' as const, label: 'Warning' },
  { value: 'critical' as const, label: 'Critical' },
];
const CHECKS = [
  { value: 'stale', label: 'Stale (older than a maximum age)' },
  { value: 'missing', label: 'Missing' },
  { value: 'invalid_type', label: 'Invalid type' },
  { value: 'out_of_range', label: 'Out of range' },
  { value: 'clock_regression', label: 'Clock regression (time goes backwards)' },
  { value: 'duplicate', label: 'Duplicate sample' },
  { value: 'disconnected', label: 'Source disconnected' },
];
const ON: { value: AlertDef['on']; label: string }[] = [
  { value: 'violated', label: 'violated' },
  { value: 'finding', label: 'finding (data quality)' },
  { value: 'inconclusive', label: 'inconclusive' },
];

interface Diag {
  severity: string;
  code: string;
  message: string;
  hint?: string;
  element?: { kind: string; name: string; part: string };
}

/** Backend validation of the monitors document against the pinned views, the data contract and the ontology. */
function useMonitorValidation() {
  const e = useEditor();
  const pt = useBlueprintModel(e.id, e.version, 'pt');
  const dt = useBlueprintModel(e.id, e.version, 'dt');
  const ontology = useBlueprintSemantics(e.id, e.version, 'ontology');
  const body = useMemo(
    () => ({
      monitors: { ...e.doc.assurance, format: 'twin-monitors/1' },
      ...(pt.data?.model ? { ptModel: pt.data.model } : {}),
      ...(dt.data?.model ? { dtModel: dt.data.model } : {}),
      telemetry: e.doc.data.telemetry.map((t) => ({ id: t.id, type: t.type })),
      ...(ontology.data?.artifact?.content ? { ontology: ontology.data.artifact.content } : {}),
    }),
    [e.doc.assurance, e.doc.data.telemetry, pt.data, dt.data, ontology.data],
  );
  const key = JSON.stringify(body);
  return useQuery({
    queryKey: ['blueprints', 'monitors-validate', e.id, e.version, key],
    queryFn: () => api.post<{ valid: boolean; diagnostics: Diag[] }>('/authoring/monitors/validate', body),
    enabled: !pt.isPending && !dt.isPending,
    placeholderData: (prev) => prev,
    staleTime: 30_000,
    retry: false,
  });
}

function monitorIndexOf(d: Diag): number | null {
  const m = /monitors\[(\d+)\]/.exec(d.element?.name ?? '');
  return m ? Number(m[1]) : null;
}

function DiagList({ diags }: { diags: Diag[] }) {
  if (diags.length === 0) return null;
  return (
    <ul className="vts-findings">
      {diags.map((d, i) => (
        <li key={i}>
          <span style={{ color: d.severity === 'error' ? 'var(--crit)' : 'var(--warn)' }}>{d.severity === 'error' ? '●' : '▲'}</span>
          <span>
            {d.message} <span className="mono subtle">{d.code}</span>
            {d.hint ? <span className="xsmall subtle"> — {d.hint}</span> : null}
          </span>
        </li>
      ))}
    </ul>
  );
}

interface PropertyCheck {
  verdict: string;
  property: string;
  states?: number;
  complete?: boolean;
  reasons?: string[];
  witness?: unknown;
  locations?: { location: string; guaranteed: boolean; reason?: string }[];
  method?: string;
}

const VERDICT: Record<string, { tone: 'ok' | 'critical' | 'warning' | 'neutral' | 'formal'; label: string }> = {
  holds: { tone: 'ok', label: 'HOLDS on every reachable state' },
  guaranteed: { tone: 'ok', label: 'GUARANTEED by the ontology' },
  violated: { tone: 'critical', label: 'VIOLATED — a reachable state breaks it' },
  does_not_hold: { tone: 'critical', label: 'DOES NOT HOLD' },
  not_guaranteed: { tone: 'warning', label: 'NOT GUARANTEED' },
  inconclusive: { tone: 'warning', label: 'INCONCLUSIVE (state space bound reached)' },
  unsupported: { tone: 'neutral', label: 'Not checked at design time' },
};

function DesignTimeCheck({ property }: { property: string }) {
  const e = useEditor();
  const dt = useBlueprintModel(e.id, e.version, 'dt');
  const ontology = useBlueprintSemantics(e.id, e.version, 'ontology');
  const interp = useBlueprintSemantics(e.id, e.version, 'dt_interpretation');
  const [result, setResult] = useState<{ property: string; r: PropertyCheck } | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);
  const run = async () => {
    if (!dt.data?.model) return;
    setBusy(true);
    setError(null);
    try {
      const r = await api.post<PropertyCheck>('/authoring/properties/check', {
        property,
        dtModel: dt.data.model,
        ...(ontology.data?.artifact?.content ? { ontology: ontology.data.artifact.content } : {}),
        ...(interp.data?.artifact?.content ? { dtInterpretation: interp.data.artifact.content } : {}),
      });
      setResult({ property, r });
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
      setResult(null);
    } finally {
      setBusy(false);
    }
  };
  const r = result && result.property === property ? result.r : null;
  const v = r ? (VERDICT[r.verdict] ?? { tone: 'neutral' as const, label: r.verdict }) : null;
  const witness = r && Array.isArray(r.witness) ? (r.witness as Record<string, unknown>[]) : null;
  return (
    <div className="stack-sm">
      <div className="row-wrap">
        <Button size="sm" icon={<ShieldCheck size={13} />} loading={busy} disabled={!dt.data?.model || !property} onClick={() => void run()}>
          Check now (design time)
        </Button>
        {v && <StatusBadge tone={v.tone} label={v.label} />}
      </div>
      {error && <Callout tone="critical" title="Check failed">{error}</Callout>}
      {r && (
        <div className="stack-sm xsmall">
          {r.states !== undefined && (
            <span className="subtle">
              {r.states} symbolic state(s) explored{r.complete === false ? ' (incomplete)' : ''}. A design-time check covers the model, not the running plant: the monitor still watches it live.
            </span>
          )}
          {r.locations && (
            <ul className="vts-findings">
              {r.locations.map((l) => (
                <li key={l.location}>
                  <span style={{ color: l.guaranteed ? 'var(--ok)' : 'var(--warn)' }}>{l.guaranteed ? '✓' : '✗'}</span>
                  <span className="mono">{l.location}</span> {l.reason && <span className="subtle">{l.reason}</span>}
                </li>
              ))}
            </ul>
          )}
          {witness && witness.length > 0 && (
            <div>
              <strong>Witness run:</strong>{' '}
              <span className="mono">
                {witness.map((w, i) => (
                  <span key={i}>
                    {i > 0 && ' → '}
                    {String(w.location ?? w.label ?? JSON.stringify(w))}
                  </span>
                ))}
              </span>
            </div>
          )}
          {(r.reasons ?? []).length > 0 && (
            <ul style={{ margin: 0, paddingLeft: 16 }}>
              {r.reasons!.map((x) => (
                <li key={x}>{x}</li>
              ))}
            </ul>
          )}
        </div>
      )}
    </div>
  );
}

/** Guided construction of the common property shapes from the DT view's states and clocks. */
function PropertyBuilder({ onUse }: { onUse: (p: string) => void }) {
  const dt = useModelFacts('dt');
  const [pattern, setPattern] = useState<'never' | 'bounded' | 'only'>('never');
  const [state, setState] = useState('');
  const [clock, setClock] = useState('');
  const [bound, setBound] = useState('5');
  const s = state || dt.locations[0] || '';
  const c = clock || dt.clocks[0] || '';
  const text = pattern === 'never' ? `A[] !${s}` : pattern === 'bounded' ? `A[] (${s} -> ${c} <= ${bound})` : `A[] (${s} -> ${c} >= ${bound})`;
  if (!dt.present) return <p className="xsmall subtle">Define the Digital Twin View to build properties from its states.</p>;
  return (
    <div className="stack-sm" style={{ padding: 8, border: '1px dashed var(--border)', borderRadius: 8 }}>
      <div className="row-wrap">
        <select className="vts-select" value={pattern} onChange={(x) => setPattern(x.target.value as typeof pattern)} aria-label="Property pattern">
          <option value="never">Never reach a state</option>
          <option value="bounded">Stay in a state at most…</option>
          <option value="only">Leave a state only after…</option>
        </select>
        <select className="vts-select" value={s} onChange={(x) => setState(x.target.value)} aria-label="State">
          {dt.locations.map((l) => (
            <option key={l}>{l}</option>
          ))}
        </select>
        {pattern !== 'never' && (
          <>
            <select className="vts-select" value={c} onChange={(x) => setClock(x.target.value)} aria-label="Clock">
              {dt.clocks.map((l) => (
                <option key={l}>{l}</option>
              ))}
            </select>
            <input className="vts-input mono" style={{ width: 80 }} value={bound} onChange={(x) => setBound(x.target.value.replace(/[^0-9.]/g, ''))} aria-label="Bound (time units)" />
          </>
        )}
      </div>
      <div className="row">
        <code className="small grow">{text}</code>
        <Button size="sm" icon={<Wand2 size={13} />} onClick={() => onUse(text)}>
          Use
        </Button>
      </div>
    </div>
  );
}

/** Parameters of another kind are not allowed (TWN004): switching kind drops them. */
function cleanMonitor(m: MonitorDef): MonitorDef {
  const base: MonitorDef = { id: m.id, kind: m.kind, name: m.name, severity: m.severity, ...(m.requirement ? { requirement: m.requirement } : {}) };
  if (m.kind === 'conformance') return { ...base, events: m.events ?? 'all', unmatchedEvents: m.unmatchedEvents ?? 'record' };
  if (m.kind === 'property') return { ...base, property: m.property ?? '' };
  const out: MonitorDef = { ...base, field: m.field ?? '', check: m.check ?? 'stale' };
  if (out.check === 'stale') out.maxAgeSeconds = m.maxAgeSeconds ?? 30;
  if (out.check === 'disconnected' && m.maxSilenceSeconds !== undefined) out.maxSilenceSeconds = m.maxSilenceSeconds;
  if (out.check === 'out_of_range') {
    if (m.min !== undefined) out.min = m.min;
    if (m.max !== undefined) out.max = m.max;
  }
  return out;
}

function MonitorInspector({ monitor, index, diags }: { monitor: MonitorDef; index: number; diags: Diag[] }) {
  const e = useEditor();
  const [assurance, setAssurance] = useSection('assurance');
  const [, setParams] = useSearchParams();
  const pt = useModelFacts('pt');
  const [confirm, setConfirm] = useState(false);
  const [builder, setBuilder] = useState(false);
  const set = (patch: Partial<MonitorDef>, label = `Edit monitor ${monitor.id}`) =>
    setAssurance((a) => ({ ...a, monitors: a.monitors.map((m, i) => (i === index ? cleanMonitor({ ...m, ...patch }) : m)) }), { label, key: `mon.${monitor.id}.${Object.keys(patch).join()}` });
  const events = monitor.events ?? 'all';
  const eventList = Array.isArray(events) ? events : [];
  const deps = usedByMonitor(e.doc, monitor.id);
  const alerts = assurance.alerts.filter((a) => a.monitor === monitor.id);
  const kind = KINDS.find((k) => k.value === monitor.kind);
  return (
    <Pane className="vts-inspector" title={<span className="row">{kind?.icon} Monitor</span>}>
      <InspectorSection title="Identity">
        <IdField
          label="Id"
          value={monitor.id}
          pattern={/^[A-Za-z][A-Za-z0-9_.-]*$/}
          taken={new Set(assurance.monitors.map((m) => m.id))}
          hint="Renaming updates requirements, alerts, presentation and scenarios."
          onRename={(to) => {
            e.updateDoc((d) => renameMonitor(d, monitor.id, to), { label: `Rename ${monitor.id} → ${to}` });
            setParams({ id: to }, { replace: true });
          }}
        />
        <TextField label="Name" value={monitor.name} onChange={(v) => set({ name: v })} />
        <div className="vts-fgrid">
          <SelectField label="Kind" value={monitor.kind} options={KINDS.map((k) => ({ value: k.value, label: k.label }))} onChange={(v) => set({ kind: v }, `Change kind of ${monitor.id}`)} hint={kind?.help} />
          <SelectField label="Severity" value={monitor.severity} options={SEVERITIES} onChange={(v) => set({ severity: v })} />
        </div>
        <SelectField
          label="Requirement"
          value={monitor.requirement ?? ''}
          allowEmpty="— none —"
          options={assurance.requirements.map((r) => ({ value: r.id, label: `${r.id} ${r.title}` }))}
          onChange={(v) => {
            setAssurance(
              (a) => ({
                ...a,
                monitors: a.monitors.map((m, i) => (i === index ? { ...m, requirement: v || undefined } : m)),
                requirements: a.requirements.map((r) => (r.id === v && !r.monitors.includes(monitor.id) ? { ...r, monitors: [...r.monitors, monitor.id] } : r)),
              }),
              { label: `Trace ${monitor.id} to ${v || 'nothing'}` },
            );
          }}
        />
      </InspectorSection>
      {monitor.kind === 'conformance' && (
        <InspectorSection title="Conformance">
          <fieldset className="vts-f" style={{ border: 0, padding: 0, margin: 0 }}>
            <legend className="vts-label">Events checked</legend>
            <div className="row-wrap">
              <button type="button" className="vts-chip" aria-pressed={events === 'all'} disabled={!e.editable} onClick={() => set({ events: 'all' })}>
                All events
              </button>
              {pt.labels.map((l) => {
                const on = eventList.includes(l);
                return (
                  <button
                    key={l}
                    type="button"
                    className="vts-chip mono"
                    aria-pressed={on}
                    disabled={!e.editable}
                    onClick={() => {
                      const next = on ? eventList.filter((x) => x !== l) : [...eventList, l];
                      set({ events: next.length === 0 ? 'all' : next });
                    }}
                  >
                    {l}
                  </button>
                );
              })}
            </div>
            <span className="vts-f__hint">Events of the Physical System View; &ldquo;All&rdquo; checks every event the plant sends.</span>
          </fieldset>
          <SelectField
            label="Events the model does not admit"
            value={monitor.unmatchedEvents ?? 'record'}
            options={[
              { value: 'record', label: 'Record the violation and keep following the plant' },
              { value: 'reject', label: 'Reject the event (the twin state does not change)' },
            ]}
            onChange={(v) => set({ unmatchedEvents: v })}
          />
        </InspectorSection>
      )}
      {monitor.kind === 'property' && (
        <InspectorSection title="Property">
          <TextField
            label="Property over the Digital Twin View"
            mono
            value={monitor.property ?? ''}
            placeholder="A[] !FAULT"
            onChange={(v) => set({ property: v })}
            hint={
              <>
                <code>A[] !STATE</code> never reach a state · <code>A[] (STATE -&gt; clock &lt;= N)</code> bounded time ·{' '}
                {e.editable && (
                  <button type="button" className="vts-linkbtn" onClick={() => setBuilder((b) => !b)}>
                    {builder ? 'hide builder' : 'build one'}
                  </button>
                )}
              </>
            }
          />
          {builder && e.editable && (
            <PropertyBuilder
              onUse={(p) => {
                set({ property: p }, `Property of ${monitor.id}`);
                setBuilder(false);
              }}
            />
          )}
          {monitor.property && <PropertyAnalysisView property={monitor.property} />}
          {monitor.property && <DesignTimeCheck property={monitor.property} />}
        </InspectorSection>
      )}
      {monitor.kind === 'data_quality' && (
        <InspectorSection title="Data quality">
          <SelectField
            label="Signal"
            value={monitor.field ?? ''}
            allowEmpty="Choose a telemetry signal…"
            options={e.doc.data.telemetry.map((t) => ({ value: t.id, label: `${t.label || t.id} (${t.id})` }))}
            onChange={(v) => set({ field: v })}
          />
          <SelectField label="Check" value={monitor.check ?? 'stale'} options={CHECKS} onChange={(v) => set({ check: v })} />
          {monitor.check === 'stale' && <IntField label="Maximum age" unit="s" min={1} value={monitor.maxAgeSeconds} onChange={(v) => set({ maxAgeSeconds: v })} />}
          {monitor.check === 'disconnected' && (
            <IntField
              label="Maximum silence"
              unit="s"
              min={1}
              value={monitor.maxSilenceSeconds}
              onChange={(v) => set({ maxSilenceSeconds: v })}
            />
          )}
          {monitor.check === 'out_of_range' && (
            <div className="vts-fgrid">
              <DecimalField label="Minimum" value={monitor.min === undefined ? undefined : String(monitor.min)} onChange={(v) => set({ min: v })} />
              <DecimalField label="Maximum" value={monitor.max === undefined ? undefined : String(monitor.max)} onChange={(v) => set({ max: v })} />
            </div>
          )}
        </InspectorSection>
      )}
      <InspectorSection title="Alerts">
        {alerts.length === 0 ? (
          <p className="xsmall subtle">No alert: results are recorded and shown in Operate, but nobody is notified.</p>
        ) : (
          <ul className="vts-findings">
            {alerts.map((a) => (
              <li key={a.id}>
                <BellRing size={13} aria-hidden="true" />
                <span>
                  on <strong>{a.on}</strong> → {a.severity}: {a.message}
                </span>
              </li>
            ))}
          </ul>
        )}
        {e.editable && (
          <Button
            size="sm"
            variant="ghost"
            icon={<Plus size={13} />}
            onClick={() =>
              setAssurance(
                (a) => ({ ...a, alerts: [...a.alerts, { id: nextId('AL', a.alerts.map((x) => x.id), '-'), monitor: monitor.id, on: monitor.kind === 'data_quality' ? 'finding' : 'violated', severity: monitor.severity, message: monitor.name }] }),
                { label: `Alert for ${monitor.id}` },
              )
            }
          >
            Add an alert
          </Button>
        )}
      </InspectorSection>
      {diags.length > 0 && (
        <InspectorSection title="Problems">
          <DiagList diags={diags} />
        </InspectorSection>
      )}
      <InspectorSection title="Used by">
        <UsedBy deps={deps} />
      </InspectorSection>
      {e.editable && (
        <div className="row">
          <Button size="sm" variant="danger" icon={<Trash2 size={13} />} onClick={() => setConfirm(true)}>
            Delete monitor
          </Button>
        </div>
      )}
      <ConfirmDelete
        open={confirm}
        onOpenChange={setConfirm}
        title={`Delete monitor ${monitor.id}?`}
        dependencies={deps}
        consequence="Its alerts are deleted with it; requirements stop referring to it."
        onConfirm={() => {
          setAssurance(
            (a) => ({
              ...a,
              monitors: a.monitors.filter((m) => m.id !== monitor.id),
              alerts: a.alerts.filter((x) => x.monitor !== monitor.id),
              requirements: a.requirements.map((r) => ({ ...r, monitors: r.monitors.filter((m) => m !== monitor.id) })),
            }),
            { label: `Delete ${monitor.id}` },
          );
          setParams({}, { replace: true });
        }}
      />
    </Pane>
  );
}

function AlertPolicy() {
  const e = useEditor();
  const [assurance, setAssurance] = useSection('assurance');
  const setAlert = (i: number, patch: Partial<AlertDef>) =>
    setAssurance((a) => ({ ...a, alerts: a.alerts.map((x, j) => (j === i ? { ...x, ...patch } : x)) }), { label: `Edit alert ${assurance.alerts[i]?.id}`, key: `alert.${i}.${Object.keys(patch).join()}` });
  const silent = assurance.monitors.filter((m) => !assurance.alerts.some((a) => a.monitor === m.id));
  return (
    <Pane
      title={<span className="row"><BellRing size={14} aria-hidden="true" /> Alert policy</span>}
      actions={
        e.editable && (
          <Button
            size="sm"
            icon={<Plus size={13} />}
            disabled={assurance.monitors.length === 0}
            onClick={() => setAssurance((a) => ({ ...a, alerts: [...a.alerts, { id: nextId('AL', a.alerts.map((x) => x.id), '-'), monitor: a.monitors[0]!.id, on: 'violated', severity: 'warning', message: '' }] }), { label: 'Add alert' })}
          >
            Alert
          </Button>
        )
      }
    >
      <div className="stack">
        <p className="small muted">An alert notifies operators in Operate when a monitor reports the chosen result. Monitors without alerts are still evaluated and shown.</p>
        {assurance.alerts.length === 0 ? (
          <EmptyState compact title="No alerts">Add one for each result an operator must act on (typically every critical monitor).</EmptyState>
        ) : (
          <table className="vts-table">
            <thead>
              <tr>
                <th>Id</th>
                <th>Monitor</th>
                <th>When</th>
                <th>Severity</th>
                <th>Message shown to the operator</th>
                <th aria-label="Actions" />
              </tr>
            </thead>
            <tbody>
              {assurance.alerts.map((a, i) => (
                <tr key={`${a.id}-${i}`}>
                  <td className="mono small">{a.id}</td>
                  <td>
                    <select className="vts-select" value={a.monitor} disabled={!e.editable} aria-label={`Monitor of ${a.id}`} onChange={(x) => setAlert(i, { monitor: x.target.value })}>
                      {!assurance.monitors.some((m) => m.id === a.monitor) && <option value={a.monitor}>{a.monitor} (unknown)</option>}
                      {assurance.monitors.map((m) => (
                        <option key={m.id} value={m.id}>
                          {m.name || m.id}
                        </option>
                      ))}
                    </select>
                  </td>
                  <td>
                    <select className="vts-select" value={a.on} disabled={!e.editable} aria-label={`Trigger of ${a.id}`} onChange={(x) => setAlert(i, { on: x.target.value as AlertDef['on'] })}>
                      {ON.map((o) => (
                        <option key={o.value} value={o.value}>
                          {o.label}
                        </option>
                      ))}
                    </select>
                  </td>
                  <td>
                    <select className="vts-select" value={a.severity} disabled={!e.editable} aria-label={`Severity of ${a.id}`} onChange={(x) => setAlert(i, { severity: x.target.value as AlertDef['severity'] })}>
                      {SEVERITIES.map((o) => (
                        <option key={o.value} value={o.value}>
                          {o.label}
                        </option>
                      ))}
                    </select>
                  </td>
                  <td>
                    <input className="vts-input" value={a.message} disabled={!e.editable} aria-label={`Message of ${a.id}`} onChange={(x) => setAlert(i, { message: x.target.value })} />
                  </td>
                  <td>
                    {e.editable && (
                      <Button size="sm" variant="ghost" iconOnly icon={<Trash2 size={13} />} onClick={() => setAssurance((s) => ({ ...s, alerts: s.alerts.filter((_, j) => j !== i) }), { label: `Delete alert ${a.id}` })}>
                        Delete alert {a.id}
                      </Button>
                    )}
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        )}
        {silent.length > 0 && (
          <p className="xsmall subtle">
            No alert for: {silent.map((m) => m.name || m.id).join(', ')}.
          </p>
        )}
      </div>
    </Pane>
  );
}

export default function MonitorsPage() {
  const e = useEditor();
  const ws = useWorkspace();
  const [assurance, setAssurance] = useSection('assurance');
  const [params, setParams] = useSearchParams();
  const tab = (params.get('tab') as 'monitors' | 'alerts' | 'json' | null) ?? 'monitors';
  const selected = params.get('id') ?? assurance.monitors[0]?.id ?? null;
  const index = assurance.monitors.findIndex((m) => m.id === selected);
  const monitor = index >= 0 ? assurance.monitors[index]! : null;
  const validation = useMonitorValidation();
  const [deleting, setDeleting] = useState<string | null>(null);
  const diags = useMemo(() => validation.data?.diagnostics ?? [], [validation.data]);
  const byIndex = useMemo(() => {
    const m = new Map<number, Diag[]>();
    for (const d of diags) {
      const i = monitorIndexOf(d);
      if (i !== null) m.set(i, [...(m.get(i) ?? []), d]);
    }
    return m;
  }, [diags]);
  const other = diags.filter((d) => monitorIndexOf(d) === null);
  const add = (kind: MonitorKind) => {
    const id = nextId(kind === 'data_quality' ? 'data-check' : kind === 'property' ? 'property' : 'conformance', assurance.monitors.map((m) => m.id), '-');
    const base = { id, kind, name: KINDS.find((k) => k.value === kind)!.label, severity: 'warning' as const };
    const m: MonitorDef =
      kind === 'conformance'
        ? { ...base, severity: 'critical', events: 'all', unmatchedEvents: 'record' }
        : kind === 'property'
          ? { ...base, property: '' }
          : { ...base, field: e.doc.data.telemetry[0]?.id ?? '', check: 'stale', maxAgeSeconds: 30 };
    setAssurance((a) => ({ ...a, monitors: [...a.monitors, m] }), { label: `Add ${kind} monitor` });
    setParams({ id }, { replace: true });
  };
  const items = assurance.monitors.map((m, i) => ({
    id: m.id,
    icon: KINDS.find((k) => k.value === m.kind)?.icon,
    label: m.name || m.id,
    search: `${m.id} ${m.name} ${m.kind} ${m.property ?? ''} ${m.field ?? ''}`,
    meta: m.severity,
    tone: byIndex.get(i)?.some((d) => d.severity === 'error') ? ('error' as const) : byIndex.get(i)?.length ? ('warning' as const) : undefined,
  }));
  const del = assurance.monitors.find((m) => m.id === deleting);
  const setTab = (t: string) => setParams(t === 'monitors' ? (selected ? { id: selected } : {}) : { tab: t }, { replace: true });
  return (
    <EdPage
      title="Monitors"
      description="What the running twin checks continuously, and which results alert an operator."
      actions={
        <>
          <Tabs
            value={tab}
            onChange={setTab}
            label="Monitor views"
            tabs={[
              { id: 'monitors', label: `Monitors (${assurance.monitors.length})` },
              { id: 'alerts', label: `Alert policy (${assurance.alerts.length})` },
              ...(ws.expert ? [{ id: 'json' as const, label: 'JSON' }] : []),
            ]}
          />
        </>
      }
      guide={
        <>
          A <strong>conformance</strong> monitor checks every observed event against the verified model; a <strong>property</strong> monitor evaluates a temporal property of the Digital Twin View on the live state;{' '}
          <strong>data-quality</strong> monitors watch telemetry freshness and plausibility. Link each to the requirement it checks (<Link to={blueprintRoute(e.id, e.version, 'assurance/requirements')}>Requirements</Link>).
        </>
      }
    >
      <div className="row-wrap">
        {validation.isPending ? (
          <StatusBadge tone="info" label="Validating…" spin />
        ) : validation.isError ? (
          <StatusBadge tone="neutral" label="Validation unavailable" title={validation.error instanceof Error ? validation.error.message : undefined} />
        ) : validation.data?.valid ? (
          <StatusBadge tone="ok" icon={ShieldCheck} label="VALID against the pinned views and data contract" />
        ) : (
          <StatusBadge tone="critical" label={`${diags.filter((d) => d.severity === 'error').length} error(s)`} />
        )}
        {validation.isFetching && !validation.isPending && <span className="xsmall subtle">re-checking…</span>}
      </div>
      {other.length > 0 && tab !== 'json' && (
        <Callout tone="warning" title="Document problems">
          <DiagList diags={other} />
        </Callout>
      )}
      {tab === 'alerts' ? (
        <AlertPolicy />
      ) : tab === 'json' ? (
        <JsonSectionView value={assurance} label="Assurance" onApply={(v) => setAssurance(v, { label: 'Edit assurance (JSON)' })} />
      ) : (
        <div className="vts-ed-split vts-ed-split--two">
          <Pane
            title="Monitors"
            flush
            actions={
              e.editable && (
                <select
                  className="vts-select"
                  style={{ minHeight: 28 }}
                  value=""
                  aria-label="Add a monitor"
                  onChange={(x) => {
                    if (x.target.value) add(x.target.value as MonitorKind);
                  }}
                >
                  <option value="">+ Add monitor…</option>
                  {KINDS.map((k) => (
                    <option key={k.value} value={k.value}>
                      {k.label}
                    </option>
                  ))}
                </select>
              )
            }
          >
            <SelectList
              items={items}
              selected={selected}
              onSelect={(id) => setParams({ id }, { replace: true })}
              onDelete={e.editable ? setDeleting : undefined}
              label="Monitors"
              empty={
                <EmptyState compact title="No monitors yet" action={e.editable ? <Button size="sm" onClick={() => add('conformance')}>Add conformance monitoring</Button> : undefined}>
                  Start with behavioural conformance: it checks every event against the verified model.
                </EmptyState>
              }
            />
          </Pane>
          {monitor ? (
            <MonitorInspector key={monitor.id} monitor={monitor} index={index} diags={byIndex.get(index) ?? []} />
          ) : (
            <Pane className="vts-inspector" title="Monitor">
              <p className="small muted">Select a monitor to edit it.</p>
            </Pane>
          )}
        </div>
      )}
      {del && (
        <ConfirmDelete
          open
          onOpenChange={(o) => !o && setDeleting(null)}
          title={`Delete monitor ${del.id}?`}
          dependencies={usedByMonitor(e.doc, del.id)}
          consequence="Its alerts are deleted with it; requirements stop referring to it."
          onConfirm={() => {
            setAssurance(
              (a) => ({
                ...a,
                monitors: a.monitors.filter((m) => m.id !== del.id),
                alerts: a.alerts.filter((x) => x.monitor !== del.id),
                requirements: a.requirements.map((r) => ({ ...r, monitors: r.monitors.filter((m) => m !== del.id) })),
              }),
              { label: `Delete ${del.id}` },
            );
            setDeleting(null);
            setParams({}, { replace: true });
          }}
        />
      )}
    </EdPage>
  );
}
