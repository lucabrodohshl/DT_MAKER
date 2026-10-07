/**
 * Shared building blocks of the Blueprint editors: page frame with guided help, panes,
 * form fields that never coerce silently (identifiers are checked as typed, numbers stay
 * decimal strings where the document is canonical), selectable lists with keyboard
 * navigation, inline findings, a destructive-action dialog that lists dependencies, and
 * the expert JSON view of a section.
 */
import clsx from 'clsx';
import { AlertOctagon, AlertTriangle, Braces, Info, Plus, Search, Trash2 } from 'lucide-react';
import { useEffect, useId, useMemo, useRef, useState, type KeyboardEvent, type ReactNode } from 'react';
import { Link } from 'react-router-dom';
import type { SectionFinding } from '@/api/types';
import { Button, Dialog, EmptyState } from '@/design';
import { CodeEditor } from '@/editor/CodeEditor';
import { useEditor } from './editor';
import { useWorkspace } from './workspace';

export const IDENT = /^[A-Za-z_][A-Za-z0-9_]*$/;
export const SLUG = /^[a-z0-9][a-z0-9-]*$/;
export const DECIMAL = /^-?\d+(\.\d+)?$/;

// ------------------------------------------------------------------ page frame
export function EdPage({
  title,
  description,
  actions,
  guide,
  children,
  wide,
  fill,
}: {
  title: ReactNode;
  description?: ReactNode;
  actions?: ReactNode;
  /** Shown in guided mode only. */
  guide?: ReactNode;
  children: ReactNode;
  wide?: boolean;
  /** The last child grows to the bottom of the viewport (canvases). */
  fill?: boolean;
}) {
  const ws = useWorkspace();
  return (
    <div className={clsx('vts-ed-page', fill && 'vts-ed-page--fill')} style={wide ? { maxWidth: 'none' } : undefined}>
      <div className="vts-ed-head">
        <div style={{ minWidth: 0 }}>
          <h2>{title}</h2>
          {description && <p>{description}</p>}
        </div>
        {actions && <div className="row-wrap">{actions}</div>}
      </div>
      {guide && !ws.expert && (
        <div className="vts-ed-guide" role="note">
          <Info size={16} aria-hidden="true" />
          <div>{guide}</div>
        </div>
      )}
      {children}
    </div>
  );
}

export function Pane({
  title,
  actions,
  children,
  flush,
  className,
  style,
}: {
  title?: ReactNode;
  actions?: ReactNode;
  children: ReactNode;
  flush?: boolean;
  className?: string;
  style?: React.CSSProperties;
}) {
  return (
    <section className={clsx('vts-ed-pane', className)} style={style}>
      {(title || actions) && (
        <div className="vts-ed-pane__head">
          {title && <h3>{title}</h3>}
          <div className="grow" />
          {actions}
        </div>
      )}
      <div className={clsx('vts-ed-pane__body', flush && 'vts-ed-pane__body--flush')}>{children}</div>
    </section>
  );
}

export function InspectorSection({ title, children }: { title: string; children: ReactNode }) {
  return (
    <div className="vts-insp-section">
      <h4>{title}</h4>
      {children}
    </div>
  );
}

// ------------------------------------------------------------------ fields
interface FieldBase {
  label: ReactNode;
  hint?: ReactNode;
  error?: string | null;
  disabled?: boolean;
}

export function TextField({
  label,
  value,
  onChange,
  hint,
  error,
  placeholder,
  mono,
  disabled,
  pattern,
  patternMessage,
  autoFocus,
  list,
}: FieldBase & {
  value: string;
  onChange: (v: string) => void;
  placeholder?: string;
  mono?: boolean;
  pattern?: RegExp;
  patternMessage?: string;
  autoFocus?: boolean;
  list?: string;
}) {
  const id = useId();
  const e = useEditor();
  const bad = pattern && value !== '' && !pattern.test(value) ? patternMessage ?? 'Invalid value' : null;
  const err = error ?? bad;
  return (
    <label className="vts-f" htmlFor={id}>
      <span>{label}</span>
      <input
        id={id}
        className={clsx('vts-input', mono && 'mono', err && 'is-invalid')}
        value={value}
        placeholder={placeholder}
        disabled={disabled || !e.editable}
        aria-invalid={!!err || undefined}
        aria-describedby={err ? `${id}-err` : undefined}
        onChange={(ev) => onChange(ev.target.value)}
        autoFocus={autoFocus}
        list={list}
      />
      {err ? <span className="vts-f__error" id={`${id}-err`}>{err}</span> : hint ? <span className="vts-f__hint">{hint}</span> : null}
    </label>
  );
}

export function TextArea({ label, value, onChange, hint, rows = 3, mono, disabled, placeholder }: FieldBase & { value: string; onChange: (v: string) => void; rows?: number; mono?: boolean; placeholder?: string }) {
  const id = useId();
  const e = useEditor();
  return (
    <label className="vts-f" htmlFor={id}>
      <span>{label}</span>
      <textarea id={id} className={clsx('vts-textarea', mono && 'mono')} rows={rows} value={value} placeholder={placeholder} disabled={disabled || !e.editable} onChange={(ev) => onChange(ev.target.value)} />
      {hint && <span className="vts-f__hint">{hint}</span>}
    </label>
  );
}

export function SelectField<T extends string>({
  label,
  value,
  onChange,
  options,
  hint,
  error,
  disabled,
  allowEmpty,
}: FieldBase & { value: T | ''; onChange: (v: T) => void; options: { value: T; label: string }[]; allowEmpty?: string }) {
  const id = useId();
  const e = useEditor();
  return (
    <label className="vts-f" htmlFor={id}>
      <span>{label}</span>
      <select id={id} className={clsx('vts-select', error && 'is-invalid')} value={value} disabled={disabled || !e.editable} onChange={(ev) => onChange(ev.target.value as T)}>
        {allowEmpty !== undefined && <option value="">{allowEmpty}</option>}
        {options.map((o) => (
          <option key={o.value} value={o.value}>{o.label}</option>
        ))}
      </select>
      {error ? <span className="vts-f__error">{error}</span> : hint ? <span className="vts-f__hint">{hint}</span> : null}
    </label>
  );
}

/** Integer input (canonical documents never hold floats). */
export function IntField({ label, value, onChange, hint, min, max, disabled, unit }: FieldBase & { value: number | undefined; onChange: (v: number | undefined) => void; min?: number; max?: number; unit?: string }) {
  const id = useId();
  const e = useEditor();
  const [text, setText] = useState(value === undefined ? '' : String(value));
  const [prev, setPrev] = useState(value);
  if (prev !== value) {
    setPrev(value);
    if (!(text !== '' && /^-?\d+$/.test(text) && Number(text) === value)) setText(value === undefined ? '' : String(value));
  }
  const bad = text !== '' && !/^-?\d+$/.test(text) ? 'Whole number expected' : null;
  return (
    <label className="vts-f" htmlFor={id}>
      <span>{label}{unit ? <span className="subtle"> ({unit})</span> : null}</span>
      <input
        id={id}
        className={clsx('vts-input num', bad && 'is-invalid')}
        inputMode="numeric"
        value={text}
        disabled={disabled || !e.editable}
        aria-invalid={!!bad || undefined}
        onChange={(ev) => {
          const t = ev.target.value;
          setText(t);
          if (t === '') onChange(undefined);
          else if (/^-?\d+$/.test(t)) {
            let n = Number(t);
            if (min !== undefined) n = Math.max(min, n);
            if (max !== undefined) n = Math.min(max, n);
            onChange(n);
          }
        }}
      />
      {bad ? <span className="vts-f__error">{bad}</span> : hint ? <span className="vts-f__hint">{hint}</span> : null}
    </label>
  );
}

/** Decimal number kept as a string ("12.5"), as canonical documents require. */
export function DecimalField({ label, value, onChange, hint, disabled, unit, placeholder }: FieldBase & { value: string | undefined; onChange: (v: string | undefined) => void; unit?: string; placeholder?: string }) {
  const id = useId();
  const e = useEditor();
  const v = value ?? '';
  const bad = v !== '' && !DECIMAL.test(v) ? 'A number such as 12 or 12.5' : null;
  return (
    <label className="vts-f" htmlFor={id}>
      <span>{label}{unit ? <span className="subtle"> ({unit})</span> : null}</span>
      <input id={id} className={clsx('vts-input num', bad && 'is-invalid')} inputMode="decimal" value={v} placeholder={placeholder} disabled={disabled || !e.editable} aria-invalid={!!bad || undefined} onChange={(ev) => onChange(ev.target.value === '' ? undefined : ev.target.value)} />
      {bad ? <span className="vts-f__error">{bad}</span> : hint ? <span className="vts-f__hint">{hint}</span> : null}
    </label>
  );
}

export function CheckField({ label, checked, onChange, disabled, hint }: { label: ReactNode; checked: boolean; onChange: (v: boolean) => void; disabled?: boolean; hint?: ReactNode }) {
  const e = useEditor();
  return (
    <div className="vts-f">
      <label className="vts-check">
        <input type="checkbox" checked={checked} disabled={disabled || !e.editable} onChange={(ev) => onChange(ev.target.checked)} />
        {label}
      </label>
      {hint && <span className="vts-f__hint">{hint}</span>}
    </div>
  );
}

/** Free-form string list (tags, values). */
export function TagsField({ label, value, onChange, hint, placeholder }: FieldBase & { value: string[]; onChange: (v: string[]) => void; placeholder?: string }) {
  const [text, setText] = useState(value.join(', '));
  const [prev, setPrev] = useState(value);
  if (prev !== value) {
    setPrev(value);
    setText(value.join(', '));
  }
  const e = useEditor();
  return (
    <label className="vts-f">
      <span>{label}</span>
      <input
        className="vts-input"
        value={text}
        placeholder={placeholder}
        disabled={!e.editable}
        onChange={(ev) => setText(ev.target.value)}
        onBlur={() => onChange(text.split(',').map((t) => t.trim()).filter(Boolean))}
      />
      {hint && <span className="vts-f__hint">{hint}</span>}
    </label>
  );
}

/** Key/value string map editor (static properties). */
export function KeyValueEditor({ value, onChange, keyPlaceholder = 'key', valuePlaceholder = 'value' }: { value: Record<string, string>; onChange: (v: Record<string, string>) => void; keyPlaceholder?: string; valuePlaceholder?: string }) {
  const e = useEditor();
  const entries = Object.entries(value);
  const set = (i: number, k: string, v: string) => {
    const next = entries.map((x, j) => (j === i ? [k, v] : x));
    onChange(Object.fromEntries(next));
  };
  return (
    <div className="stack-sm">
      {entries.map(([k, v], i) => (
        <div key={i} className="row">
          <input className="vts-input mono" style={{ width: '40%' }} value={k} placeholder={keyPlaceholder} disabled={!e.editable} aria-label="Property name" onChange={(ev) => set(i, ev.target.value, v)} />
          <input className="vts-input grow" value={v} placeholder={valuePlaceholder} disabled={!e.editable} aria-label={`Value of ${k}`} onChange={(ev) => set(i, k, ev.target.value)} />
          <Button size="sm" variant="ghost" iconOnly icon={<Trash2 size={13} />} disabled={!e.editable} onClick={() => onChange(Object.fromEntries(entries.filter((_, j) => j !== i)))}>
            Remove property
          </Button>
        </div>
      ))}
      {e.editable && (
        <Button size="sm" variant="ghost" icon={<Plus size={13} />} onClick={() => onChange({ ...value, [entries.length === 0 ? 'property' : `property_${entries.length + 1}`]: '' })}>
          Add property
        </Button>
      )}
    </div>
  );
}

// ------------------------------------------------------------------ lists
export interface ListEntry {
  id: string;
  label: ReactNode;
  meta?: ReactNode;
  depth?: number;
  icon?: ReactNode;
  search?: string;
  tone?: 'error' | 'warning';
}

/** Searchable, keyboard-navigable selection list (Up/Down, Home/End, Delete). */
export function SelectList({
  items,
  selected,
  onSelect,
  label,
  onDelete,
  empty,
  filterPlaceholder = 'Filter…',
}: {
  items: ListEntry[];
  selected: string | null;
  onSelect: (id: string) => void;
  label: string;
  onDelete?: (id: string) => void;
  empty?: ReactNode;
  filterPlaceholder?: string;
}) {
  const [q, setQ] = useState('');
  const ref = useRef<HTMLUListElement>(null);
  const shown = useMemo(
    () => items.filter((i) => !q || (i.search ?? `${i.id} ${typeof i.label === 'string' ? i.label : ''}`).toLowerCase().includes(q.toLowerCase())),
    [items, q],
  );
  useEffect(() => {
    if (!selected || !ref.current) return;
    ref.current.querySelector<HTMLElement>(`[data-id="${CSS.escape(selected)}"]`)?.scrollIntoView({ block: 'nearest' });
  }, [selected]);
  const onKey = (ev: KeyboardEvent) => {
    const i = shown.findIndex((x) => x.id === selected);
    if (ev.key === 'ArrowDown') {
      ev.preventDefault();
      const n = shown[Math.min(i + 1, shown.length - 1)];
      if (n) onSelect(n.id);
    } else if (ev.key === 'ArrowUp') {
      ev.preventDefault();
      const n = shown[Math.max(i - 1, 0)];
      if (n) onSelect(n.id);
    } else if (ev.key === 'Home' && shown[0]) {
      ev.preventDefault();
      onSelect(shown[0].id);
    } else if (ev.key === 'End' && shown.length) {
      ev.preventDefault();
      onSelect(shown[shown.length - 1]!.id);
    } else if ((ev.key === 'Delete' || ev.key === 'Backspace') && selected && onDelete) {
      ev.preventDefault();
      onDelete(selected);
    }
  };
  return (
    <div className="stack-sm" style={{ gap: 4 }}>
      {items.length > 8 && (
        <label className="row" style={{ padding: '6px 8px 0' }}>
          <Search size={13} aria-hidden="true" className="subtle" />
          <input className="vts-input grow" style={{ minHeight: 28 }} value={q} onChange={(ev) => setQ(ev.target.value)} placeholder={filterPlaceholder} aria-label={`Filter ${label}`} />
        </label>
      )}
      {items.length === 0 ? (
        <div style={{ padding: 8 }}>{empty ?? <EmptyState compact title="Nothing here yet" />}</div>
      ) : (
        <ul ref={ref} className="vts-ed-list" role="listbox" aria-label={label} tabIndex={0} onKeyDown={onKey} aria-activedescendant={selected ? `opt-${selected}` : undefined}>
          {shown.map((i) => (
            <li key={i.id} role="option" id={`opt-${i.id}`} aria-selected={i.id === selected} data-id={i.id}>
              <button
                type="button"
                tabIndex={-1}
                className={clsx('vts-ed-item', i.depth !== undefined && 'vts-ed-item--nested', i.id === selected && 'is-selected')}
                style={i.depth !== undefined ? ({ '--depth': i.depth } as React.CSSProperties) : undefined}
                onClick={() => onSelect(i.id)}
              >
                {i.icon}
                <span className="truncate">{i.label}</span>
                {i.tone === 'error' && <AlertOctagon size={13} aria-label="has errors" style={{ color: 'var(--crit)', flex: 'none' }} />}
                {i.tone === 'warning' && <AlertTriangle size={13} aria-label="has warnings" style={{ color: 'var(--warn)', flex: 'none' }} />}
                {i.meta && <span className="vts-ed-item__meta">{i.meta}</span>}
              </button>
            </li>
          ))}
        </ul>
      )}
    </div>
  );
}

// ------------------------------------------------------------------ findings
export function FindingsInline({ findings, empty }: { findings: SectionFinding[]; empty?: ReactNode }) {
  if (findings.length === 0) return <>{empty ?? null}</>;
  return (
    <ul className="vts-findings" aria-label="Validation findings">
      {findings.map((f, i) => (
        <li key={i}>
          {f.severity === 'error' ? <AlertOctagon size={13} style={{ color: 'var(--crit)' }} aria-label="Error" /> : <AlertTriangle size={13} style={{ color: 'var(--warn)' }} aria-label="Warning" />}
          <span>
            {f.message} <span className="mono subtle">{f.code}</span>
          </span>
        </li>
      ))}
    </ul>
  );
}

// ------------------------------------------------------------------ destructive actions
export interface Dependency {
  what: string;
  where: string;
  route?: string;
}

/** Confirmation of a destructive action, listing everything that refers to the target. */
export function ConfirmDelete({
  open,
  onOpenChange,
  title,
  dependencies,
  onConfirm,
  consequence,
}: {
  open: boolean;
  onOpenChange: (o: boolean) => void;
  title: string;
  dependencies: Dependency[];
  onConfirm: () => void;
  consequence?: string;
}) {
  const e = useEditor();
  return (
    <Dialog
      open={open}
      onOpenChange={onOpenChange}
      title={title}
      description={dependencies.length > 0 ? `${dependencies.length} element(s) refer to it.` : 'Nothing refers to it.'}
      footer={
        <>
          <Button onClick={() => onOpenChange(false)}>Cancel</Button>
          <Button
            variant="danger"
            icon={<Trash2 size={14} />}
            onClick={() => {
              onConfirm();
              onOpenChange(false);
            }}
          >
            Delete{dependencies.length > 0 ? ' anyway' : ''}
          </Button>
        </>
      }
    >
      <div className="stack-sm">
        {dependencies.length > 0 && (
          <>
            <p className="small">These references will become invalid and be reported by validation until you fix them:</p>
            <ul className="vts-list small">
              {dependencies.map((d, i) => (
                <li key={i}>
                  <strong>{d.what}</strong> <span className="muted">in {d.where}</span>
                  {d.route && (
                    <>
                      {' '}
                      <Link to={`/studio/blueprints/${encodeURIComponent(e.id)}/v/${e.version}/${d.route}`} onClick={() => onOpenChange(false)}>
                        open
                      </Link>
                    </>
                  )}
                </li>
              ))}
            </ul>
          </>
        )}
        {consequence && <p className="small muted">{consequence}</p>}
        <p className="xsmall subtle">You can undo this with Ctrl+Z while the draft is open.</p>
      </div>
    </Dialog>
  );
}

// ------------------------------------------------------------------ expert JSON view
/** The section as JSON (expert mode): edit and apply; invalid JSON is never applied. */
export function JsonSectionView<T>({ value, onApply, label }: { value: T; onApply: (v: T) => void; label: string }) {
  const e = useEditor();
  const text = useMemo(() => JSON.stringify(value, null, 2), [value]);
  const [draft, setDraft] = useState(text);
  const [error, setError] = useState<string | null>(null);
  const [nonce, setNonce] = useState(0);
  const [prev, setPrev] = useState(text);
  if (prev !== text) {
    setPrev(text);
    setDraft(text);
    setNonce((n) => n + 1);
  }
  return (
    <div className="stack-sm">
      <CodeEditor value={text} resetKey={`${label}-${nonce}`} language="json" ariaLabel={`${label} as JSON`} onChange={setDraft} readOnly={!e.editable} height={420} />
      {error && <p className="vts-f__error">{error}</p>}
      {e.editable && (
        <div className="row">
          <Button
            size="sm"
            variant="primary"
            icon={<Braces size={13} />}
            disabled={draft === text}
            onClick={() => {
              try {
                const parsed = JSON.parse(draft) as T;
                setError(null);
                onApply(parsed);
              } catch (err) {
                setError(`Not valid JSON: ${err instanceof Error ? err.message : String(err)}`);
              }
            }}
          >
            Apply JSON
          </Button>
          <Button size="sm" variant="ghost" disabled={draft === text} onClick={() => { setDraft(text); setNonce((n) => n + 1); }}>
            Revert
          </Button>
          <span className="xsmall subtle">Applied changes are validated like any other edit.</span>
        </div>
      )}
    </div>
  );
}

/** "Used by": a compact list of references with links. */
export function UsedBy({ deps }: { deps: Dependency[] }) {
  const e = useEditor();
  if (deps.length === 0) return <p className="xsmall subtle">Not referenced elsewhere.</p>;
  return (
    <ul className="vts-findings">
      {deps.map((d, i) => (
        <li key={i}>
          <span>
            {d.route ? <Link to={`/studio/blueprints/${encodeURIComponent(e.id)}/v/${e.version}/${d.route}`}>{d.what}</Link> : <strong>{d.what}</strong>}{' '}
            <span className="subtle">({d.where})</span>
          </span>
        </li>
      ))}
    </ul>
  );
}
