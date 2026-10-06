/**
 * Assurance → Requirements: what the twin must guarantee or watch, in the user's words, each
 * traced to the monitors that check it at run time and, optionally, to a formal statement over
 * the Digital Twin View. The coverage matrix shows unmonitored requirements; whether a formal
 * statement can be checked or monitored is decided by the backend's property analysis.
 */
import { useQuery } from '@tanstack/react-query';
import { ClipboardCheck, Plus, ShieldAlert, Trash2 } from 'lucide-react';
import { useMemo, useState } from 'react';
import { Link, useSearchParams } from 'react-router-dom';
import { blueprintRoute, useBlueprintModel, useBlueprintSemantics, useBlueprintValidation } from '@/api/blueprints';
import { api } from '@/api/client';
import type { MonitorDef, RequirementCategory, RequirementDef } from '@/api/types';
import { Button, Callout, EmptyState, StatusBadge, Tabs } from '@/design';
import { nextId, useEditor, useSection } from '../editor';
import { renameRequirement } from '../refactor';
import { ConfirmDelete, EdPage, FindingsInline, InspectorSection, JsonSectionView, Pane, SelectField, SelectList, TextArea, TextField, type Dependency } from '../ui';
import { useWorkspace } from '../workspace';
import { IdField } from './StructurePage';

const CATEGORIES: { value: RequirementCategory; label: string }[] = [
  { value: 'safety', label: 'Safety' },
  { value: 'mission', label: 'Mission' },
  { value: 'performance', label: 'Performance' },
  { value: 'timing', label: 'Timing' },
  { value: 'operational', label: 'Operational' },
];
const SEVERITIES = [
  { value: 'info' as const, label: 'Info' },
  { value: 'warning' as const, label: 'Warning' },
  { value: 'critical' as const, label: 'Critical' },
];
const REQ_ID = /^[A-Za-z][A-Za-z0-9_.-]*$/;

export interface PropertyAnalysis {
  valid: boolean;
  diagnostics: { severity: string; code: string; message: string; hint?: string }[];
  text: string;
  quantifier: string;
  locations: string[];
  clocks: string[];
  semantic: string[];
  designTime: string;
  runtime: string;
  evaluator: string;
  reasons: string[];
}

/** The backend's analysis of a property over the DT view: parse, design-time and run-time support. */
export function usePropertyAnalysis(property: string | undefined) {
  const e = useEditor();
  const dt = useBlueprintModel(e.id, e.version, 'dt');
  const ontology = useBlueprintSemantics(e.id, e.version, 'ontology');
  const model = dt.data?.model;
  const ont = ontology.data?.artifact?.content;
  return useQuery({
    queryKey: ['blueprints', 'property', e.id, e.version, property ?? '', dt.data?.artifact?.contentSha256 ?? '', ontology.data?.artifact?.contentSha256 ?? ''],
    queryFn: () => api.post<PropertyAnalysis>('/authoring/properties/analyse', { property, dtModel: model, ...(ont ? { ontology: ont } : {}) }),
    enabled: !!property && !!model,
    staleTime: 60_000,
    retry: false,
  });
}

const DESIGN: Record<string, { tone: 'ok' | 'formal' | 'neutral' | 'warning'; label: string }> = {
  checkable: { tone: 'formal', label: 'Checkable at design time' },
  guarantee_check: { tone: 'formal', label: 'Guarantee check (ontology)' },
  unsupported: { tone: 'neutral', label: 'Not checked at design time' },
};
const RUNTIME: Record<string, { tone: 'ok' | 'neutral' | 'warning'; label: string }> = {
  monitorable: { tone: 'ok', label: 'Monitorable at run time' },
  not_monitorable: { tone: 'neutral', label: 'Not a run-time property' },
  unsupported: { tone: 'neutral', label: 'Not monitored' },
};

export function PropertyAnalysisView({ property }: { property: string }) {
  const q = usePropertyAnalysis(property);
  if (!property) return null;
  if (q.isPending) return <span className="xsmall subtle">Analysing…</span>;
  if (q.isError) return <Callout tone="critical" title="Not analysable">{q.error instanceof Error ? q.error.message : 'The property could not be analysed.'}</Callout>;
  const a = q.data;
  if (!a.valid) {
    return (
      <ul className="vts-findings">
        {a.diagnostics.map((d, i) => (
          <li key={i}>
            <ShieldAlert size={13} aria-hidden="true" style={{ color: 'var(--crit)' }} />
            <span>
              {d.message} <span className="mono subtle">{d.code}</span>
              {d.hint ? <span className="xsmall subtle"> — {d.hint}</span> : null}
            </span>
          </li>
        ))}
      </ul>
    );
  }
  const d = DESIGN[a.designTime] ?? { tone: 'neutral' as const, label: a.designTime };
  const r = RUNTIME[a.runtime] ?? { tone: 'neutral' as const, label: a.runtime };
  return (
    <div className="stack-sm">
      <div className="row-wrap">
        <StatusBadge tone={d.tone} label={d.label} />
        <StatusBadge tone={r.tone} label={r.label} />
        {a.evaluator !== 'none' && <span className="xsmall subtle">evaluated by {a.evaluator === 'runtime' ? 'the runtime monitor' : 'Studio'}</span>}
      </div>
      <span className="xsmall subtle mono">
        {a.text}
        {a.locations.length > 0 && ` · states ${a.locations.join(', ')}`}
        {a.clocks.length > 0 && ` · clocks ${a.clocks.join(', ')}`}
        {a.semantic.length > 0 && ` · semantic ${a.semantic.join(', ')}`}
      </span>
      {a.reasons.length > 0 && (
        <ul className="xsmall muted" style={{ margin: 0, paddingLeft: 16 }}>
          {a.reasons.map((x) => (
            <li key={x}>{x}</li>
          ))}
        </ul>
      )}
    </div>
  );
}

function RequirementInspector({ req }: { req: RequirementDef }) {
  const e = useEditor();
  const [assurance, setAssurance] = useSection('assurance');
  const [, setParams] = useSearchParams();
  const [confirm, setConfirm] = useState(false);
  const validation = useBlueprintValidation(e.id, e.version);
  const findings = (validation.data?.findings ?? []).filter((f) => f.section === 'assurance' && (f.target === req.id || f.path.includes(`requirements[${assurance.requirements.indexOf(req)}]`)));
  const set = (patch: Partial<RequirementDef>, label = `Edit requirement ${req.id}`) =>
    setAssurance((a) => ({ ...a, requirements: a.requirements.map((r) => (r.id === req.id ? { ...r, ...patch } : r)) }), { label, key: `req.${req.id}.${Object.keys(patch).join()}` });
  const monitors = assurance.monitors;
  const deps: Dependency[] = monitors.filter((m) => m.requirement === req.id).map((m) => ({ what: `Monitor ${m.name || m.id}`, where: 'Monitors', route: `assurance/monitors?id=${encodeURIComponent(m.id)}` }));
  const createMonitor = () => {
    const id = nextId(req.id.toLowerCase().replace(/[^a-z0-9]+/g, '-'), monitors.map((m) => m.id), '-');
    const m: MonitorDef = { id, kind: 'property', name: req.title || id, severity: req.severity, requirement: req.id, property: req.formal ?? '' };
    setAssurance((a) => ({ ...a, monitors: [...a.monitors, m], requirements: a.requirements.map((r) => (r.id === req.id ? { ...r, monitors: [...r.monitors, id] } : r)) }), { label: `Monitor for ${req.id}` });
  };
  return (
    <Pane className="vts-inspector" title={<span className="row"><ClipboardCheck size={14} aria-hidden="true" /> Requirement</span>}>
      <InspectorSection title="Identity">
        <IdField
          label="Id"
          value={req.id}
          pattern={REQ_ID}
          hint="e.g. REQ-S1. Renaming updates the monitors that refer to it."
          taken={new Set(assurance.requirements.map((r) => r.id))}
          onRename={(to) => {
            e.updateDoc((d) => renameRequirement(d, req.id, to), { label: `Rename ${req.id} → ${to}` });
            setParams({ id: to }, { replace: true });
          }}
        />
        <TextField label="Title" value={req.title} onChange={(v) => set({ title: v })} />
        <div className="vts-fgrid">
          <SelectField label="Category" value={req.category} options={CATEGORIES} onChange={(v) => set({ category: v })} />
          <SelectField label="Severity" value={req.severity} options={SEVERITIES} onChange={(v) => set({ severity: v })} />
        </div>
        <TextArea label="Description" value={req.description ?? ''} onChange={(v) => set({ description: v })} rows={3} />
      </InspectorSection>
      <InspectorSection title="Formal statement (optional)">
        <TextField
          label="Property over the Digital Twin View"
          mono
          value={req.formal ?? ''}
          placeholder="A[] !FAULT"
          onChange={(v) => set({ formal: v || undefined })}
          hint={<>Safety: <code>A[] !STATE</code>; timing: <code>A[] (STATE -&gt; clock &lt;= 5)</code>; semantic: <code>A[] sem(...)</code>.</>}
        />
        {req.formal && <PropertyAnalysisView property={req.formal} />}
      </InspectorSection>
      <InspectorSection title="Checked by">
        <div className="stack-sm">
          {monitors.length === 0 && <p className="xsmall subtle">No monitors defined yet.</p>}
          <div className="row-wrap">
            {monitors.map((m) => {
              const on = req.monitors.includes(m.id);
              return (
                <button key={m.id} type="button" className="vts-chip" aria-pressed={on} disabled={!e.editable} onClick={() => set({ monitors: on ? req.monitors.filter((x) => x !== m.id) : [...req.monitors, m.id] }, `${on ? 'Unlink' : 'Link'} ${m.id}`)}>
                  {m.name || m.id}
                </button>
              );
            })}
          </div>
          {req.monitors.filter((m) => !monitors.some((x) => x.id === m)).map((m) => (
            <span key={m} className="xsmall" style={{ color: 'var(--crit)' }}>
              Unknown monitor {m}{' '}
              {e.editable && (
                <button type="button" className="vts-linkbtn" onClick={() => set({ monitors: req.monitors.filter((x) => x !== m) })}>
                  remove
                </button>
              )}
            </span>
          ))}
          {e.editable && req.formal && (
            <Button size="sm" variant="ghost" icon={<Plus size={13} />} onClick={createMonitor}>
              Create a property monitor from the formal statement
            </Button>
          )}
        </div>
      </InspectorSection>
      {findings.length > 0 && (
        <InspectorSection title="Problems">
          <FindingsInline findings={findings} />
        </InspectorSection>
      )}
      {e.editable && (
        <div className="row">
          <Button size="sm" variant="danger" icon={<Trash2 size={13} />} onClick={() => setConfirm(true)}>
            Delete requirement
          </Button>
        </div>
      )}
      <ConfirmDelete
        open={confirm}
        onOpenChange={setConfirm}
        title={`Delete requirement ${req.id}?`}
        dependencies={deps}
        consequence="Monitors keep running; they just no longer trace to this requirement."
        onConfirm={() => {
          setAssurance((a) => ({ ...a, requirements: a.requirements.filter((r) => r.id !== req.id), monitors: a.monitors.map((m) => (m.requirement === req.id ? { ...m, requirement: undefined } : m)) }), { label: `Delete ${req.id}` });
          setParams({}, { replace: true });
        }}
      />
    </Pane>
  );
}

function Coverage() {
  const e = useEditor();
  const [assurance] = useSection('assurance');
  const [, setParams] = useSearchParams();
  const reqs = assurance.requirements;
  const unmonitored = reqs.filter((r) => r.monitors.length === 0);
  const orphanMonitors = assurance.monitors.filter((m) => !reqs.some((r) => r.monitors.includes(m.id)) && !m.requirement);
  return (
    <div className="stack">
      <div className="row-wrap">
        <StatusBadge tone="neutral" label={`${reqs.length} requirement(s)`} />
        {unmonitored.length > 0 ? <StatusBadge tone="warning" label={`${unmonitored.length} not monitored`} /> : reqs.length > 0 && <StatusBadge tone="ok" label="Every requirement is monitored" />}
        {orphanMonitors.length > 0 && <StatusBadge tone="neutral" label={`${orphanMonitors.length} monitor(s) without a requirement`} />}
      </div>
      {reqs.length === 0 ? (
        <EmptyState compact title="No requirements">Requirements describe what must hold; monitors check them on the running twin.</EmptyState>
      ) : (
        <div style={{ overflowX: 'auto' }}>
          <table className="vts-table">
            <caption className="sr-only">Requirement to monitor coverage</caption>
            <thead>
              <tr>
                <th scope="col">Requirement</th>
                {assurance.monitors.map((m) => (
                  <th key={m.id} scope="col" className="xsmall" style={{ writingMode: 'vertical-rl', transform: 'rotate(180deg)', whiteSpace: 'nowrap', verticalAlign: 'bottom', height: 120 }}>
                    {m.name || m.id}
                  </th>
                ))}
                <th scope="col">Formal</th>
              </tr>
            </thead>
            <tbody>
              {reqs.map((r) => (
                <tr key={r.id} className="is-clickable" onClick={() => setParams({ id: r.id }, { replace: true })}>
                  <td>
                    <strong className="small">{r.id}</strong> <span className="small">{r.title}</span>{' '}
                    {r.monitors.length === 0 && <StatusBadge tone="warning" label="Not monitored" />}
                  </td>
                  {assurance.monitors.map((m) => (
                    <td key={m.id} style={{ textAlign: 'center' }} aria-label={r.monitors.includes(m.id) ? `${r.id} checked by ${m.id}` : undefined}>
                      {r.monitors.includes(m.id) ? '●' : ''}
                    </td>
                  ))}
                  <td className="mono xsmall">{r.formal ?? '—'}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      )}
      <p className="xsmall subtle">
        Monitors are defined in <Link to={blueprintRoute(e.id, e.version, 'assurance/monitors')}>Assurance → Monitors</Link>; a requirement can be checked by several monitors.
      </p>
    </div>
  );
}

export default function RequirementsPage() {
  const e = useEditor();
  const ws = useWorkspace();
  const [assurance, setAssurance] = useSection('assurance');
  const [params, setParams] = useSearchParams();
  const validation = useBlueprintValidation(e.id, e.version);
  const [tab, setTab] = useState<'requirements' | 'coverage' | 'json'>('requirements');
  const [deleting, setDeleting] = useState<string | null>(null);
  const selected = params.get('id') ?? assurance.requirements[0]?.id ?? null;
  const req = assurance.requirements.find((r) => r.id === selected) ?? null;
  const bad = useMemo(() => {
    const m = new Map<string, 'error' | 'warning'>();
    for (const f of validation.data?.findings ?? []) if (f.section === 'assurance' && f.target) m.set(f.target, m.get(f.target) === 'error' ? 'error' : f.severity);
    return m;
  }, [validation.data]);
  const add = () => {
    const id = nextId('REQ', assurance.requirements.map((r) => r.id), '-');
    setAssurance((a) => ({ ...a, requirements: [...a.requirements, { id, title: 'New requirement', category: 'safety', severity: 'warning', monitors: [] }] }), { label: 'Add requirement' });
    setParams({ id }, { replace: true });
    setTab('requirements');
  };
  const items = assurance.requirements.map((r) => ({
    id: r.id,
    label: (
      <>
        <span className="mono xsmall">{r.id}</span> {r.title}
      </>
    ),
    search: `${r.id} ${r.title} ${r.category}`,
    meta: r.monitors.length === 0 ? 'unmonitored' : r.category,
    tone: bad.get(r.id) ?? (r.monitors.length === 0 ? ('warning' as const) : undefined),
  }));
  const del = assurance.requirements.find((r) => r.id === deleting);
  return (
    <EdPage
      title="Requirements"
      description="What the twin must guarantee or watch, traced to the monitors that check it."
      actions={
        <>
          <Tabs
            value={tab}
            onChange={setTab}
            label="Requirement views"
            tabs={[
              { id: 'requirements', label: 'Requirements' },
              { id: 'coverage', label: 'Coverage' },
              ...(ws.expert ? [{ id: 'json' as const, label: 'JSON' }] : []),
            ]}
          />
          {e.editable && (
            <Button size="sm" variant="primary" icon={<Plus size={14} />} onClick={add}>
              Requirement
            </Button>
          )}
        </>
      }
      guide={<>Write each requirement in plain words, then link the monitors that check it on the running twin. A formal statement over the Digital Twin View is optional; Studio tells you whether it can be checked at design time and monitored at run time.</>}
    >
      {tab === 'coverage' ? (
        <Coverage />
      ) : tab === 'json' ? (
        <JsonSectionView value={assurance.requirements} label="Requirements" onApply={(v) => setAssurance((a) => ({ ...a, requirements: v }), { label: 'Edit requirements (JSON)' })} />
      ) : (
        <div className="vts-ed-split vts-ed-split--two">
          <Pane title="Requirements" flush>
            <SelectList
              items={items}
              selected={selected}
              onSelect={(id) => setParams({ id }, { replace: true })}
              onDelete={e.editable ? setDeleting : undefined}
              label="Requirements"
              empty={
                <EmptyState compact title="No requirements yet" action={e.editable ? <Button size="sm" onClick={add}>Add a requirement</Button> : undefined}>
                  Start with the safety requirements: what must never happen?
                </EmptyState>
              }
            />
          </Pane>
          {req ? (
            <RequirementInspector key={req.id} req={req} />
          ) : (
            <Pane className="vts-inspector" title="Requirement">
              <p className="small muted">Select a requirement to edit it, or open the coverage view.</p>
            </Pane>
          )}
        </div>
      )}
      {del && (
        <ConfirmDelete
          open
          onOpenChange={(o) => !o && setDeleting(null)}
          title={`Delete requirement ${del.id}?`}
          dependencies={assurance.monitors.filter((m) => m.requirement === del.id).map((m) => ({ what: `Monitor ${m.name || m.id}`, where: 'Monitors', route: `assurance/monitors?id=${encodeURIComponent(m.id)}` }))}
          onConfirm={() => {
            setAssurance((a) => ({ ...a, requirements: a.requirements.filter((r) => r.id !== del.id), monitors: a.monitors.map((m) => (m.requirement === del.id ? { ...m, requirement: undefined } : m)) }), { label: `Delete ${del.id}` });
            setDeleting(null);
            setParams({}, { replace: true });
          }}
        />
      )}
    </EdPage>
  );
}
