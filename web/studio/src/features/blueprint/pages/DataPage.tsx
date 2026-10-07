/**
 * Build → Data & Connectivity: the data contract (telemetry signals, events with their formal
 * PT/DT labels, commands with acknowledgement and observed consequence, static properties),
 * the data sources (MQTT, OPC UA, HTTP/REST, simulator, replay, file), the bindings from
 * source fields to contract items with unit conversion — each testable against the real
 * source — and the data-flow view (source → binding → signal → asset / ontology symbol).
 */
import { ReactFlow, Background, Controls, type Edge, type Node } from '@xyflow/react';
import '@xyflow/react/dist/style.css';
import Dagre from '@dagrejs/dagre';
import { Activity, Bell, Cable, Database, FlaskConical, Play, Plus, Send, Trash2, Variable } from 'lucide-react';
import { useMemo, useState } from 'react';
import { useSearchParams } from 'react-router-dom';
import { blueprintApi, useBlueprintValidation } from '@/api/blueprints';
import type { BindingDef, BindingTestResult, CommandDef, DataSourceDef, DataType, EventDef, SourceKind, StaticPropertyDef, TelemetryDef } from '@/api/types';
import { Button, Callout, EmptyState, ErrorBlock, KeyValue, StatusBadge, Tabs } from '@/design';
import { safeLayout } from '@/design/graphLayout';
import { nextId, useEditor, useSection } from '../editor';
import { useModelFacts, useOntologySymbols } from '../formalFacts';
import { renameEvent, renameSource, renameTelemetry } from '../refactor';
import { usedByEvent, usedBySource, usedByTelemetry } from '../refs';
import { IdField } from './StructurePage';
import {
  CheckField,
  ConfirmDelete,
  DecimalField,
  EdPage,
  FindingsInline,
  InspectorSection,
  IntField,
  JsonSectionView,
  Pane,
  SelectField,
  SelectList,
  TextArea,
  TextField,
  UsedBy,
  type Dependency,
} from '../ui';
import { useWorkspace } from '../workspace';

type Tab = 'contract' | 'connectivity' | 'flow';
type ItemKind = 'telemetry' | 'event' | 'command' | 'property';

const TYPES: { value: DataType; label: string }[] = [
  { value: 'real', label: 'real (decimal)' },
  { value: 'integer', label: 'integer' },
  { value: 'boolean', label: 'boolean' },
  { value: 'string', label: 'string' },
  { value: 'enum', label: 'enum (listed values)' },
];

function useAssetOptions() {
  const e = useEditor();
  return e.doc.structure.assets.map((a) => ({ value: a.id, label: `${a.name || a.id} (${a.id})` }));
}

// ------------------------------------------------------------------ inspectors
function TelemetryInspector({ t, findings }: { t: TelemetryDef; findings: ReturnType<typeof useFindingsFor> }) {
  const e = useEditor();
  const [data, setData] = useSection('data');
  const [, setParams] = useSearchParams();
  const assets = useAssetOptions();
  const onto = useOntologySymbols();
  const [confirm, setConfirm] = useState(false);
  const set = (patch: Partial<TelemetryDef>, label: string) =>
    setData((d) => ({ ...d, telemetry: d.telemetry.map((x) => (x.id === t.id ? { ...x, ...patch } : x)) }), { label, key: `tel.${t.id}.${Object.keys(patch)[0]}` });
  const deps = usedByTelemetry(e.doc, t.id);
  return (
    <Pane className="vts-inspector" title={`Signal · ${t.label || t.id}`} actions={<StatusBadge tone="info" icon={Activity} label="telemetry" />}>
      <InspectorSection title="Signal">
        <IdField label="Id" value={t.id} taken={new Set(data.telemetry.map((x) => x.id))} onRename={(n) => { e.updateDoc((d) => renameTelemetry(d, t.id, n), { label: `Rename signal ${t.id}` }); setParams({ telemetry: n }, { replace: true }); }} />
        <TextField label="Label" value={t.label} onChange={(v) => set({ label: v }, 'Signal label')} />
        <div className="vts-fgrid">
          <SelectField<DataType> label="Type" value={t.type} onChange={(v) => set({ type: v }, 'Signal type')} options={TYPES} />
          <TextField label="Unit" value={t.unit ?? ''} onChange={(v) => set({ unit: v }, 'Signal unit')} placeholder="degC, m/s, %" />
        </div>
        {t.type === 'enum' && <TextField label="Values" value={(t.values ?? []).join(', ')} onChange={(v) => set({ values: v.split(',').map((x) => x.trim()).filter(Boolean) }, 'Enum values')} placeholder="OFF, STANDBY, RUN" />}
        <SelectField label="Measured on asset" value={t.asset ?? ''} onChange={(v) => set({ asset: v || undefined }, 'Signal asset')} options={assets} allowEmpty="— none —" />
        <TextArea label="Description" value={t.description ?? ''} onChange={(v) => set({ description: v }, 'Signal description')} rows={2} />
      </InspectorSection>
      <InspectorSection title="Range and quality">
        <div className="vts-fgrid">
          <DecimalField label="Min" value={t.range?.min} onChange={(v) => set({ range: { ...t.range, min: v } }, 'Signal range')} unit={t.unit} />
          <DecimalField label="Max" value={t.range?.max} onChange={(v) => set({ range: { ...t.range, max: v } }, 'Signal range')} unit={t.unit} />
          <IntField label="Expected period" value={t.expectedPeriodMs} onChange={(v) => set({ expectedPeriodMs: v }, 'Expected period')} unit="ms" min={1} />
          <IntField label="Stale after" value={t.quality?.maxAgeMs} onChange={(v) => set({ quality: { ...t.quality, maxAgeMs: v } }, 'Staleness')} unit="ms" min={1} />
        </div>
      </InspectorSection>
      <InspectorSection title="Semantics">
        <SelectField
          label="Ontology symbol"
          value={t.ontologySymbol ?? ''}
          onChange={(v) => set({ ontologySymbol: v || undefined }, 'Ontology symbol')}
          options={[...onto.functions, ...onto.relations].map((s) => ({ value: s.name, label: `${s.name} — ${s.signature}` }))}
          allowEmpty={onto.present ? '— not observed by the semantics —' : 'Define the ontology first'}
          hint="Values of this signal give the meaning of the symbol at runtime (interpretations are evaluated on them)"
        />
      </InspectorSection>
      <InspectorSection title="Presentation">
        <div className="vts-fgrid">
          <TextField label="Category" value={t.presentation?.category ?? ''} onChange={(v) => set({ presentation: { ...t.presentation, category: v } }, 'Signal category')} />
          <IntField label="Precision" value={t.presentation?.precision} onChange={(v) => set({ presentation: { ...t.presentation, precision: v } }, 'Signal precision')} min={0} max={9} />
          <SelectField label="Chart" value={t.presentation?.chart ?? 'line'} onChange={(v) => set({ presentation: { ...t.presentation, chart: v } }, 'Signal chart')} options={[{ value: 'line', label: 'Line' }, { value: 'state', label: 'State band' }, { value: 'table', label: 'Table' }]} />
        </div>
      </InspectorSection>
      <FindingsBlock findings={findings} />
      <InspectorSection title="Used by"><UsedBy deps={deps} /></InspectorSection>
      {e.editable && <div><Button size="sm" variant="danger" icon={<Trash2 size={13} />} onClick={() => setConfirm(true)}>Delete signal</Button></div>}
      <ConfirmDelete open={confirm} onOpenChange={setConfirm} title={`Delete signal ${t.id}?`} dependencies={deps} onConfirm={() => { setData((d) => ({ ...d, telemetry: d.telemetry.filter((x) => x.id !== t.id) }), { label: `Delete signal ${t.id}` }); setParams({}, { replace: true }); }} />
    </Pane>
  );
}

function EventInspector({ ev, findings }: { ev: EventDef; findings: ReturnType<typeof useFindingsFor> }) {
  const e = useEditor();
  const [data, setData] = useSection('data');
  const [, setParams] = useSearchParams();
  const assets = useAssetOptions();
  const pt = useModelFacts('pt');
  const dt = useModelFacts('dt');
  const [confirm, setConfirm] = useState(false);
  const set = (patch: Partial<EventDef>, label: string) =>
    setData((d) => ({ ...d, events: d.events.map((x) => (x.id === ev.id ? { ...x, ...patch } : x)) }), { label, key: `ev.${ev.id}.${Object.keys(patch)[0]}` });
  const deps = usedByEvent(e.doc, ev.id);
  return (
    <Pane className="vts-inspector" title={`Event · ${ev.label || ev.id}`} actions={<StatusBadge tone="info" icon={Bell} label="event" />}>
      <InspectorSection title="Event">
        <IdField label="Id" value={ev.id} taken={new Set(data.events.map((x) => x.id))} onRename={(n) => { e.updateDoc((d) => renameEvent(d, ev.id, n), { label: `Rename event ${ev.id}` }); setParams({ event: n }, { replace: true }); }} />
        <TextField label="Label" value={ev.label} onChange={(v) => set({ label: v }, 'Event label')} />
        <SelectField label="Raised by asset" value={ev.asset ?? ''} onChange={(v) => set({ asset: v || undefined }, 'Event asset')} options={assets} allowEmpty="— none —" />
        <TextArea label="Description" value={ev.description ?? ''} onChange={(v) => set({ description: v }, 'Event description')} rows={2} />
      </InspectorSection>
      <InspectorSection title="Formal labels">
        <p className="xsmall subtle">The label this event carries in each behavioural view. PT labels are what the physical system reports; the runtime translates them into DT labels through the alignment's label equivalence.</p>
        <SelectField label="Physical System View (PT) label" value={ev.formal?.pt ?? ''} onChange={(v) => set({ formal: { ...ev.formal, pt: v || undefined } }, 'PT label')} options={pt.labels.map((l) => ({ value: l, label: l }))} allowEmpty={pt.present ? '— not a PT event —' : 'Define the PT view first'} />
        <SelectField label="Digital Twin View (DT) label" value={ev.formal?.dt ?? ''} onChange={(v) => set({ formal: { ...ev.formal, dt: v || undefined } }, 'DT label')} options={dt.labels.map((l) => ({ value: l, label: l }))} allowEmpty={dt.present ? '— not a DT event —' : 'Define the DT view first'} />
      </InspectorSection>
      <InspectorSection title="Payload">
        {(ev.payload ?? []).map((p, i) => (
          <div key={i} className="row">
            <input className="vts-input mono grow" value={p.key} disabled={!e.editable} aria-label="Payload field" onChange={(x) => set({ payload: (ev.payload ?? []).map((q, j) => (j === i ? { ...q, key: x.target.value } : q)) }, 'Payload field')} />
            <select className="vts-select" value={p.type} disabled={!e.editable} aria-label="Payload type" onChange={(x) => set({ payload: (ev.payload ?? []).map((q, j) => (j === i ? { ...q, type: x.target.value as DataType } : q)) }, 'Payload type')}>
              {TYPES.map((t) => <option key={t.value} value={t.value}>{t.value}</option>)}
            </select>
            <Button size="sm" variant="ghost" iconOnly icon={<Trash2 size={13} />} disabled={!e.editable} onClick={() => set({ payload: (ev.payload ?? []).filter((_, j) => j !== i) }, 'Remove payload field')}>Remove field</Button>
          </div>
        ))}
        {e.editable && <Button size="sm" variant="ghost" icon={<Plus size={13} />} onClick={() => set({ payload: [...(ev.payload ?? []), { key: 'field', type: 'string' }] }, 'Add payload field')}>Add payload field</Button>}
      </InspectorSection>
      <FindingsBlock findings={findings} />
      <InspectorSection title="Used by"><UsedBy deps={deps} /></InspectorSection>
      {e.editable && <div><Button size="sm" variant="danger" icon={<Trash2 size={13} />} onClick={() => setConfirm(true)}>Delete event</Button></div>}
      <ConfirmDelete open={confirm} onOpenChange={setConfirm} title={`Delete event ${ev.id}?`} dependencies={deps} onConfirm={() => { setData((d) => ({ ...d, events: d.events.filter((x) => x.id !== ev.id) }), { label: `Delete event ${ev.id}` }); setParams({}, { replace: true }); }} />
    </Pane>
  );
}

function CommandInspector({ c, findings }: { c: CommandDef; findings: ReturnType<typeof useFindingsFor> }) {
  const e = useEditor();
  const [data, setData] = useSection('data');
  const [, setParams] = useSearchParams();
  const assets = useAssetOptions();
  const events = data.events.map((x) => ({ value: x.id, label: x.label || x.id }));
  const set = (patch: Partial<CommandDef>, label: string) =>
    setData((d) => ({ ...d, commands: d.commands.map((x) => (x.id === c.id ? { ...x, ...patch } : x)) }), { label, key: `cmd.${c.id}.${Object.keys(patch)[0]}` });
  return (
    <Pane className="vts-inspector" title={`Command · ${c.label || c.id}`} actions={<StatusBadge tone="info" icon={Send} label="command" />}>
      <InspectorSection title="Command">
        <TextField label="Id" value={c.id} onChange={(v) => set({ id: v }, 'Command id')} mono />
        <TextField label="Label" value={c.label} onChange={(v) => set({ label: v }, 'Command label')} />
        <SelectField label="Target asset" value={c.asset ?? ''} onChange={(v) => set({ asset: v || undefined }, 'Command asset')} options={assets} allowEmpty="— none —" />
        <TextArea label="Description" value={c.description ?? ''} onChange={(v) => set({ description: v }, 'Command description')} rows={2} />
      </InspectorSection>
      <InspectorSection title="Parameters">
        {(c.parameters ?? []).map((p, i) => (
          <div key={i} className="row">
            <input className="vts-input mono grow" value={p.key} disabled={!e.editable} aria-label="Parameter" onChange={(x) => set({ parameters: (c.parameters ?? []).map((q, j) => (j === i ? { ...q, key: x.target.value } : q)) }, 'Parameter')} />
            <select className="vts-select" value={p.type} disabled={!e.editable} aria-label="Parameter type" onChange={(x) => set({ parameters: (c.parameters ?? []).map((q, j) => (j === i ? { ...q, type: x.target.value as DataType } : q)) }, 'Parameter type')}>
              {TYPES.map((t) => <option key={t.value} value={t.value}>{t.value}</option>)}
            </select>
            <Button size="sm" variant="ghost" iconOnly icon={<Trash2 size={13} />} disabled={!e.editable} onClick={() => set({ parameters: (c.parameters ?? []).filter((_, j) => j !== i) }, 'Remove parameter')}>Remove parameter</Button>
          </div>
        ))}
        {e.editable && <Button size="sm" variant="ghost" icon={<Plus size={13} />} onClick={() => set({ parameters: [...(c.parameters ?? []), { key: 'value', type: 'real' }] }, 'Add parameter')}>Add parameter</Button>}
      </InspectorSection>
      <InspectorSection title="Acknowledgement and observed consequence">
        <p className="xsmall subtle">A command is accepted when its acknowledgement event arrives, and has worked only when its consequence is observed — never just because it was sent.</p>
        <div className="vts-fgrid">
          <SelectField label="Acknowledged by" value={c.acknowledgement?.event ?? ''} onChange={(v) => set({ acknowledgement: { ...c.acknowledgement, event: v || undefined } }, 'Acknowledgement')} options={events} allowEmpty="— none —" />
          <IntField label="within" value={c.acknowledgement?.timeoutMs} onChange={(v) => set({ acknowledgement: { ...c.acknowledgement, timeoutMs: v } }, 'Acknowledgement timeout')} unit="ms" min={1} />
          <SelectField label="Observed consequence" value={c.observedConsequence?.event ?? ''} onChange={(v) => set({ observedConsequence: { ...c.observedConsequence, event: v || undefined } }, 'Observed consequence')} options={events} allowEmpty="— none —" />
          <IntField label="within" value={c.observedConsequence?.timeoutMs} onChange={(v) => set({ observedConsequence: { ...c.observedConsequence, timeoutMs: v } }, 'Consequence timeout')} unit="ms" min={1} />
        </div>
      </InspectorSection>
      <FindingsBlock findings={findings} />
      {e.editable && <div><Button size="sm" variant="danger" icon={<Trash2 size={13} />} onClick={() => { setData((d) => ({ ...d, commands: d.commands.filter((x) => x.id !== c.id) }), { label: `Delete command ${c.id}` }); setParams({}, { replace: true }); }}>Delete command</Button></div>}
    </Pane>
  );
}

function PropertiesEditor() {
  const e = useEditor();
  const [data, setData] = useSection('data');
  const assets = useAssetOptions();
  const set = (i: number, patch: Partial<StaticPropertyDef>) => setData((d) => ({ ...d, properties: d.properties.map((p, j) => (j === i ? { ...p, ...patch } : p)) }), { label: 'Static property', key: `prop.${i}` });
  return (
    <Pane title="Static properties" actions={e.editable && <Button size="sm" icon={<Plus size={13} />} onClick={() => setData((d) => ({ ...d, properties: [...d.properties, { id: nextId('property', d.properties.map((p) => p.id)), label: 'Property', type: 'string', perInstance: true, value: '' }] }), { label: 'Add static property' })}>Property</Button>}>
      {data.properties.length === 0 ? (
        <EmptyState compact title="No static properties">Fixed facts of the twin (serial number, rated power…), set per Blueprint or per instance.</EmptyState>
      ) : (
        <table className="vts-table">
          <thead><tr><th>Id</th><th>Label</th><th>Type</th><th>Asset</th><th>Per instance</th><th>Value</th><th /></tr></thead>
          <tbody>
            {data.properties.map((p, i) => (
              <tr key={i}>
                <td><input className="vts-input mono" value={p.id} disabled={!e.editable} aria-label="Property id" onChange={(x) => set(i, { id: x.target.value })} /></td>
                <td><input className="vts-input" value={p.label} disabled={!e.editable} aria-label="Property label" onChange={(x) => set(i, { label: x.target.value })} /></td>
                <td><select className="vts-select" value={p.type} disabled={!e.editable} aria-label="Property type" onChange={(x) => set(i, { type: x.target.value as DataType })}>{TYPES.map((t) => <option key={t.value} value={t.value}>{t.value}</option>)}</select></td>
                <td><select className="vts-select" value={p.asset ?? ''} disabled={!e.editable} aria-label="Asset" onChange={(x) => set(i, { asset: x.target.value || undefined })}><option value="">—</option>{assets.map((a) => <option key={a.value} value={a.value}>{a.label}</option>)}</select></td>
                <td><input type="checkbox" checked={!!p.perInstance} disabled={!e.editable} aria-label="Set per instance" onChange={(x) => set(i, { perInstance: x.target.checked })} /></td>
                <td><input className="vts-input" value={p.value ?? ''} disabled={!e.editable} aria-label="Value" onChange={(x) => set(i, { value: x.target.value })} placeholder={p.perInstance ? 'set per instance' : ''} /></td>
                <td>{e.editable && <Button size="sm" variant="ghost" iconOnly icon={<Trash2 size={13} />} onClick={() => setData((d) => ({ ...d, properties: d.properties.filter((_, j) => j !== i) }), { label: 'Delete static property' })}>Delete property</Button>}</td>
              </tr>
            ))}
          </tbody>
        </table>
      )}
    </Pane>
  );
}

function useFindingsFor(section: string, target: string | null) {
  const e = useEditor();
  const v = useBlueprintValidation(e.id, e.version);
  return (v.data?.findings ?? []).filter((f) => f.section === section && (!target || f.target === target));
}

function FindingsBlock({ findings }: { findings: ReturnType<typeof useFindingsFor> }) {
  if (findings.length === 0) return null;
  return <InspectorSection title="Problems"><FindingsInline findings={findings} /></InspectorSection>;
}

function ContractTab() {
  const e = useEditor();
  const [data, setData] = useSection('data');
  const [params, setParams] = useSearchParams();
  const validation = useBlueprintValidation(e.id, e.version);
  const sel: { kind: ItemKind; id: string } | null = params.get('telemetry')
    ? { kind: 'telemetry', id: params.get('telemetry')! }
    : params.get('event')
      ? { kind: 'event', id: params.get('event')! }
      : params.get('command')
        ? { kind: 'command', id: params.get('command')! }
        : params.get('select')
          ? { kind: data.telemetry.some((t) => t.id === params.get('select')) ? 'telemetry' : data.events.some((x) => x.id === params.get('select')) ? 'event' : 'command', id: params.get('select')! }
          : null;
  const bad = new Map<string, 'error' | 'warning'>();
  for (const f of validation.data?.findings ?? []) if ((f.section === 'data' || f.section === 'connectivity') && f.target) bad.set(f.target, bad.get(f.target) === 'error' ? 'error' : f.severity);
  const select = (kind: ItemKind, id: string) => setParams({ [kind]: id }, { replace: true });
  const findings = useFindingsFor('data', sel?.id ?? '__none__');
  const add = (kind: ItemKind) => {
    if (kind === 'telemetry') {
      const id = nextId('signal', data.telemetry.map((t) => t.id));
      setData((d) => ({ ...d, telemetry: [...d.telemetry, { id, label: 'New signal', type: 'real', unit: '', asset: e.doc.structure.root || undefined, expectedPeriodMs: 1000, quality: { maxAgeMs: 10000 }, presentation: { precision: 2, chart: 'line' }, range: {} }] }), { label: 'Add signal' });
      select('telemetry', id);
    } else if (kind === 'event') {
      const id = nextId('event', data.events.map((t) => t.id));
      setData((d) => ({ ...d, events: [...d.events, { id, label: 'New event', asset: e.doc.structure.root || undefined, payload: [], formal: {} }] }), { label: 'Add event' });
      select('event', id);
    } else if (kind === 'command') {
      const id = nextId('command', data.commands.map((t) => t.id));
      setData((d) => ({ ...d, commands: [...d.commands, { id, label: 'New command', parameters: [] }] }), { label: 'Add command' });
      select('command', id);
    }
  };
  const group = (title: string, kind: ItemKind, icon: React.ReactNode, items: { id: string; label: string; meta?: string }[]) => (
    <Pane title={<span className="row">{icon} {title}</span>} flush actions={e.editable && <Button size="sm" variant="ghost" icon={<Plus size={13} />} onClick={() => add(kind)}>Add</Button>}>
      <SelectList
        label={title}
        items={items.map((i) => ({ id: i.id, label: i.label, meta: i.meta, search: `${i.id} ${i.label}`, tone: bad.get(i.id) }))}
        selected={sel?.kind === kind ? sel.id : null}
        onSelect={(id) => select(kind, id)}
        empty={<p className="small muted" style={{ padding: 8 }}>None yet.</p>}
      />
    </Pane>
  );
  const t = sel?.kind === 'telemetry' ? data.telemetry.find((x) => x.id === sel.id) : undefined;
  const ev = sel?.kind === 'event' ? data.events.find((x) => x.id === sel.id) : undefined;
  const cmd = sel?.kind === 'command' ? data.commands.find((x) => x.id === sel.id) : undefined;
  return (
    <div className="stack">
      <div className="vts-ed-split vts-ed-split--wide-inspector">
        <div className="grid-3" style={{ alignItems: 'start' }}>
          {group('Telemetry', 'telemetry', <Activity size={14} aria-hidden="true" />, data.telemetry.map((x) => ({ id: x.id, label: x.label || x.id, meta: [x.type, x.unit].filter(Boolean).join(' · ') })))}
          {group('Events', 'event', <Bell size={14} aria-hidden="true" />, data.events.map((x) => ({ id: x.id, label: x.label || x.id, meta: x.formal?.dt ?? x.formal?.pt ?? '' })))}
          {group('Commands', 'command', <Send size={14} aria-hidden="true" />, data.commands.map((x) => ({ id: x.id, label: x.label || x.id })))}
        </div>
        {t ? <TelemetryInspector key={t.id} t={t} findings={findings} /> : ev ? <EventInspector key={ev.id} ev={ev} findings={findings} /> : cmd ? <CommandInspector key={cmd.id} c={cmd} findings={findings} /> : (
          <Pane className="vts-inspector" title="Inspector"><p className="small muted">Select a signal, event or command.</p></Pane>
        )}
      </div>
      <PropertiesEditor />
    </div>
  );
}

// ------------------------------------------------------------------ connectivity
const SOURCE_KINDS: { value: SourceKind; label: string }[] = [
  { value: 'simulator', label: 'Simulator' },
  { value: 'mqtt', label: 'MQTT' },
  { value: 'opcua', label: 'OPC UA' },
  { value: 'rest', label: 'HTTP / REST' },
  { value: 'replay', label: 'Replay (recorded trace)' },
  { value: 'file', label: 'File (CSV / JSON lines)' },
];

function SourceInspector({ s }: { s: DataSourceDef }) {
  const e = useEditor();
  const [conn, setConn] = useSection('connectivity');
  const [, setParams] = useSearchParams();
  const [confirm, setConfirm] = useState(false);
  const set = (patch: Partial<DataSourceDef>, label: string) => setConn((c) => ({ ...c, sources: c.sources.map((x) => (x.id === s.id ? { ...x, ...patch } : x)) }), { label, key: `src.${s.id}.${Object.keys(patch)[0]}` });
  const cfg = s.config ?? {};
  const setCfg = (k: string, v: string | number | boolean | undefined) => set({ config: Object.fromEntries(Object.entries({ ...cfg, [k]: v }).filter(([, x]) => x !== undefined && x !== '')) as DataSourceDef['config'] }, `Source ${k}`);
  const str = (k: string) => (cfg[k] === undefined ? '' : String(cfg[k]));
  const findings = useFindingsFor('connectivity', s.id);
  const deps: Dependency[] = usedBySource(e.doc, s.id);
  return (
    <Pane className="vts-inspector" title={`Source · ${s.name || s.id}`} actions={<StatusBadge tone="neutral" icon={Database} label={s.kind} />}>
      <InspectorSection title="Source">
        <IdField label="Id" value={s.id} taken={new Set(conn.sources.map((x) => x.id))} onRename={(n) => { e.updateDoc((d) => renameSource(d, s.id, n), { label: `Rename source ${s.id}` }); setParams({ tab: 'connectivity', source: n }, { replace: true }); }} />
        <TextField label="Name" value={s.name} onChange={(v) => set({ name: v }, 'Source name')} />
        <SelectField<SourceKind> label="Kind" value={s.kind} onChange={(v) => set({ kind: v, config: v === 'simulator' ? { model: 'event-script' } : {} }, 'Source kind')} options={SOURCE_KINDS} />
      </InspectorSection>
      <InspectorSection title="Connection">
        {s.kind === 'mqtt' && (
          <div className="vts-fgrid">
            <TextField label="Broker host" value={str('host')} onChange={(v) => setCfg('host', v)} placeholder="broker.plant.local" />
            <IntField label="Port" value={cfg.port as number | undefined} onChange={(v) => setCfg('port', v)} min={1} max={65535} />
            <TextField label="Topic" value={str('topic')} onChange={(v) => setCfg('topic', v)} placeholder="plant/line4/#" mono />
            <CheckField label="TLS" checked={!!cfg.tls} onChange={(v) => setCfg('tls', v)} />
            <TextField label="Credential reference" value={str('credentialRef')} onChange={(v) => setCfg('credentialRef', v)} hint="Name of a secret in the deployment environment — never the secret itself" />
          </div>
        )}
        {s.kind === 'opcua' && (
          <>
            <Callout tone="warning">This build has no OPC UA client: the source is validated and stored, but cannot be tested or deployed (the adapter reports itself unavailable).</Callout>
            <div className="vts-fgrid">
              <TextField label="Endpoint" value={str('endpoint')} onChange={(v) => setCfg('endpoint', v)} placeholder="opc.tcp://plc:4840" mono />
              <SelectField label="Security" value={str('security') || 'None'} onChange={(v) => setCfg('security', v)} options={['None', 'Sign', 'SignAndEncrypt'].map((x) => ({ value: x, label: x }))} />
            </div>
          </>
        )}
        {s.kind === 'rest' && (
          <div className="vts-fgrid">
            <TextField label="URL" value={str('url')} onChange={(v) => setCfg('url', v)} placeholder="http://gateway/api/values" mono />
            <IntField label="Poll interval" value={cfg.intervalMs as number | undefined} onChange={(v) => setCfg('intervalMs', v)} unit="ms" min={100} />
          </div>
        )}
        {(s.kind === 'replay' || s.kind === 'file') && (
          <div className="vts-fgrid">
            <TextField label="Trace file" value={str('file')} onChange={(v) => setCfg('file', v)} placeholder="uploads/cycle-2026-10-01.jsonl" mono hint="Relative to the Studio data directory" />
            {s.kind === 'replay' && <DecimalField label="Speed" value={str('speed') || undefined} onChange={(v) => setCfg('speed', v)} hint="Logical seconds per wall second" />}
          </div>
        )}
        {s.kind === 'simulator' && (
          <SelectField label="Simulator" value={str('model') || 'event-script'} onChange={(v) => setCfg('model', v)} options={[{ value: 'event-script', label: 'Event script (Preview / Simulation)' }, { value: 'mobile-robot', label: 'Mobile-robot simulator (World & Layout)' }]} hint="The simulator itself is configured in the Blueprint's simulation section" />
        )}
      </InspectorSection>
      <FindingsBlock findings={findings} />
      <InspectorSection title="Bindings using this source"><UsedBy deps={deps} /></InspectorSection>
      {e.editable && <div><Button size="sm" variant="danger" icon={<Trash2 size={13} />} onClick={() => setConfirm(true)}>Delete source</Button></div>}
      <ConfirmDelete open={confirm} onOpenChange={setConfirm} title={`Delete source ${s.id}?`} dependencies={deps} onConfirm={() => setConn((c) => ({ ...c, sources: c.sources.filter((x) => x.id !== s.id) }), { label: `Delete source ${s.id}` })} />
    </Pane>
  );
}

function TestResult({ r }: { r: BindingTestResult }) {
  const ok = r.status === 'ok';
  return (
    <div className="stack-sm" style={{ padding: 8, background: 'var(--bg)', borderRadius: 6, border: '1px solid var(--divider)' }}>
      <div className="row-wrap">
        <StatusBadge tone={ok ? 'ok' : r.status === 'adapter_unavailable' ? 'neutral' : 'critical'} label={ok ? 'Value received' : r.status.replace(/_/g, ' ')} />
        {r.latencyMs !== undefined && <span className="xsmall subtle">{r.latencyMs} ms</span>}
      </div>
      {r.detail && <p className="small">{r.detail}</p>}
      {r.value !== undefined && (
        <KeyValue
          compact
          items={[
            ['Raw value', <code key="r" className="vts-formula">{JSON.stringify(r.value)}</code>],
            ['Canonical value', <code key="c" className="vts-formula">{JSON.stringify(r.canonical)}</code>],
            ['Type check', r.typeCheck === 'ok' ? <StatusBadge key="t" tone="ok" label="Valid" /> : <span key="t" style={{ color: 'var(--crit)' }}>{r.typeCheck}</span>],
            ['Unit', r.unit ? `${r.unit.source || '—'} → ${r.unit.canonical || '—'} (×${r.unit.scale ?? '1'} + ${r.unit.offset ?? '0'})` : '—'],
            ['Timestamp', r.timestamp || 'not provided by the source (ingestion time is used)'],
            ['Quality', r.quality ?? '—'],
          ]}
        />
      )}
      {r.availableFields && r.availableFields.length > 0 && <p className="xsmall subtle">Fields in the payload: {r.availableFields.join(', ')}</p>}
    </div>
  );
}

function BindingsTable() {
  const e = useEditor();
  const [conn, setConn] = useSection('connectivity');
  const [data] = useSection('data');
  const [params] = useSearchParams();
  const [results, setResults] = useState<Record<string, BindingTestResult | Error | 'running'>>({});
  const selected = params.get('select');
  const set = (i: number, patch: Partial<BindingDef>) => setConn((c) => ({ ...c, bindings: c.bindings.map((b, j) => (j === i ? { ...b, ...patch } : b)) }), { label: 'Edit binding', key: `bind.${i}.${Object.keys(patch)[0]}` });
  const unbound = data.telemetry.filter((t) => !conn.bindings.some((b) => b.target.kind === 'telemetry' && b.target.id === t.id));
  const test = async (b: BindingDef) => {
    setResults((r) => ({ ...r, [b.id]: 'running' }));
    try {
      await e.saveNow();
      const r = await blueprintApi.testBinding(e.id, e.version, { binding: b });
      setResults((x) => ({ ...x, [b.id]: r }));
    } catch (err) {
      setResults((x) => ({ ...x, [b.id]: err instanceof Error ? err : new Error(String(err)) }));
    }
  };
  return (
    <Pane
      title={<span className="row"><Cable size={14} aria-hidden="true" /> Bindings</span>}
      actions={e.editable && (
        <Button size="sm" icon={<Plus size={13} />} disabled={conn.sources.length === 0} onClick={() => {
          const t = unbound[0] ?? data.telemetry[0];
          setConn((c) => ({ ...c, bindings: [...c.bindings, { id: nextId(`b-${t?.id ?? 'signal'}`, c.bindings.map((b) => b.id), '-'), target: { kind: 'telemetry', id: t?.id ?? '' }, source: c.sources[0]!.id, select: { field: t?.id ?? '' } }] }), { label: 'Add binding' });
        }}>
          Binding
        </Button>
      )}
    >
      {unbound.length > 0 && <Callout tone="warning" title={`${unbound.length} unbound signal${unbound.length === 1 ? '' : 's'}`}>{unbound.map((t) => t.id).join(', ')} — no source feeds them yet.</Callout>}
      {conn.bindings.length === 0 ? (
        <EmptyState compact title="No bindings">A binding maps a field of a source's payload to a signal or event of the contract, with unit conversion.</EmptyState>
      ) : (
        <table className="vts-table" style={{ marginTop: 8 }}>
          <thead><tr><th>Target</th><th>Source</th><th>Field / path</th><th>Scale</th><th>Offset</th><th>Test</th><th /></tr></thead>
          <tbody>
            {conn.bindings.map((b, i) => {
              const r = results[b.id];
              return (
                <tr key={b.id} aria-selected={b.id === selected}>
                  <td>
                    <select className="vts-select" value={`${b.target.kind}:${b.target.id}`} disabled={!e.editable} aria-label="Binding target" onChange={(x) => { const [k, ...rest] = x.target.value.split(':'); set(i, { target: { kind: k as 'telemetry' | 'event', id: rest.join(':') } }); }}>
                      {data.telemetry.map((t) => <option key={`t${t.id}`} value={`telemetry:${t.id}`}>signal · {t.label || t.id}</option>)}
                      {data.events.map((t) => <option key={`e${t.id}`} value={`event:${t.id}`}>event · {t.label || t.id}</option>)}
                    </select>
                  </td>
                  <td>
                    <select className="vts-select" value={b.source} disabled={!e.editable} aria-label="Source" onChange={(x) => set(i, { source: x.target.value })}>
                      {conn.sources.map((s) => <option key={s.id} value={s.id}>{s.name || s.id}</option>)}
                    </select>
                  </td>
                  <td><input className="vts-input mono" value={b.select.field ?? b.select.path ?? ''} disabled={!e.editable} aria-label="Field or JSON path" onChange={(x) => set(i, { select: x.target.value.startsWith('$.') ? { path: x.target.value } : { field: x.target.value } })} /></td>
                  <td><input className="vts-input num" style={{ width: 70 }} value={b.unit?.scale ?? ''} placeholder="1" disabled={!e.editable} aria-label="Scale" onChange={(x) => set(i, { unit: { ...b.unit, scale: x.target.value || undefined } })} /></td>
                  <td><input className="vts-input num" style={{ width: 70 }} value={b.unit?.offset ?? ''} placeholder="0" disabled={!e.editable} aria-label="Offset" onChange={(x) => set(i, { unit: { ...b.unit, offset: x.target.value || undefined } })} /></td>
                  <td style={{ minWidth: 260 }}>
                    <Button size="sm" icon={<Play size={12} />} loading={r === 'running'} onClick={() => void test(b)}>Test connection</Button>
                    {r && r !== 'running' && (r instanceof Error ? <ErrorBlock error={r} compact /> : <TestResult r={r} />)}
                  </td>
                  <td>{e.editable && <Button size="sm" variant="ghost" iconOnly icon={<Trash2 size={13} />} onClick={() => setConn((c) => ({ ...c, bindings: c.bindings.filter((_, j) => j !== i) }), { label: 'Delete binding' })}>Delete binding</Button>}</td>
                </tr>
              );
            })}
          </tbody>
        </table>
      )}
    </Pane>
  );
}

function ConnectivityTab() {
  const e = useEditor();
  const [conn, setConn] = useSection('connectivity');
  const [params, setParams] = useSearchParams();
  const sel = params.get('source') ?? (conn.sources.some((s) => s.id === params.get('select')) ? params.get('select') : null);
  const source = conn.sources.find((s) => s.id === sel);
  return (
    <div className="stack">
      <div className="vts-ed-split vts-ed-split--wide-inspector">
        <Pane title={<span className="row"><Database size={14} aria-hidden="true" /> Data sources</span>} flush actions={e.editable && (
          <Button size="sm" icon={<Plus size={13} />} onClick={() => { const id = nextId('source', conn.sources.map((s) => s.id), '-'); setConn((c) => ({ ...c, sources: [...c.sources, { id, kind: 'simulator', name: 'Simulator', config: { model: 'event-script' } }] }), { label: 'Add source' }); setParams({ tab: 'connectivity', source: id }, { replace: true }); }}>
            Source
          </Button>
        )}>
          <SelectList
            label="Data sources"
            items={conn.sources.map((s) => ({ id: s.id, label: s.name || s.id, meta: s.kind }))}
            selected={sel}
            onSelect={(id) => setParams({ tab: 'connectivity', source: id }, { replace: true })}
            empty={<EmptyState compact title="No data sources">Add the simulator for previews and tests, and the real source (MQTT, OPC UA, REST, replay) for deployments.</EmptyState>}
          />
        </Pane>
        {source ? <SourceInspector key={source.id} s={source} /> : <Pane className="vts-inspector" title="Inspector"><p className="small muted">Select a data source.</p></Pane>}
      </div>
      <BindingsTable />
    </div>
  );
}

// ------------------------------------------------------------------ data flow
function DataFlow() {
  const e = useEditor();
  const { connectivity: conn, data, structure } = e.doc;
  const { nodes, edges } = useMemo(() => {
    const g = new Dagre.graphlib.Graph();
    g.setGraph({ rankdir: 'LR', nodesep: 14, ranksep: 90 });
    g.setDefaultEdgeLabel(() => ({}));
    const nodes: Node[] = [];
    const edges: Edge[] = [];
    const add = (id: string, label: string, kind: 'source' | 'signal' | 'event' | 'asset' | 'symbol') => {
      if (nodes.some((n) => n.id === id)) return;
      g.setNode(id, { width: 180, height: 40 });
      const colour = kind === 'source' ? 'var(--accent)' : kind === 'asset' ? 'var(--text-muted)' : kind === 'symbol' ? 'var(--formal)' : kind === 'event' ? 'var(--warn)' : 'var(--ok)';
      nodes.push({ id, position: { x: 0, y: 0 }, data: { label }, style: { width: 180, fontSize: 11, borderRadius: 8, border: `2px solid ${colour}`, background: 'var(--surface)' } });
    };
    for (const s of conn.sources) add(`s:${s.id}`, `${s.name || s.id} (${s.kind})`, 'source');
    for (const b of conn.bindings) {
      const tid = `${b.target.kind === 'event' ? 'e' : 't'}:${b.target.id}`;
      const label = b.target.kind === 'event' ? data.events.find((x) => x.id === b.target.id)?.label : data.telemetry.find((x) => x.id === b.target.id)?.label;
      add(tid, `${label ?? b.target.id}`, b.target.kind === 'event' ? 'event' : 'signal');
      g.setEdge(`s:${b.source}`, tid);
      edges.push({ id: `b:${b.id}`, source: `s:${b.source}`, target: tid, label: b.select.field ?? b.select.path ?? '', labelStyle: { fontSize: 9 } });
    }
    for (const t of data.telemetry) {
      add(`t:${t.id}`, t.label || t.id, 'signal');
      if (t.asset) {
        const a = structure.assets.find((x) => x.id === t.asset);
        add(`a:${t.asset}`, `asset · ${a?.name ?? t.asset}`, 'asset');
        g.setEdge(`t:${t.id}`, `a:${t.asset}`);
        edges.push({ id: `ta:${t.id}`, source: `t:${t.id}`, target: `a:${t.asset}`, style: { strokeDasharray: '4 3' } });
      }
      if (t.ontologySymbol) {
        add(`k:${t.ontologySymbol}`, `symbol · ${t.ontologySymbol}`, 'symbol');
        g.setEdge(`t:${t.id}`, `k:${t.ontologySymbol}`);
        edges.push({ id: `tk:${t.id}`, source: `t:${t.id}`, target: `k:${t.ontologySymbol}`, style: { stroke: 'var(--formal)' } });
      }
    }
    safeLayout(g);
    for (const n of nodes) {
      const p = g.node(n.id) as unknown as { x: number; y: number } | undefined;
      n.position = { x: (p?.x ?? 0) - 90, y: (p?.y ?? 0) - 20 };
    }
    return { nodes, edges };
  }, [conn, data, structure]);
  if (nodes.length === 0) return <EmptyState title="No data flow yet">Add sources, signals and bindings.</EmptyState>;
  return (
    <div className="stack-sm">
      <div className="row-wrap xsmall">
        <StatusBadge tone="info" label="Source" /> → <StatusBadge tone="ok" label="Signal" /> / <StatusBadge tone="warning" label="Event" /> → <StatusBadge tone="neutral" label="Asset" /> · <StatusBadge tone="formal" label="Ontology symbol" />
      </div>
      <div className="vts-canvas-wrap" style={{ height: 'calc(100vh - var(--topbar-height) - 300px)' }}>
        <ReactFlow nodes={nodes} edges={edges} fitView nodesDraggable={false} proOptions={{ hideAttribution: true }}>
          <Background gap={20} />
          <Controls showInteractive={false} />
        </ReactFlow>
      </div>
    </div>
  );
}

export default function DataPage() {
  const ws = useWorkspace();
  const [params, setParams] = useSearchParams();
  const [data, setData] = useSection('data');
  const [conn, setConn] = useSection('connectivity');
  const [json, setJson] = useState(false);
  const tab = (params.get('tab') as Tab | null) ?? (params.get('source') ? 'connectivity' : 'contract');
  return (
    <EdPage
      title="Data & Connectivity"
      description="The data contract of the twin type, the sources it reads, and how source fields bind to it. Unit conversions and type checks are explicit; nothing is coerced silently."
      actions={
        <>
          <Button size="sm" variant="ghost" icon={<FlaskConical size={13} />} onClick={() => setParams({ tab: 'connectivity' })}>Test bindings</Button>
          {ws.expert && <Button size="sm" variant="ghost" onClick={() => setJson((j) => !j)}>{json ? 'Form view' : 'JSON view'}</Button>}
        </>
      }
      guide={<>Define the <strong>signals</strong> the twin observes (with type, unit and range), the <strong>events</strong> the physical system reports (and their PT/DT labels), and <strong>commands</strong>. Then add data <strong>sources</strong> and <strong>bind</strong> each signal to a source field; <em>Test connection</em> fetches a real sample.</>}
    >
      <Tabs<Tab>
        label="Data views"
        value={tab}
        onChange={(t) => setParams({ tab: t }, { replace: true })}
        tabs={[
          { id: 'contract', label: <span className="row"><Variable size={14} aria-hidden="true" /> Data contract</span> },
          { id: 'connectivity', label: <span className="row"><Cable size={14} aria-hidden="true" /> Sources & bindings</span> },
          { id: 'flow', label: 'Data flow' },
        ]}
      />
      {json ? (
        <div className="grid-2">
          <JsonSectionView value={data} label="Data contract" onApply={(v) => setData(v, { label: 'Edit data contract (JSON)' })} />
          <JsonSectionView value={conn} label="Connectivity" onApply={(v) => setConn(v, { label: 'Edit connectivity (JSON)' })} />
        </div>
      ) : tab === 'connectivity' ? (
        <ConnectivityTab />
      ) : tab === 'flow' ? (
        <DataFlow />
      ) : (
        <ContractTab />
      )}
    </EdPage>
  );
}
