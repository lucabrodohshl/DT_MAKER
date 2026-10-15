/**
 * The inspector: everything about the selection, in hairline-separated blocks (no cards).
 * Fields commit on Enter or when they lose focus, so a rename or an expression is one undoable
 * operation. Problems the server found on the element are listed with it.
 */
import { useState, type ReactNode } from 'react';
import { ArrowLeftRight, Trash2 } from 'lucide-react';
import {
  addAction,
  addEvent,
  addPart,
  addProperty,
  addState,
  dependents,
  effective,
  locate,
  parseDeclaration,
  remove,
  renameMember,
  renamePart,
  renameState,
  renameType,
  rootType,
  triggerText,
  typeByName,
  updateAction,
  updateEvent,
  updatePart,
  updateProperty,
  updateRelationship,
  updateState,
  updateTransition,
  updateType,
  windowText,
} from './ops';
import { useModel } from './store';
import type { Action, AssetType, Group, Link, ModelEvent, Part, Property, Relationship, State, TimeAtom, Transition, Trigger, TwinModel } from './types';

// ------------------------------------------------------------------ field primitives

function Commit({ value, onCommit, mono, placeholder, disabled, multiline, error, ariaLabel }: {
  value: string;
  onCommit: (v: string) => string | null | void;
  mono?: boolean;
  placeholder?: string;
  disabled?: boolean;
  multiline?: boolean;
  error?: boolean;
  ariaLabel?: string;
}) {
  const [draft, setDraft] = useState({ source: value, value });
  const v = draft.source === value ? draft.value : value;
  const [msg, setMsg] = useState<string | null>(null);
  const commit = () => {
    if (v === value) return;
    const r = onCommit(v);
    if (typeof r === 'string') {
      setMsg(r);
      setDraft({ source: value, value });
    } else setMsg(null);
  };
  const props = {
    className: `ms-in${mono ? ' is-mono' : ''}${error || msg ? ' is-error' : ''}`,
    value: v,
    placeholder,
    disabled,
    'aria-label': ariaLabel,
    onChange: (e: React.ChangeEvent<HTMLInputElement | HTMLTextAreaElement>) => setDraft({ source: value, value: e.target.value }),
    onBlur: commit,
    onKeyDown: (e: React.KeyboardEvent<HTMLInputElement | HTMLTextAreaElement>) => {
      if (e.key === 'Enter' && (!multiline || e.metaKey || e.ctrlKey)) {
        e.preventDefault();
        commit();
      } else if (e.key === 'Escape') setDraft({ source: value, value });
    },
  };
  return (
    <div style={{ minWidth: 0 }}>
      {multiline ? <textarea rows={2} {...props} /> : <input {...props} />}
      {msg && <div className="ms-err">{msg}</div>}
    </div>
  );
}

function Fld({ label, children, top }: { label: string; children: ReactNode; top?: boolean }) {
  return (
    <div className={`ms-fld${top ? ' ms-fld--top' : ''}`}>
      <label>{label}</label>
      {children}
    </div>
  );
}

function Block({ title, count, action, children }: { title: string; count?: number; action?: ReactNode; children: ReactNode }) {
  return (
    <section className="ms-block">
      <div className="ms-block__head">
        <span>
          {title}
          {count !== undefined && <small>{count}</small>}
        </span>
        {action}
      </div>
      {children}
    </section>
  );
}

function Head({ kind, title, sub, onDelete }: { kind: string; title: ReactNode; sub?: ReactNode; onDelete?: () => void }) {
  return (
    <div className="ms-insp__head">
      <div className="row-between">
        <span className="ms-insp__kind">{kind}</span>
        {onDelete && (
          <button type="button" className="vts-btn vts-btn--ghost vts-btn--sm" onClick={onDelete} title="Delete (undo with ⌘Z)">
            <Trash2 size={13} aria-hidden="true" /> Delete
          </button>
        )}
      </div>
      <div className="ms-insp__title">{title}</div>
      {sub && <div className="ms-insp__sub">{sub}</div>}
    </div>
  );
}

function Problems({ id }: { id: string }) {
  const { diagnostics } = useModel();
  const mine = diagnostics.filter((d) => d.element === id);
  if (mine.length === 0) return null;
  return (
    <section className="ms-block">
      {mine.map((d, i) => (
        <div key={i} className="ms-note" style={{ color: d.severity === 'error' ? 'var(--st-error)' : d.severity === 'warning' ? 'var(--st-warn)' : undefined }}>
          {d.severity === 'error' ? '●' : d.severity === 'warning' ? '▲' : '·'} {d.message}
          {d.hint && <div className="ms-note">{d.hint}</div>}
        </div>
      ))}
    </section>
  );
}

function useDelete() {
  const m = useModel();
  return (id: string) => {
    const deps = dependents(m.doc, id);
    if (deps.length > 0 && !window.confirm(`Deleting it also removes or breaks:\n\n• ${deps.join('\n• ')}\n\nUndo is available (⌘Z).`)) return;
    const err = m.apply((d) => remove(d, id));
    if (err) window.alert(err);
    else m.select(null);
  };
}

// ------------------------------------------------------------------ model

function ModelInspector() {
  const m = useModel();
  const { doc } = m;
  const root = rootType(doc);
  return (
    <>
      <Head kind="Model" title={doc.label ?? doc.name} sub={doc.name} />
      <Block title="Model">
        <Fld label="Label">
          <Commit value={doc.label ?? ''} disabled={!m.editable} onCommit={(v) => m.apply({ doc: { ...doc, label: v || undefined }, label: 'Rename model' })} />
        </Fld>
        <Fld label="Description" top>
          <Commit multiline value={doc.description ?? ''} disabled={!m.editable} onCommit={(v) => m.apply({ doc: { ...doc, description: v || undefined }, label: 'Edit description' })} />
        </Fld>
        <Fld label="System">
          <button type="button" className="vts-btn vts-btn--sm" onClick={() => root && m.select(root.id)}>{doc.root}</button>
        </Fld>
        <Fld label="Time unit">
          <span className="ms-mono">{doc.time?.unit ?? 's'} · {doc.time?.ticksPerUnit ?? 1000} ticks</span>
        </Fld>
      </Block>
      <Block title="Relationship vocabulary" count={(doc.relationshipTypes ?? []).length}>
        <ul className="ms-rows">
          {(doc.relationshipTypes ?? []).map((r) => (
            <li key={r.id} aria-selected={false}>
              <span className="ms-rows__glyph">→</span>
              <span className="ms-rows__name">{r.name}</span>
              <span className="ms-rows__meta">{r.symmetric ? 'symmetric' : r.inverse ?? ''}</span>
            </li>
          ))}
        </ul>
      </Block>
      <Block title="Advanced">
        <label className="row small">
          <input
            type="checkbox"
            checked={!!doc.allowNondeterminism}
            disabled={!m.editable}
            onChange={(e) => m.apply({ doc: { ...doc, allowNondeterminism: e.target.checked || undefined }, label: 'Toggle nondeterminism' })}
          />
          Allow overlapping transitions (the twin tracks every possibility)
        </label>
      </Block>
    </>
  );
}

// ------------------------------------------------------------------ type

function TypeInspector({ type }: { type: AssetType }) {
  const m = useModel();
  const del = useDelete();
  const { doc } = m;
  const [decl, setDecl] = useState('');
  const [declErr, setDeclErr] = useState<string | null>(null);
  const isRoot = doc.root === type.name;
  const props = effective(doc, type, 'properties');
  const events = effective(doc, type, 'events');
  const actions = effective(doc, type, 'actions');
  const parts = effective(doc, type, 'parts');
  const addDecl = () => {
    const r = parseDeclaration(decl);
    if (r.error || !r.property) {
      setDeclErr(r.error ?? 'Not understood');
      return;
    }
    const err = m.apply((d) => addProperty(d, type.name, r.property!));
    if (err) setDeclErr(err);
    else {
      setDecl('');
      setDeclErr(null);
    }
  };
  const own = (id: string) => !!(type.properties ?? []).concat().find((p) => p.id === id) || !!(type.events ?? []).find((e) => e.id === id) || !!(type.actions ?? []).find((a) => a.id === id);
  return (
    <>
      <Head kind={isRoot ? 'System type' : 'Asset type'} title={type.label ?? type.name} sub={type.name} onDelete={!isRoot && m.editable ? () => del(type.id) : undefined} />
      <Problems id={type.id} />
      <Block title="Type">
        <Fld label="Name">
          <Commit mono value={type.name} disabled={!m.editable} onCommit={(v) => m.apply((d) => renameType(d, type.name, v))} />
        </Fld>
        <Fld label="Label">
          <Commit value={type.label ?? ''} disabled={!m.editable} onCommit={(v) => m.apply((d) => updateType(d, type.name, { label: v || undefined }))} />
        </Fld>
        <Fld label="Description" top>
          <Commit multiline value={type.description ?? ''} disabled={!m.editable} onCommit={(v) => m.apply((d) => updateType(d, type.name, { description: v || undefined }))} />
        </Fld>
        {!isRoot && (
          <Fld label="Based on">
            <select className="ms-sel" value={type.extends ?? ''} disabled={!m.editable} onChange={(e) => m.apply((d) => updateType(d, type.name, { extends: e.target.value || undefined }))}>
              <option value="">— nothing —</option>
              {doc.types.filter((t) => t.name !== type.name).map((t) => <option key={t.id} value={t.name}>{t.name}</option>)}
            </select>
          </Fld>
        )}
      </Block>
      <Block title="Properties" count={props.length}>
        <ul className="ms-rows">
          {props.map((p) => (
            <li key={p.id} onClick={() => m.select(p.id)} aria-selected={m.selection === p.id} title={own(p.id) ? undefined : 'Inherited'}>
              <span className="ms-rows__glyph">{p.kind === 'measured' ? '~' : p.kind === 'fixed' ? '=' : '#'}</span>
              <span className="ms-rows__name">{p.name}</span>
              <span className="ms-rows__meta">
                {p.type}
                {p.unit ? ` ${p.unit}` : ''}
                {p.kind === 'fixed' && p.value !== undefined ? ` = ${p.value}` : ''}
                {p.range ? ` ${p.range.min ?? ''}..${p.range.max ?? ''}` : ''}
              </span>
            </li>
          ))}
        </ul>
        {m.editable && (
          <div>
            <div className="ms-add">
              <input
                className="ms-in is-mono"
                value={decl}
                placeholder="temperature: real degC -40..200"
                onChange={(e) => setDecl(e.target.value)}
                onKeyDown={(e) => e.key === 'Enter' && addDecl()}
                aria-label="New property (one line)"
              />
              <button type="button" className="vts-btn vts-btn--sm" onClick={addDecl}>Add</button>
            </div>
            {declErr ? <div className="ms-err">{declErr}</div> : <div className="ms-note">name: type unit [measured|fixed|per-twin] [min..max] [= value]</div>}
          </div>
        )}
      </Block>
      <Block
        title="Events"
        count={events.length}
        action={m.editable && (
          <span className="row">
            <button type="button" onClick={() => { const n = window.prompt('Event reported by the system (e.g. started)'); if (n) m.apply((d) => addEvent(d, type.name, n)); }}>+ reported</button>
            <button type="button" onClick={() => { const n = window.prompt('Event derived from data (name)', 'overheated'); if (n) m.apply((d) => addEvent(d, type.name, n, 'condition', '')); }}>+ condition</button>
          </span>
        )}
      >
        <ul className="ms-rows">
          {events.map((e) => (
            <li key={e.id} onClick={() => m.select(e.id)} aria-selected={m.selection === e.id}>
              <span className="ms-rows__glyph">▸</span>
              <span className="ms-rows__name">{e.name}</span>
              <span className="ms-rows__meta">{e.origin === 'condition' ? `when ${e.when ?? ''}` : e.meaning ? `means ${e.meaning}` : 'reported'}</span>
            </li>
          ))}
        </ul>
      </Block>
      <Block
        title="Actions"
        count={actions.length}
        action={m.editable && <button type="button" onClick={() => { const n = window.prompt('Action (e.g. start)'); if (n) m.apply((d) => addAction(d, type.name, n)); }}>+ action</button>}
      >
        <ul className="ms-rows">
          {actions.map((a) => (
            <li key={a.id} onClick={() => m.select(a.id)} aria-selected={m.selection === a.id}>
              <span className="ms-rows__glyph">◦</span>
              <span className="ms-rows__name">{a.name}()</span>
              <span className="ms-rows__meta">{a.meaning ?? ''}</span>
            </li>
          ))}
        </ul>
      </Block>
      <Block
        title="Parts"
        count={parts.length}
        action={m.editable && (
          <select
            className="ms-sel"
            style={{ width: 130, height: 24 }}
            value=""
            onChange={(e) => e.target.value && m.apply((d) => addPart(d, type.name, e.target.value))}
            aria-label="Add a part"
          >
            <option value="">+ part of type…</option>
            {doc.types.filter((t) => t.name !== type.name).map((t) => <option key={t.id} value={t.name}>{t.name}</option>)}
          </select>
        )}
      >
        <ul className="ms-rows">
          {parts.map((p) => (
            <li key={p.id} onClick={() => m.select(p.id)} aria-selected={m.selection === p.id}>
              <span className="ms-rows__glyph">{p.external ? '⇢' : '□'}</span>
              <span className="ms-rows__name">{p.name}</span>
              <span className="ms-rows__meta">{p.type}</span>
            </li>
          ))}
        </ul>
        {parts.length > 0 && type.name !== m.scope && (
          <button type="button" className="vts-btn vts-btn--sm" onClick={() => { m.setScope(type.name); m.setView('structure'); }}>Show its parts on the canvas</button>
        )}
      </Block>
      <Block title="Behaviour" count={type.behavior?.states.length}>
        {type.behavior ? (
          <div className="ms-note">
            {type.behavior.states.length} states, {type.behavior.transitions.length} transitions. Initial: {type.behavior.states.find((s) => s.initial)?.name ?? '—'}.
          </div>
        ) : (
          <div className="ms-note">No states yet.</div>
        )}
        <button type="button" className="vts-btn vts-btn--sm" onClick={() => { m.setMachine(type.name); m.setView('behavior'); if (!type.behavior && m.editable) m.apply((d) => addState(d, type.name, 'Idle', { x: 80, y: 120 })); }}>
          {type.behavior ? 'Open behaviour' : 'Add behaviour'}
        </button>
      </Block>
    </>
  );
}

// ------------------------------------------------------------------ members

function PropertyInspector({ type, p }: { type: AssetType; p: Property }) {
  const m = useModel();
  const del = useDelete();
  const set = (patch: Partial<Property>) => m.apply((d) => updateProperty(d, type.name, p.id, patch));
  return (
    <>
      <Head kind={`Property of ${type.name}`} title={p.name} sub={`${p.kind} ${p.type}${p.unit ? ` ${p.unit}` : ''}`} onDelete={m.editable ? () => del(p.id) : undefined} />
      <Problems id={p.id} />
      <Block title="Property">
        <Fld label="Name"><Commit mono value={p.name} disabled={!m.editable} onCommit={(v) => m.apply((d) => renameMember(d, type.name, 'property', p.name, v))} /></Fld>
        <Fld label="Label"><Commit value={p.label ?? ''} disabled={!m.editable} onCommit={(v) => set({ label: v || undefined })} /></Fld>
        <Fld label="Kind">
          <select className="ms-sel" value={p.kind} disabled={!m.editable} onChange={(e) => set({ kind: e.target.value as Property['kind'], value: e.target.value === 'fixed' ? p.value ?? '0' : undefined, period: e.target.value === 'measured' ? p.period ?? '1' : undefined })}>
            <option value="measured">measured — live data</option>
            <option value="fixed">fixed — a design value</option>
            <option value="per-twin">per twin — entered per twin</option>
          </select>
        </Fld>
        <Fld label="Type">
          <select className="ms-sel" value={p.type} disabled={!m.editable} onChange={(e) => set({ type: e.target.value as Property['type'] })}>
            {['real', 'integer', 'boolean', 'enum', 'string'].map((t) => <option key={t} value={t}>{t}</option>)}
          </select>
        </Fld>
        {p.type === 'enum' && (
          <Fld label="Values"><Commit mono value={(p.values ?? []).join(', ')} disabled={!m.editable} onCommit={(v) => set({ values: v.split(',').map((x) => x.trim()).filter(Boolean) })} /></Fld>
        )}
        {(p.type === 'real' || p.type === 'integer') && (
          <>
            <Fld label="Unit"><Commit mono value={p.unit ?? ''} placeholder="degC, bar, %, A, rpm…" disabled={!m.editable} onCommit={(v) => set({ unit: v || undefined })} /></Fld>
            <Fld label="Range">
              <div className="row">
                <Commit mono value={p.range?.min ?? ''} placeholder="min" disabled={!m.editable} onCommit={(v) => set({ range: { ...p.range, min: v || undefined } })} />
                <span className="subtle">..</span>
                <Commit mono value={p.range?.max ?? ''} placeholder="max" disabled={!m.editable} onCommit={(v) => set({ range: { ...p.range, max: v || undefined } })} />
              </div>
            </Fld>
          </>
        )}
        {p.kind === 'fixed' && <Fld label="Value"><Commit mono value={p.value ?? ''} disabled={!m.editable} onCommit={(v) => set({ value: v })} /></Fld>}
        {p.kind === 'measured' && <Fld label="Every"><Commit mono value={p.period ?? ''} placeholder="seconds" disabled={!m.editable} onCommit={(v) => set({ period: v || undefined })} /></Fld>}
        <Fld label="Description" top><Commit multiline value={p.description ?? ''} disabled={!m.editable} onCommit={(v) => set({ description: v || undefined })} /></Fld>
      </Block>
      <BackTo type={type} />
    </>
  );
}

function EventInspector({ type, e }: { type: AssetType; e: ModelEvent }) {
  const m = useModel();
  const del = useDelete();
  const set = (patch: Partial<ModelEvent>) => m.apply((d) => updateEvent(d, type.name, e.id, patch));
  return (
    <>
      <Head kind={`Event of ${type.name}`} title={<>▸ {e.name}</>} sub={e.origin} onDelete={m.editable ? () => del(e.id) : undefined} />
      <Problems id={e.id} />
      <Block title="Event">
        <Fld label="Name"><Commit mono value={e.name} disabled={!m.editable} onCommit={(v) => m.apply((d) => renameMember(d, type.name, 'event', e.name, v))} /></Fld>
        <Fld label="Label"><Commit value={e.label ?? ''} disabled={!m.editable} onCommit={(v) => set({ label: v || undefined })} /></Fld>
        <Fld label="Occurs">
          <select className="ms-sel" value={e.origin} disabled={!m.editable} onChange={(ev) => set({ origin: ev.target.value as ModelEvent['origin'] })}>
            <option value="reported">when the system reports it</option>
            <option value="condition">when a condition becomes true</option>
          </select>
        </Fld>
        {e.origin === 'condition' ? (
          <>
            <Fld label="When" top><Commit multiline mono value={e.when ?? ''} placeholder="temperature > limit" disabled={!m.editable} onCommit={(v) => set({ when: v })} /></Fld>
            <Fld label="Re-arm" top><Commit multiline mono value={e.rearm ?? ''} placeholder={`not (${e.when || 'condition'})`} disabled={!m.editable} onCommit={(v) => set({ rearm: v || undefined })} /></Fld>
          </>
        ) : (
          <Fld label="Means" top><Commit multiline mono value={e.meaning ?? ''} placeholder="optional: what holds when it is reported" disabled={!m.editable} onCommit={(v) => set({ meaning: v || undefined })} /></Fld>
        )}
        <Fld label="Unexpected">
          <select className="ms-sel" value={e.unexpected ?? 'nonconformance'} disabled={!m.editable} onChange={(ev) => set({ unexpected: ev.target.value === 'ignore' ? 'ignore' : undefined })}>
            <option value="nonconformance">is a conformance violation</option>
            <option value="ignore">is ignored</option>
          </select>
        </Fld>
        <Fld label="Description" top><Commit multiline value={e.description ?? ''} disabled={!m.editable} onCommit={(v) => set({ description: v || undefined })} /></Fld>
      </Block>
      <BackTo type={type} />
    </>
  );
}

function ActionInspector({ type, a }: { type: AssetType; a: Action }) {
  const m = useModel();
  const del = useDelete();
  const set = (patch: Partial<Action>) => m.apply((d) => updateAction(d, type.name, a.id, patch));
  return (
    <>
      <Head kind={`Action of ${type.name}`} title={<>◦ {a.name}()</>} onDelete={m.editable ? () => del(a.id) : undefined} />
      <Problems id={a.id} />
      <Block title="Action">
        <Fld label="Name"><Commit mono value={a.name} disabled={!m.editable} onCommit={(v) => m.apply((d) => renameMember(d, type.name, 'action', a.name, v))} /></Fld>
        <Fld label="Label"><Commit value={a.label ?? ''} disabled={!m.editable} onCommit={(v) => set({ label: v || undefined })} /></Fld>
        <Fld label="Requires" top><Commit multiline mono value={a.meaning ?? ''} placeholder="optional precondition, e.g. temperature <= limit" disabled={!m.editable} onCommit={(v) => set({ meaning: v || undefined })} /></Fld>
        <Fld label="Unexpected">
          <select className="ms-sel" value={a.unexpected ?? 'nonconformance'} disabled={!m.editable} onChange={(ev) => set({ unexpected: ev.target.value === 'ignore' ? 'ignore' : undefined })}>
            <option value="nonconformance">is refused</option>
            <option value="ignore">is ignored</option>
          </select>
        </Fld>
        <label className="row small">
          <input type="checkbox" checked={a.requestable !== false} disabled={!m.editable} onChange={(ev) => set({ requestable: ev.target.checked ? undefined : false })} />
          Operators can request it (otherwise only through links)
        </label>
      </Block>
      <BackTo type={type} />
    </>
  );
}

function BackTo({ type }: { type: AssetType }) {
  const m = useModel();
  return (
    <section className="ms-block">
      <button type="button" className="vts-btn vts-btn--ghost vts-btn--sm" onClick={() => m.select(type.id)}>← {type.name}</button>
    </section>
  );
}

// ------------------------------------------------------------------ asset (part)

function PartInspector({ container, part }: { container: AssetType; part: Part }) {
  const m = useModel();
  const del = useDelete();
  const { doc } = m;
  const type = typeByName(doc, part.type);
  const fixed = type ? effective(doc, type, 'properties').filter((p) => p.kind === 'fixed') : [];
  const rels = (container.relationships ?? []).filter((r) => r.from.split('.')[0] === part.name || r.to.split('.')[0] === part.name);
  return (
    <>
      <Head kind={`Asset in ${container.name}`} title={part.label ?? part.name} sub={`${part.name} : ${part.type}`} onDelete={m.editable ? () => del(part.id) : undefined} />
      <Problems id={part.id} />
      <Block title="Asset">
        <Fld label="Name"><Commit mono value={part.name} disabled={!m.editable} onCommit={(v) => m.apply((d) => renamePart(d, container.name, part.name, v))} /></Fld>
        <Fld label="Label"><Commit value={part.label ?? ''} disabled={!m.editable} onCommit={(v) => m.apply((d) => updatePart(d, container.name, part.id, { label: v || undefined }))} /></Fld>
        <Fld label="Type">
          <div className="row">
            <select className="ms-sel" value={part.type} disabled={!m.editable} onChange={(e) => m.apply((d) => updatePart(d, container.name, part.id, { type: e.target.value }))}>
              {doc.types.filter((t) => t.name !== container.name).map((t) => <option key={t.id} value={t.name}>{t.name}</option>)}
            </select>
            {type && <button type="button" className="vts-btn vts-btn--sm" onClick={() => m.select(type.id)}>Edit type</button>}
          </div>
        </Fld>
        <label className="row small">
          <input type="checkbox" checked={!!part.external} disabled={!m.editable} onChange={(e) => m.apply((d) => updatePart(d, container.name, part.id, { external: e.target.checked || undefined }))} />
          External: an asset outside the twin (shared, bound when a twin is created)
        </label>
      </Block>
      {fixed.length > 0 && (
        <Block title="Values" count={fixed.length}>
          {fixed.map((p) => (
            <Fld key={p.id} label={p.name}>
              <Commit
                mono
                value={part.values?.[p.name] ?? ''}
                placeholder={`${p.value ?? ''}${p.unit ? ` ${p.unit}` : ''} (type default)`}
                disabled={!m.editable}
                onCommit={(v) => {
                  const values = { ...(part.values ?? {}) };
                  if (v) values[p.name] = v;
                  else delete values[p.name];
                  return m.apply((d) => updatePart(d, container.name, part.id, { values: Object.keys(values).length ? values : undefined }));
                }}
              />
            </Fld>
          ))}
        </Block>
      )}
      <Block title="Relationships" count={rels.length}>
        <ul className="ms-rows">
          {rels.map((r) => (
            <li key={r.id} onClick={() => m.select(r.id)} aria-selected={m.selection === r.id}>
              <span className="ms-rows__glyph">→</span>
              <span className="ms-rows__name">{r.from} {r.type} {r.to}</span>
              <span />
            </li>
          ))}
        </ul>
        {rels.length === 0 && <div className="ms-note">Drag from the asset's corner handle to another asset to relate them.</div>}
      </Block>
    </>
  );
}

function RelationshipInspector({ container, r }: { container: AssetType; r: Relationship }) {
  const m = useModel();
  const del = useDelete();
  const set = (patch: Partial<Relationship>) => m.apply((d) => updateRelationship(d, container.name, r.id, patch));
  const ends = ['self', ...effective(m.doc, container, 'parts').map((p) => p.name)];
  return (
    <>
      <Head kind={`Relationship in ${container.name}`} title={<>{r.from} <span className="subtle">{r.type}</span> {r.to}</>} onDelete={m.editable ? () => del(r.id) : undefined} />
      <Problems id={r.id} />
      <Block title="Relationship">
        <Fld label="Type">
          <select className="ms-sel" value={r.type} disabled={!m.editable} onChange={(e) => set({ type: e.target.value })}>
            {(m.doc.relationshipTypes ?? []).map((t) => <option key={t.id} value={t.name}>{t.label ?? t.name}</option>)}
          </select>
        </Fld>
        <Fld label="From">
          <select className="ms-sel" value={r.from} disabled={!m.editable} onChange={(e) => set({ from: e.target.value })}>
            {ends.map((n) => <option key={n} value={n}>{n}</option>)}
          </select>
        </Fld>
        <Fld label="To">
          <select className="ms-sel" value={r.to} disabled={!m.editable} onChange={(e) => set({ to: e.target.value })}>
            {ends.map((n) => <option key={n} value={n}>{n}</option>)}
          </select>
        </Fld>
        <button type="button" className="vts-btn vts-btn--sm" disabled={!m.editable} onClick={() => set({ from: r.to, to: r.from })}>
          <ArrowLeftRight size={13} aria-hidden="true" /> Swap direction
        </button>
      </Block>
    </>
  );
}

function LinkInspector({ container, l }: { container: AssetType; l: Link }) {
  const m = useModel();
  const del = useDelete();
  return (
    <>
      <Head kind={`Link in ${container.name}`} title={<>▸ {l.from} → ◦ {l.to}()</>} onDelete={m.editable ? () => del(l.id) : undefined} />
      <Problems id={l.id} />
      <Block title="What it means">
        <div className="ms-note">
          Whenever <span className="ms-mono">{l.from}</span> occurs, <span className="ms-mono">{l.to}</span> happens in the same step. The two assets move together;
          if the action is not possible at that moment, the event is not either (unless the action is declared “ignored when unexpected”).
        </div>
      </Block>
    </>
  );
}

// ------------------------------------------------------------------ behaviour

function atomsToFields(atoms: TimeAtom[] | undefined) {
  const lo = atoms?.find((a) => a.op === '>=' || a.op === '>');
  const hi = atoms?.find((a) => a.op === '<=' || a.op === '<');
  return { lo: lo ? String(lo.bound) : '', hi: hi ? String(hi.bound) : '' };
}

function StateInspector({ type, s }: { type: AssetType; s: State }) {
  const m = useModel();
  const del = useDelete();
  const b = type.behavior!;
  const timer = b.timers?.[0]?.name ?? 't';
  const unit = m.doc.time?.unit ?? 's';
  const limit = s.limits?.find((a) => a.timer === timer && (a.op === '<=' || a.op === '<'));
  const set = (patch: Partial<State>) => m.apply((d) => updateState(d, type.name, s.id, patch));
  const outgoing = b.transitions.filter((t) => t.from.includes(s.name) || (b.groups ?? []).some((g) => t.from.includes(g.name) && g.states.includes(s.name)));
  return (
    <>
      <Head kind={`State of ${type.name}`} title={s.name} sub={s.initial ? 'initial state' : undefined} onDelete={m.editable ? () => del(s.id) : undefined} />
      <Problems id={s.id} />
      <Block title="State">
        <Fld label="Name"><Commit mono value={s.name} disabled={!m.editable} onCommit={(v) => m.apply((d) => renameState(d, type.name, s.name, v))} /></Fld>
        <Fld label="Label"><Commit value={s.label ?? ''} disabled={!m.editable} onCommit={(v) => set({ label: v || undefined })} /></Fld>
        <label className="row small">
          <input type="checkbox" checked={!!s.initial} disabled={!m.editable || !!s.initial} onChange={() => set({ initial: true })} />
          Initial state
        </label>
        <Fld label="Means" top><Commit multiline mono value={s.meaning ?? ''} placeholder="optional, e.g. running and temperature <= limit" disabled={!m.editable} onCommit={(v) => set({ meaning: v || undefined })} /></Fld>
        <Fld label="At most">
          <div className="row">
            <Commit
              mono
              value={limit ? String(limit.bound) : ''}
              placeholder="no limit"
              disabled={!m.editable}
              onCommit={(v) => {
                const others = (s.limits ?? []).filter((a) => a !== limit);
                if (!v.trim()) return m.apply((d) => updateState(d, type.name, s.id, { limits: others.length ? others : undefined }));
                const n = Number(v);
                if (!Number.isInteger(n) || n < 0) return 'A whole number of time units';
                return m.apply((d) => updateState(d, type.name, s.id, { limits: [...others, { timer, op: '<=', bound: n }] }));
              }}
            />
            <span className="subtle">{unit} in this state</span>
          </div>
        </Fld>
        <Fld label="Tone">
          <select className="ms-sel" value={s.tone ?? 'normal'} disabled={!m.editable} onChange={(e) => set({ tone: e.target.value === 'normal' ? undefined : (e.target.value as State['tone']) })}>
            <option value="normal">normal</option>
            <option value="warning">warning</option>
            <option value="critical">critical</option>
          </select>
        </Fld>
        <Fld label="Description" top><Commit multiline value={s.description ?? ''} disabled={!m.editable} onCommit={(v) => set({ description: v || undefined })} /></Fld>
      </Block>
      <Block title="Leaves on" count={outgoing.length}>
        <ul className="ms-rows">
          {outgoing.map((t) => (
            <li key={t.id} onClick={() => m.select(t.id)} aria-selected={m.selection === t.id}>
              <span className="ms-rows__glyph">→</span>
              <span className="ms-rows__name">{triggerText(t.trigger, unit)}</span>
              <span className="ms-rows__meta">{t.to}</span>
            </li>
          ))}
        </ul>
      </Block>
    </>
  );
}

function triggerKind(t: Trigger): 'event' | 'action' | 'timeout' | 'internal' {
  return 'event' in t ? 'event' : 'action' in t ? 'action' : 'timeout' in t ? 'timeout' : 'internal';
}

function TransitionInspector({ type, t }: { type: AssetType; t: Transition }) {
  const m = useModel();
  const del = useDelete();
  const { doc } = m;
  const b = type.behavior!;
  const timer = b.timers?.[0]?.name ?? 't';
  const unit = doc.time?.unit ?? 's';
  const set = (patch: Partial<Transition>) => m.apply((d) => updateTransition(d, type.name, t.id, patch));
  const kind = triggerKind(t.trigger);
  const events = [
    ...effective(doc, type, 'events').map((e) => e.name),
    ...effective(doc, type, 'parts').flatMap((p) => {
      const pt = typeByName(doc, p.type);
      return pt ? effective(doc, pt, 'events').map((e) => `${p.name}.${e.name}`) : [];
    }),
  ];
  const actions = effective(doc, type, 'actions').map((a) => a.name);
  const w = atomsToFields(t.window);
  const setWindow = (lo: string, hi: string): string | null => {
    const atoms: TimeAtom[] = [];
    for (const [v, op] of [[lo, '>='], [hi, '<=']] as const) {
      if (!v.trim()) continue;
      const n = Number(v);
      if (!Number.isInteger(n) || n < 0) return 'A whole number of time units';
      atoms.push({ timer, op, bound: n });
    }
    return m.apply((d) => updateTransition(d, type.name, t.id, { window: atoms.length ? atoms : undefined }));
  };
  const sources = [...b.states.map((s) => s.name), ...(b.groups ?? []).map((g) => g.name)];
  return (
    <>
      <Head kind={`Transition of ${type.name}`} title={<>{t.from.join(', ')} → {t.to}</>} sub={`${triggerText(t.trigger, unit)}${t.window ? ` · ${windowText(t.window)}` : ''}`} onDelete={m.editable ? () => del(t.id) : undefined} />
      <Problems id={t.id} />
      <Block title="Transition">
        <Fld label="From" top>
          <div className="ms-chips">
            {sources.map((n) => (
              <button
                key={n}
                type="button"
                className="ms-chip"
                aria-pressed={t.from.includes(n)}
                disabled={!m.editable}
                onClick={() => {
                  const from = t.from.includes(n) ? t.from.filter((x) => x !== n) : [...t.from, n];
                  if (from.length) set({ from });
                }}
              >
                {n}
              </button>
            ))}
          </div>
        </Fld>
        <Fld label="To">
          <select className="ms-sel" value={t.to} disabled={!m.editable} onChange={(e) => set({ to: e.target.value })}>
            {b.states.map((s) => <option key={s.id} value={s.name}>{s.name}</option>)}
          </select>
        </Fld>
        <Fld label="Trigger">
          <select
            className="ms-sel"
            value={kind}
            disabled={!m.editable}
            onChange={(e) => {
              const k = e.target.value;
              set({ trigger: k === 'event' ? { event: events[0] ?? 'event' } : k === 'action' ? { action: actions[0] ?? 'action' } : k === 'timeout' ? { timeout: 10 } : { internal: true } });
            }}
          >
            <option value="event">▸ an event</option>
            <option value="action">◦ an action</option>
            <option value="timeout">⧗ a timeout</option>
            <option value="internal">τ internal (advanced)</option>
          </select>
        </Fld>
        {kind === 'event' && (
          <Fld label="Event">
            <select className="ms-sel is-mono" value={(t.trigger as { event: string }).event} disabled={!m.editable} onChange={(e) => set({ trigger: { event: e.target.value } })}>
              {[...new Set([(t.trigger as { event: string }).event, ...events])].map((n) => <option key={n} value={n}>{n}</option>)}
            </select>
          </Fld>
        )}
        {kind === 'action' && (
          <Fld label="Action">
            <select className="ms-sel is-mono" value={(t.trigger as { action: string }).action} disabled={!m.editable} onChange={(e) => set({ trigger: { action: e.target.value } })}>
              {[...new Set([(t.trigger as { action: string }).action, ...actions])].map((n) => <option key={n} value={n}>{n}()</option>)}
            </select>
          </Fld>
        )}
        {kind === 'timeout' && (
          <Fld label="After">
            <div className="row">
              <Commit
                mono
                value={String((t.trigger as { timeout: number | string }).timeout)}
                disabled={!m.editable}
                onCommit={(v) => {
                  const n = Number(v);
                  if (!Number.isInteger(n) || n < 0) return 'A whole number of time units';
                  return m.apply((d) => updateTransition(d, type.name, t.id, { trigger: { timeout: n } }));
                }}
              />
              <span className="subtle">{unit} in the state</span>
            </div>
          </Fld>
        )}
        {kind !== 'timeout' && (
          <Fld label="Only if">
            <div className="row">
              <Commit mono value={w.lo} placeholder="not before" disabled={!m.editable} onCommit={(v) => setWindow(v, w.hi)} ariaLabel="Not before" />
              <Commit mono value={w.hi} placeholder="not after" disabled={!m.editable} onCommit={(v) => setWindow(w.lo, v)} ariaLabel="Not after" />
              <span className="subtle">{unit}</span>
            </div>
          </Fld>
        )}
        <div className="ms-note">Conditions on measured values are written as events (“when temperature &gt; limit”) or state meanings, not here.</div>
        <Fld label="Description" top><Commit multiline value={t.description ?? ''} disabled={!m.editable} onCommit={(v) => set({ description: v || undefined })} /></Fld>
      </Block>
    </>
  );
}

function GroupInspector({ type, g }: { type: AssetType; g: Group }) {
  const m = useModel();
  const del = useDelete();
  const b = type.behavior!;
  return (
    <>
      <Head kind={`Group of ${type.name}`} title={g.label ?? g.name} sub={g.name} onDelete={m.editable ? () => del(g.id) : undefined} />
      <Block title="Members">
        <div className="ms-chips">
          {b.states.map((s) => (
            <button
              key={s.id}
              type="button"
              className="ms-chip"
              aria-pressed={g.states.includes(s.name)}
              disabled={!m.editable}
              onClick={() =>
                m.apply((d) => {
                  const dd = structuredClone(d);
                  const gg = typeByName(dd, type.name)!.behavior!.groups!.find((x) => x.id === g.id)!;
                  gg.states = gg.states.includes(s.name) ? gg.states.filter((x) => x !== s.name) : [...gg.states, s.name];
                  return { doc: dd, label: 'Edit group', select: g.id };
                })
              }
            >
              {s.name}
            </button>
          ))}
        </div>
        <div className="ms-note">A transition from the group leaves each member.</div>
      </Block>
    </>
  );
}

// ------------------------------------------------------------------ dispatcher

export function Inspector() {
  const m = useModel();
  const loc = locate(m.doc, m.selection);
  if (!loc) return <ModelInspector />;
  const t = loc.type;
  switch (loc.kind) {
    case 'type': return <TypeInspector type={t} />;
    case 'part': return <PartInspector container={t} part={loc.element as Part} />;
    case 'property': return <PropertyInspector type={t} p={loc.element as Property} />;
    case 'event': return <EventInspector type={t} e={loc.element as ModelEvent} />;
    case 'action': return <ActionInspector type={t} a={loc.element as Action} />;
    case 'relationship': return <RelationshipInspector container={t} r={loc.element as Relationship} />;
    case 'link': return <LinkInspector container={t} l={loc.element as Link} />;
    case 'state': return <StateInspector type={t} s={loc.element as State} />;
    case 'transition': return <TransitionInspector type={t} t={loc.element as Transition} />;
    case 'group': return <GroupInspector type={t} g={loc.element as Group} />;
    default: return <TypeInspector type={t} />;
  }
}

export type { TwinModel };
