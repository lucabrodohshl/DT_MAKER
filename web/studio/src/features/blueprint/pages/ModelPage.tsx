/**
 * Behavior → Physical System View (V_P) / Digital Twin View (V_D): the timed-automaton
 * editor. One canonical model (twin-ta/1) is edited graphically or as TwinTA text, or
 * imported from UPPAAL; structural diagnostics, constraint parsing and text conversion are
 * the backend's (authoring engine). Unsupported UPPAAL constructs are reported, never
 * approximated. Saves go into the draft's editable artefact version.
 */
import * as DropdownMenu from '@radix-ui/react-dropdown-menu';
import { AlertOctagon, AlertTriangle, Download, FileUp, LayoutGrid, Plus, Search, Trash2, Workflow } from 'lucide-react';
import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import { Link } from 'react-router-dom';
import { useQueryClient } from '@tanstack/react-query';
import { api } from '@/api/client';
import { blueprintApi, bpKeys, useBlueprintModel } from '@/api/blueprints';
import type { TaAtom, TaDiagnostic, TaEdge, TaLayout, TaModel } from '@/api/types';
import { Button, Callout, Dialog, EmptyState, ErrorBlock, HashChip, Segmented, StatusBadge, Tabs, downloadText } from '@/design';
import { CodeEditor } from '@/editor/CodeEditor';
import { versionRoute } from '@/features/common/links';
import { useEditor } from '../editor';
import { usedByLabel, usedByState } from '../refs';
import { CheckField, ConfirmDelete, EdPage, InspectorSection, Pane, TextArea, TextField, UsedBy, type Dependency } from '../ui';
import { TaCanvas } from '../ta/TaCanvas';
import {
  addEdge,
  addLocation,
  autoLayout,
  clockUses,
  cleanLayout,
  completeLayout,
  conjunctionText,
  deleteEdges,
  deleteLocations,
  edgeLabel,
  emptyLayout,
  emptyModel,
  renameClock,
  renameLocation,
  setInitial,
  uniqueName,
  updateEdge,
  updateLocation,
  validName,
} from '../ta/taEdit';
import '../ta/ta.css';

interface Work {
  model: TaModel;
  layout: TaLayout;
}

const ROLE_TEXT = {
  pt: {
    title: 'Physical System View',
    short: 'V_P',
    description:
      'How the physical system behaves, as reported by its controller or specification: modes, events and their timing. It is the reference the twin is aligned against; it is not executed.',
  },
  dt: {
    title: 'Digital Twin View',
    short: 'V_D',
    description:
      'The behaviour the twin runtime executes: its states, admissible events and timing. It is compiled (translation-validated) into the Twin IR and must be semantically aligned with the Physical System View.',
  },
} as const;

/** A guard or invariant field parsed by the backend (never by the browser). */
function ConstraintField({ label, atoms, kind, onCommit, disabled }: { label: string; atoms: TaAtom[]; kind: 'guard' | 'invariant'; onCommit: (atoms: TaAtom[]) => void; disabled: boolean }) {
  const text = conjunctionText(atoms);
  const [draft, setDraft] = useState(text);
  const [prev, setPrev] = useState(text);
  const [error, setError] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);
  if (prev !== text) {
    setPrev(text);
    setDraft(text);
    setError(null);
  }
  const commit = async () => {
    if (draft.trim() === text) return;
    if (draft.trim() === '') {
      setError(null);
      onCommit([]);
      return;
    }
    setBusy(true);
    try {
      const r = await api.post<{ atoms?: TaAtom[]; diagnostics: TaDiagnostic[] }>('/authoring/constraints/parse', { text: draft, kind });
      const err = r.diagnostics.find((d) => d.severity === 'error');
      if (err || !r.atoms) setError(err ? `${err.code}: ${err.message}${err.hint ? ` — ${err.hint}` : ''}` : 'Not a constraint');
      else {
        setError(null);
        onCommit(r.atoms);
      }
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    } finally {
      setBusy(false);
    }
  };
  return (
    <label className="vts-f">
      <span>{label}</span>
      <input
        className={`vts-input mono${error ? ' is-invalid' : ''}`}
        value={draft}
        disabled={disabled}
        placeholder={kind === 'guard' ? 'e.g. t >= 30' : 'e.g. t <= 600'}
        onChange={(ev) => setDraft(ev.target.value)}
        onBlur={() => void commit()}
        onKeyDown={(ev) => ev.key === 'Enter' && (ev.target as HTMLInputElement).blur()}
        aria-invalid={!!error || undefined}
      />
      {error ? <span className="vts-f__error">{error}</span> : <span className="vts-f__hint">{busy ? 'Checking…' : kind === 'guard' ? 'Conjunction of clock constraints (&&); empty = always enabled' : 'Upper bounds only (< or <=); empty = no deadline'}</span>}
    </label>
  );
}

function NameField({ label, value, taken, onRename, disabled }: { label: string; value: string; taken: string[]; onRename: (n: string) => void; disabled: boolean }) {
  const [text, setText] = useState(value);
  const [prev, setPrev] = useState(value);
  if (prev !== value) {
    setPrev(value);
    setText(value);
  }
  const err = !validName(text) ? 'Letters, digits and _ (not starting with a digit)' : text !== value && taken.includes(text) ? 'Already used' : null;
  return (
    <label className="vts-f">
      <span>{label}</span>
      <input
        className={`vts-input mono${err ? ' is-invalid' : ''}`}
        value={text}
        disabled={disabled}
        onChange={(ev) => setText(ev.target.value)}
        onBlur={() => (!err && text !== value ? onRename(text) : setText(value))}
        onKeyDown={(ev) => ev.key === 'Enter' && (ev.target as HTMLInputElement).blur()}
      />
      {err && <span className="vts-f__error">{err}</span>}
    </label>
  );
}

export default function ModelPage({ role }: { role: 'pt' | 'dt' }) {
  const e = useEditor();
  const qc = useQueryClient();
  const q = useBlueprintModel(e.id, e.version, role);
  const R = ROLE_TEXT[role];
  const [work, setWork] = useState<Work | null>(null);
  const [history, setHistory] = useState<{ past: Work[]; future: Work[] }>({ past: [], future: [] });
  const [dirty, setDirty] = useState(false);
  const [saving, setSaving] = useState(false);
  const [saveError, setSaveError] = useState<string | null>(null);
  const [diagnostics, setDiagnostics] = useState<TaDiagnostic[] | null>(null);
  const [selection, setSelection] = useState<{ locations: string[]; edges: string[] }>({ locations: [], edges: [] });
  const [view, setView] = useState<'diagram' | 'text'>('diagram');
  const [side, setSide] = useState<'selection' | 'model' | 'problems'>('selection');
  const [search, setSearch] = useState('');
  const [focus, setFocus] = useState<{ id: string; nonce: number } | null>(null);
  const [importOpen, setImportOpen] = useState(false);
  const [deleting, setDeleting] = useState<{ locations: string[]; edges: string[] } | null>(null);
  const timer = useRef<ReturnType<typeof setTimeout> | null>(null);
  const workRef = useRef<Work | null>(null);

  const server: Work | null = useMemo(() => (q.data?.model ? { model: q.data.model, layout: completeLayout(q.data.model, q.data.layout) } : null), [q.data]);
  const current = work ?? server;
  useEffect(() => {
    workRef.current = current;
  });
  const diags = useMemo(() => diagnostics ?? q.data?.diagnostics ?? [], [diagnostics, q.data]);
  const editable = e.editable;

  const save = useCallback(async () => {
    const w = workRef.current;
    if (!w || !editable) return;
    setSaving(true);
    setSaveError(null);
    try {
      const r = await e.enqueue((rev) => blueprintApi.saveModel(e.id, e.version, role, rev, w.model, cleanLayout(w.model, w.layout)));
      setDiagnostics(r.diagnostics);
      if (workRef.current === w) setDirty(false);
      void qc.invalidateQueries({ queryKey: bpKeys.version(e.id, e.version) });
      void qc.invalidateQueries({ queryKey: bpKeys.status(e.id, e.version) });
      void qc.invalidateQueries({ queryKey: bpKeys.validation(e.id, e.version) });
      void qc.invalidateQueries({ queryKey: ['blueprints', 'model', e.id, e.version, role === 'pt' ? 'dt' : 'pt'] });
    } catch (err) {
      setSaveError(err instanceof Error ? err.message : String(err));
    } finally {
      setSaving(false);
    }
  }, [e, editable, qc, role]);

  const commit = useCallback(
    (next: Work, label: string) => {
      if (!editable || !current) return;
      void label;
      setHistory((h) => ({ past: [...h.past.slice(-149), current], future: [] }));
      setWork(next);
      setDirty(true);
      if (timer.current) clearTimeout(timer.current);
      timer.current = setTimeout(() => void save(), 1200);
    },
    [current, editable, save],
  );
  useEffect(() => () => {
    if (timer.current) clearTimeout(timer.current);
  }, []);
  const undo = () => {
    const prev = history.past[history.past.length - 1];
    if (!prev || !current) return;
    setHistory((h) => ({ past: h.past.slice(0, -1), future: [current, ...h.future] }));
    setWork(prev);
    setDirty(true);
    if (timer.current) clearTimeout(timer.current);
    timer.current = setTimeout(() => void save(), 1200);
  };
  const redo = () => {
    const next = history.future[0];
    if (!next || !current) return;
    setHistory((h) => ({ past: [...h.past, current], future: h.future.slice(1) }));
    setWork(next);
    setDirty(true);
    if (timer.current) clearTimeout(timer.current);
    timer.current = setTimeout(() => void save(), 1200);
  };

  const errorElements = useMemo(() => new Set(diags.filter((d) => d.severity === 'error' && d.element?.name).map((d) => d.element!.name)), [diags]);
  const highlight = useMemo(() => {
    if (!current || !search.trim()) return null;
    const s = search.toLowerCase();
    const hits = new Set<string>();
    for (const l of current.model.locations) if (l.name.toLowerCase().includes(s)) hits.add(l.name);
    for (const ed of current.model.edges) if (`${edgeLabel(ed)} ${conjunctionText(ed.guard)} ${ed.resets.join(' ')}`.toLowerCase().includes(s)) hits.add(ed.id);
    return hits;
  }, [current, search]);

  const depsFor = (sel: { locations: string[]; edges: string[] }): Dependency[] => {
    if (!current) return [];
    const out: Dependency[] = [];
    for (const n of sel.locations) out.push(...usedByState(e.doc, n, role).filter((d) => d.where !== 'I_P' && d.where !== 'I_D'));
    for (const id of sel.edges) {
      const ed = current.model.edges.find((x) => x.id === id);
      if (!ed?.sync || ed.sync.direction !== '!') continue;
      const label = edgeLabel(ed);
      if (current.model.edges.filter((x) => edgeLabel(x) === label).length === 1) out.push(...usedByLabel(e.doc, label, role).filter((d) => d.where !== 'I_P' && d.where !== 'I_D'));
    }
    return out;
  };
  const doDelete = (sel: { locations: string[]; edges: string[] }) => {
    if (!current) return;
    let w = current;
    if (sel.edges.length) {
      const r = deleteEdges(w.model, w.layout, sel.edges);
      w = { model: r.m, layout: r.l };
    }
    if (sel.locations.length) {
      const r = deleteLocations(w.model, w.layout, sel.locations);
      w = { model: r.m, layout: r.l };
    }
    commit(w, 'Delete');
    setSelection({ locations: [], edges: [] });
  };
  const requestDelete = () => {
    if (!editable || (!selection.locations.length && !selection.edges.length)) return;
    if (depsFor(selection).length > 0) setDeleting(selection);
    else doDelete(selection);
  };

  const onKeyDown = (ev: React.KeyboardEvent) => {
    const t = ev.target as HTMLElement;
    if (t instanceof HTMLInputElement || t instanceof HTMLTextAreaElement || t.closest('.cm-editor')) return;
    const mod = ev.metaKey || ev.ctrlKey;
    if (mod && ev.key.toLowerCase() === 'z' && !ev.shiftKey) {
      ev.preventDefault();
      undo();
    } else if (mod && (ev.key.toLowerCase() === 'y' || (ev.key.toLowerCase() === 'z' && ev.shiftKey))) {
      ev.preventDefault();
      redo();
    } else if (ev.key === 'Delete' || ev.key === 'Backspace') {
      ev.preventDefault();
      requestDelete();
    }
  };

  if (q.isPending) return <EdPage title={R.title}><p className="small muted">Loading…</p></EdPage>;
  if (q.isError) return <EdPage title={R.title}><ErrorBlock error={q.error} onRetry={() => void q.refetch()} /></EdPage>;

  if (!current) {
    const other = role === 'pt' ? 'dt' : 'pt';
    return (
      <EdPage title={`${R.title} (${R.short})`} description={R.description}>
        <EmptyState
          icon={<Workflow size={30} />}
          title={`No ${R.title} yet`}
          action={
            editable ? (
              <div className="row-wrap" style={{ justifyContent: 'center' }}>
                <Button variant="primary" icon={<Plus size={14} />} onClick={() => commit({ model: emptyModel(role === 'pt' ? 'PhysicalSystemPT' : 'DigitalTwinDT'), layout: { ...emptyLayout(), locations: { IDLE: { x: 120, y: 120 } } } }, 'Create model')}>
                  Start drawing
                </Button>
                <Button icon={<FileUp size={14} />} onClick={() => setImportOpen(true)}>Import UPPAAL / TwinTA</Button>
                <CopyFromOther role={role} other={other} onCopy={(w) => commit(w, `Copy from ${other.toUpperCase()}`)} />
              </div>
            ) : undefined
          }
        >
          {editable ? 'Draw the automaton, import an UPPAAL model, or start from a copy of the other view.' : 'This published version has no model for this role.'}
        </EmptyState>
        <ImportModelDialog role={role} open={importOpen} onOpenChange={setImportOpen} onImported={() => { setWork(null); setHistory({ past: [], future: [] }); void q.refetch(); }} />
      </EdPage>
    );
  }

  const { model, layout } = current;
  const loc = selection.locations.length === 1 && selection.edges.length === 0 ? model.locations.find((l) => l.name === selection.locations[0]) : undefined;
  const edge = selection.edges.length === 1 && selection.locations.length === 0 ? model.edges.find((x) => x.id === selection.edges[0]) : undefined;
  const artifact = q.data?.artifact;
  const errors = diags.filter((d) => d.severity === 'error').length;
  const warnings = diags.filter((d) => d.severity === 'warning').length;

  return (
    <EdPage
      wide
      fill
      title={`${R.title} (${R.short})`}
      description={R.description}
      actions={
        <>
          {artifact && <Link to={versionRoute(artifact.kind, artifact.ref)} className="mono xsmall">{artifact.ref}</Link>}
          {artifact && <StatusBadge tone={artifact.state === 'published' ? 'ok' : 'info'} label={artifact.state} />}
          {errors > 0 ? <StatusBadge tone="critical" icon={AlertOctagon} label={`${errors} error${errors === 1 ? '' : 's'}`} /> : <StatusBadge tone="ok" label="Structurally valid" />}
          {warnings > 0 && <StatusBadge tone="warning" icon={AlertTriangle} label={`${warnings} warning${warnings === 1 ? '' : 's'}`} />}
          {editable && <StatusBadge tone={saveError ? 'critical' : saving ? 'info' : dirty ? 'info' : 'ok'} label={saveError ? 'Save failed' : saving ? 'Saving…' : dirty ? 'Unsaved' : 'Saved'} spin={saving} title={saveError ?? undefined} />}
        </>
      }
      guide={
        <>
          Double-click the background to add a state; drag from a state's blue dot to another state (or itself) to add a transition; select a transition to set its event,
          guard and clock resets. Guards and invariants are parsed by the backend. Delete removes the selection, Ctrl+Z / Ctrl+Shift+Z undo and redo.
        </>
      }
    >
      <div className="vts-ed-fill" data-own-history onKeyDown={onKeyDown}>
        <div className="vts-toolbar" role="toolbar" aria-label="Model tools">
          <Button size="sm" variant="ghost" icon={<Plus size={14} />} disabled={!editable} onClick={() => {
            const xs = Object.values(layout.locations);
            const at = { x: (xs.length ? Math.max(...xs.map((p) => p.x)) : 0) + 220, y: xs.length ? xs[0]!.y : 120 };
            const r = addLocation(model, layout, at);
            commit({ model: r.m, layout: r.l }, 'Add state');
            setSelection({ locations: [r.name], edges: [] });
            setSide('selection');
          }}>
            State
          </Button>
          <Button size="sm" variant="ghost" icon={<LayoutGrid size={14} />} disabled={!editable} onClick={() => commit({ model, layout: autoLayout(model, 'LR') }, 'Auto-layout')}>Auto-layout</Button>
          <span className="vts-toolbar__sep" />
          <label className="row">
            <Search size={14} aria-hidden="true" className="subtle" />
            <input
              className="vts-input"
              style={{ minHeight: 28, width: 200 }}
              value={search}
              onChange={(ev) => setSearch(ev.target.value)}
              placeholder="Find state or transition…"
              aria-label="Find state or transition"
              onKeyDown={(ev) => {
                if (ev.key === 'Enter' && highlight && highlight.size) {
                  const id = [...highlight][0]!;
                  setFocus({ id, nonce: Date.now() });
                  setSelection(model.locations.some((l) => l.name === id) ? { locations: [id], edges: [] } : { locations: [], edges: [id] });
                }
              }}
            />
          </label>
          {highlight && <span className="xsmall subtle">{highlight.size} match{highlight.size === 1 ? '' : 'es'}</span>}
          <span className="grow" />
          <Segmented label="Model view" value={view} onChange={setView} options={[{ id: 'diagram', label: 'Diagram' }, { id: 'text', label: 'TwinTA text' }]} />
          <Button size="sm" variant="ghost" icon={<FileUp size={14} />} disabled={!editable} onClick={() => setImportOpen(true)}>Import</Button>
          <ExportMenu model={model} layout={layout} />
        </div>
        <div className="vts-ed-split vts-ed-split--wide-inspector" style={{ alignItems: 'stretch', flex: 1, minHeight: 0 }}>
          <div style={{ minHeight: 420, height: '100%' }}>
            {view === 'diagram' ? (
              <div className="vts-canvas-wrap" style={{ height: '100%' }}>
                <TaCanvas
                  model={model}
                  layout={layout}
                  editable={editable}
                  selection={selection}
                  onSelection={(s) => {
                    setSelection(s);
                    if (s.locations.length || s.edges.length) setSide('selection');
                  }}
                  onMove={(pos) => commit({ model, layout: { ...layout, locations: { ...layout.locations, ...pos } } }, 'Move state')}
                  onConnect={(source, target) => {
                    const r = addEdge(model, source, target);
                    commit({ model: r.m, layout }, 'Add transition');
                    setSelection({ locations: [], edges: [r.id] });
                    setSide('selection');
                  }}
                  onAddLocation={(at) => {
                    const r = addLocation(model, layout, at);
                    commit({ model: r.m, layout: r.l }, 'Add state');
                    setSelection({ locations: [r.name], edges: [] });
                    setSide('selection');
                  }}
                  errorElements={errorElements}
                  highlight={highlight}
                  focus={focus}
                />
              </div>
            ) : (
              <TextView model={model} layout={layout} editable={editable} onApply={(m) => commit({ model: m, layout: completeLayout(m, layout) }, 'Edit as text')} />
            )}
          </div>
          <Pane className="vts-inspector" flush>
            <div style={{ padding: '0 8px' }}>
              <Tabs
                label="Model panels"
                value={side}
                onChange={setSide}
                tabs={[
                  { id: 'selection', label: 'Inspector' },
                  { id: 'model', label: 'Clocks & constants' },
                  { id: 'problems', label: <span className="row">Problems {diags.length > 0 && <span className="vts-tag">{diags.length}</span>}</span> },
                ]}
              />
            </div>
            <div className="vts-ed-pane__body stack-sm">
              {side === 'selection' &&
                (loc ? (
                  <LocationInspector key={loc.name} role={role} model={model} layout={layout} name={loc.name} editable={editable} commit={commit} onRenamed={(n) => setSelection({ locations: [n], edges: [] })} onDelete={requestDelete} diags={diags} />
                ) : edge ? (
                  <EdgeInspector key={edge.id} role={role} model={model} layout={layout} edge={edge} editable={editable} commit={commit} onDelete={requestDelete} diags={diags} />
                ) : selection.locations.length + selection.edges.length > 1 ? (
                  <div className="stack-sm small">
                    <p>{selection.locations.length} state(s) and {selection.edges.length} transition(s) selected.</p>
                    {editable && <Button size="sm" variant="danger" icon={<Trash2 size={13} />} onClick={requestDelete}>Delete selection</Button>}
                  </div>
                ) : (
                  <div className="stack-sm small">
                    <p className="muted">Select a state or a transition to edit it.</p>
                    <p className="xsmall subtle">
                      {model.locations.length} states · {model.edges.length} transitions · {model.clocks.length} clock{model.clocks.length === 1 ? '' : 's'} · {model.channels.length} events
                    </p>
                    {q.data?.semanticDigest && (
                      <p className="xsmall subtle">Semantic digest <HashChip value={diagnostics === null ? q.data.semanticDigest : undefined} label="semantic digest" /></p>
                    )}
                  </div>
                ))}
              {side === 'model' && <ModelInspector model={model} layout={layout} editable={editable} commit={commit} />}
              {side === 'problems' &&
                (diags.length === 0 ? (
                  <p className="small muted">No structural problems.</p>
                ) : (
                  <ul className="vts-findings">
                    {diags.map((d, i) => (
                      <li key={i}>
                        {d.severity === 'error' ? <AlertOctagon size={13} style={{ color: 'var(--crit)' }} aria-label="Error" /> : <AlertTriangle size={13} style={{ color: 'var(--warn)' }} aria-label="Warning" />}
                        <span>
                          {d.element?.name ? (
                            <button type="button" className="vts-linkbtn" onClick={() => {
                              const n = d.element!.name;
                              if (model.locations.some((l) => l.name === n)) setSelection({ locations: [n], edges: [] });
                              else if (model.edges.some((x) => x.id === n)) setSelection({ locations: [], edges: [n] });
                              setFocus({ id: n, nonce: Date.now() });
                            }}>{d.element.name}</button>
                          ) : null}{' '}
                          {d.message} <span className="mono subtle">{d.code}</span>
                          {d.hint && <div className="subtle">{d.hint}</div>}
                        </span>
                      </li>
                    ))}
                  </ul>
                ))}
            </div>
          </Pane>
        </div>
      </div>
      <ImportModelDialog role={role} open={importOpen} onOpenChange={setImportOpen} onImported={() => { setWork(null); setHistory({ past: [], future: [] }); setDiagnostics(null); void q.refetch(); }} />
      <ConfirmDelete
        open={deleting !== null}
        onOpenChange={(o) => !o && setDeleting(null)}
        title="Delete the selection?"
        dependencies={deleting ? depsFor(deleting) : []}
        consequence="Interpretation entries for removed states and events must be removed from the interpretations too; validation will list them."
        onConfirm={() => deleting && doDelete(deleting)}
      />
    </EdPage>
  );
}

function CopyFromOther({ role, other, onCopy }: { role: 'pt' | 'dt'; other: 'pt' | 'dt'; onCopy: (w: Work) => void }) {
  const e = useEditor();
  const q = useBlueprintModel(e.id, e.version, other);
  if (!q.data?.model) return null;
  return (
    <Button onClick={() => onCopy({ model: { ...q.data!.model!, name: role === 'pt' ? `${q.data!.model!.name}PT` : `${q.data!.model!.name}DT` }, layout: completeLayout(q.data!.model!, q.data!.layout) })}>
      Start from a copy of the {other.toUpperCase()} view
    </Button>
  );
}

function LocationInspector({ role, model, layout, name, editable, commit, onRenamed, onDelete, diags }: { role: 'pt' | 'dt'; model: TaModel; layout: TaLayout; name: string; editable: boolean; commit: (w: Work, label: string) => void; onRenamed: (n: string) => void; onDelete: () => void; diags: TaDiagnostic[] }) {
  const e = useEditor();
  const l = model.locations.find((x) => x.name === name)!;
  const own = diags.filter((d) => d.element?.name === name);
  const outgoing = model.edges.filter((x) => x.source === name);
  return (
    <>
      <InspectorSection title="State">
        <NameField label="Name" value={l.name} taken={model.locations.map((x) => x.name)} disabled={!editable} onRename={(n) => { const r = renameLocation(model, layout, l.name, n); commit({ model: r.m, layout: r.l }, `Rename ${l.name}`); onRenamed(n); }} />
        <CheckField label="Initial state" checked={l.initial} disabled={!editable || l.initial} onChange={(v) => v && commit({ model: setInitial(model, l.name), layout }, 'Set initial state')} hint="Exactly one state is initial" />
        <ConstraintField label="Invariant" atoms={l.invariant} kind="invariant" disabled={!editable} onCommit={(atoms) => commit({ model: updateLocation(model, l.name, { invariant: atoms }), layout }, 'Invariant')} />
        <TextArea label="Note" value={l.note} onChange={(v) => commit({ model: updateLocation(model, l.name, { note: v }), layout }, 'State note')} rows={2} />
      </InspectorSection>
      <InspectorSection title="Outgoing transitions">
        {outgoing.length === 0 ? <p className="xsmall subtle">None — a final state.</p> : (
          <ul className="vts-findings">{outgoing.map((x) => <li key={x.id}><span className="mono">{edgeLabel(x)}</span> → {x.target}{x.guard.length > 0 && <span className="subtle"> when {conjunctionText(x.guard)}</span>}</li>)}</ul>
        )}
      </InspectorSection>
      {own.length > 0 && (
        <InspectorSection title="Problems">
          <ul className="vts-findings">{own.map((d, i) => <li key={i} style={{ color: d.severity === 'error' ? 'var(--crit)' : 'var(--warn)' }}>{d.message}</li>)}</ul>
        </InspectorSection>
      )}
      <InspectorSection title="Used by"><UsedBy deps={usedByState(e.doc, name, role)} /></InspectorSection>
      {editable && <div><Button size="sm" variant="danger" icon={<Trash2 size={13} />} onClick={onDelete}>Delete state</Button></div>}
    </>
  );
}

function EdgeInspector({ role, model, layout, edge, editable, commit, onDelete, diags }: { role: 'pt' | 'dt'; model: TaModel; layout: TaLayout; edge: TaEdge; editable: boolean; commit: (w: Work, label: string) => void; onDelete: () => void; diags: TaDiagnostic[] }) {
  const e = useEditor();
  const own = diags.filter((d) => d.element?.name === edge.id);
  const [channel, setChannel] = useState(edge.sync?.channel ?? '');
  const [prev, setPrev] = useState(edge.sync?.channel ?? '');
  if (prev !== (edge.sync?.channel ?? '')) {
    setPrev(edge.sync?.channel ?? '');
    setChannel(edge.sync?.channel ?? '');
  }
  const chErr = channel !== '' && !validName(channel) ? 'Letters, digits and _' : null;
  const set = (patch: Partial<TaEdge>, label: string) => commit({ model: updateEdge(model, edge.id, patch), layout }, label);
  return (
    <>
      <InspectorSection title={`Transition ${edge.id}`}>
        <div className="vts-fgrid">
          <label className="vts-f"><span>From</span>
            <select className="vts-select" value={edge.source} disabled={!editable} onChange={(ev) => set({ source: ev.target.value }, 'Transition source')}>{model.locations.map((l) => <option key={l.name}>{l.name}</option>)}</select>
          </label>
          <label className="vts-f"><span>To</span>
            <select className="vts-select" value={edge.target} disabled={!editable} onChange={(ev) => set({ target: ev.target.value }, 'Transition target')}>{model.locations.map((l) => <option key={l.name}>{l.name}</option>)}</select>
          </label>
        </div>
        <div className="vts-fgrid">
          <label className="vts-f">
            <span>Event (channel)</span>
            <input
              className={`vts-input mono${chErr ? ' is-invalid' : ''}`}
              list="vts-ta-channels"
              value={channel}
              placeholder="internal step (τ)"
              disabled={!editable}
              onChange={(ev) => setChannel(ev.target.value)}
              onBlur={() => !chErr && set({ sync: channel ? { channel, direction: edge.sync?.direction ?? '!' } : null }, 'Transition event')}
              onKeyDown={(ev) => ev.key === 'Enter' && (ev.target as HTMLInputElement).blur()}
            />
            {chErr ? <span className="vts-f__error">{chErr}</span> : <span className="vts-f__hint">Empty = internal step τ</span>}
            <datalist id="vts-ta-channels">{model.channels.map((c) => <option key={c.name} value={c.name} />)}</datalist>
          </label>
          <label className="vts-f">
            <span>Direction</span>
            <select className="vts-select" value={edge.sync?.direction ?? '!'} disabled={!editable || !edge.sync} onChange={(ev) => edge.sync && set({ sync: { channel: edge.sync.channel, direction: ev.target.value as '!' | '?' } }, 'Event direction')}>
              <option value="!">! send (observable event)</option>
              <option value="?">? receive</option>
            </select>
          </label>
        </div>
        <ConstraintField label="Guard" atoms={edge.guard} kind="guard" disabled={!editable} onCommit={(atoms) => set({ guard: atoms }, 'Guard')} />
        <fieldset className="vts-f" style={{ border: 0, padding: 0, margin: 0 }}>
          <legend className="vts-label">Reset clocks</legend>
          <div className="row-wrap">
            {model.clocks.map((c) => (
              <CheckField key={c.name} label={<span className="mono">{c.name} := 0</span>} checked={edge.resets.includes(c.name)} disabled={!editable} onChange={(v) => set({ resets: v ? [...edge.resets, c.name] : edge.resets.filter((r) => r !== c.name) }, 'Clock reset')} />
            ))}
            {model.clocks.length === 0 && <span className="xsmall subtle">No clocks declared.</span>}
          </div>
        </fieldset>
        <TextField label="Note" value={edge.note} onChange={(v) => set({ note: v }, 'Transition note')} />
      </InspectorSection>
      {own.length > 0 && (
        <InspectorSection title="Problems">
          <ul className="vts-findings">{own.map((d, i) => <li key={i} style={{ color: d.severity === 'error' ? 'var(--crit)' : 'var(--warn)' }}>{d.message}</li>)}</ul>
        </InspectorSection>
      )}
      {edge.sync?.direction === '!' && <InspectorSection title="Used by"><UsedBy deps={usedByLabel(e.doc, edgeLabel(edge), role)} /></InspectorSection>}
      {editable && <div><Button size="sm" variant="danger" icon={<Trash2 size={13} />} onClick={onDelete}>Delete transition</Button></div>}
    </>
  );
}

function ModelInspector({ model, layout, editable, commit }: { model: TaModel; layout: TaLayout; editable: boolean; commit: (w: Work, label: string) => void }) {
  return (
    <>
      <InspectorSection title="Automaton">
        <NameField label="Name" value={model.name} taken={[]} disabled={!editable} onRename={(n) => commit({ model: { ...model, name: n }, layout }, 'Rename automaton')} />
        <TextArea label="Note" value={model.note} onChange={(v) => commit({ model: { ...model, note: v }, layout }, 'Model note')} rows={2} />
      </InspectorSection>
      <InspectorSection title="Clocks">
        {model.clocks.map((c) => {
          const uses = clockUses(model, c.name);
          return (
            <div key={c.name} className="row">
              <div className="grow"><NameField label="" value={c.name} taken={model.clocks.map((x) => x.name)} disabled={!editable} onRename={(n) => commit({ model: renameClock(model, c.name, n), layout }, 'Rename clock')} /></div>
              <span className="xsmall subtle">{uses} use{uses === 1 ? '' : 's'}</span>
              <Button size="sm" variant="ghost" iconOnly icon={<Trash2 size={13} />} disabled={!editable || uses > 0} title={uses > 0 ? 'Used by guards, invariants or resets' : undefined} onClick={() => commit({ model: { ...model, clocks: model.clocks.filter((x) => x.name !== c.name) }, layout }, 'Delete clock')}>Delete clock</Button>
            </div>
          );
        })}
        {editable && <Button size="sm" variant="ghost" icon={<Plus size={13} />} onClick={() => commit({ model: { ...model, clocks: [...model.clocks, { name: uniqueName('c', model.clocks.map((x) => x.name)), note: '' }] }, layout }, 'Add clock')}>Add clock</Button>}
      </InspectorSection>
      <InspectorSection title="Integer constants">
        {model.constants.map((c, i) => (
          <div key={i} className="row">
            <input className="vts-input mono grow" value={c.name} disabled={!editable} aria-label="Constant name" onChange={(ev) => commit({ model: { ...model, constants: model.constants.map((x, j) => (j === i ? { ...x, name: ev.target.value } : x)) }, layout }, 'Constant')} />
            <input className="vts-input num" style={{ width: 90 }} value={c.value} disabled={!editable} aria-label="Constant value" onChange={(ev) => /^-?\d+$/.test(ev.target.value) && commit({ model: { ...model, constants: model.constants.map((x, j) => (j === i ? { ...x, value: Number(ev.target.value) } : x)) }, layout }, 'Constant')} />
            <Button size="sm" variant="ghost" iconOnly icon={<Trash2 size={13} />} disabled={!editable} onClick={() => commit({ model: { ...model, constants: model.constants.filter((_, j) => j !== i) }, layout }, 'Delete constant')}>Delete constant</Button>
          </div>
        ))}
        {editable && <Button size="sm" variant="ghost" icon={<Plus size={13} />} onClick={() => commit({ model: { ...model, constants: [...model.constants, { name: uniqueName('LIMIT', model.constants.map((x) => x.name)), value: 0, note: '' }] }, layout }, 'Add constant')}>Add constant</Button>}
        <p className="xsmall subtle">Constants can be used as bounds: <span className="mono">t &lt;= LIMIT</span>.</p>
      </InspectorSection>
      <InspectorSection title="Events (channels)">
        {model.channels.length === 0 ? <p className="xsmall subtle">Declared automatically from the transitions.</p> : <p className="small mono">{model.channels.map((c) => c.name).join(', ')}</p>}
      </InspectorSection>
    </>
  );
}

function TextView({ model, layout, editable, onApply }: { model: TaModel; layout: TaLayout; editable: boolean; onApply: (m: TaModel) => void }) {
  const [text, setText] = useState<string | null>(null);
  const [draft, setDraft] = useState('');
  const [diags, setDiags] = useState<TaDiagnostic[]>([]);
  const [nonce, setNonce] = useState(0);
  const [error, setError] = useState<string | null>(null);
  const key = useMemo(() => JSON.stringify(model), [model]);
  useEffect(() => {
    let live = true;
    api
      .post<{ text: string }>('/authoring/models/validate', { model })
      .then((r) => {
        if (!live) return;
        setText(r.text);
        setDraft(r.text);
        setNonce((n) => n + 1);
      })
      .catch((e) => live && setError(e instanceof Error ? e.message : String(e)));
    return () => {
      live = false;
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [key]);
  void layout;
  if (error) return <ErrorBlock error={new Error(error)} />;
  if (text === null) return <p className="small muted">Rendering…</p>;
  return (
    <div className="stack-sm" style={{ height: '100%' }}>
      <CodeEditor
        value={text}
        resetKey={`ta-text-${nonce}`}
        language="text"
        readOnly={!editable}
        onChange={setDraft}
        ariaLabel="Model as TwinTA text"
        height="calc(100vh - var(--topbar-height) - 380px)"
        diagnostics={diags.filter((d) => d.range).map((d) => ({ code: d.code, severity: d.severity, message: d.message, span: { line: d.range!.line, column: d.range!.column, length: Math.max(1, d.range!.endColumn - d.range!.column) } }))}
      />
      {diags.length > 0 && (
        <ul className="vts-findings">{diags.map((d, i) => <li key={i} style={{ color: d.severity === 'error' ? 'var(--crit)' : 'var(--warn)' }}>{d.range ? `line ${d.range.line}: ` : ''}{d.message} <span className="mono">{d.code}</span></li>)}</ul>
      )}
      {editable && (
        <div className="row">
          <Button
            size="sm"
            variant="primary"
            disabled={draft === text}
            onClick={async () => {
              const r = await api.post<{ valid: boolean; model?: TaModel; diagnostics: TaDiagnostic[] }>('/authoring/models/parse', { source: draft });
              setDiags(r.diagnostics);
              if (r.model && !r.diagnostics.some((d) => d.severity === 'error')) onApply(r.model);
            }}
          >
            Apply text
          </Button>
          <Button size="sm" variant="ghost" disabled={draft === text} onClick={() => { setDraft(text); setNonce((n) => n + 1); setDiags([]); }}>Revert</Button>
          <span className="xsmall subtle">The text and the diagram are the same canonical model; applying replaces the diagram.</span>
        </div>
      )}
    </div>
  );
}

function ExportMenu({ model, layout }: { model: TaModel; layout: TaLayout }) {
  const exp = async (target: 'twinta' | 'uppaal' | 'json') => {
    const r = await api.post<{ content: string; mediaType: string }>('/authoring/models/render', { model, layout: cleanLayout(model, layout), target });
    downloadText(`${model.name}.${target === 'twinta' ? 'twinta' : target === 'uppaal' ? 'xml' : 'tta.json'}`, r.mediaType, r.content);
  };
  return (
    <DropdownMenu.Root>
      <DropdownMenu.Trigger asChild>
        <Button size="sm" variant="ghost" icon={<Download size={14} />}>Export</Button>
      </DropdownMenu.Trigger>
      <DropdownMenu.Portal>
        <DropdownMenu.Content className="vts-menu" align="end" sideOffset={6}>
          <DropdownMenu.Item className="vts-menu__item" onSelect={() => void exp('uppaal')}>UPPAAL XML</DropdownMenu.Item>
          <DropdownMenu.Item className="vts-menu__item" onSelect={() => void exp('twinta')}>TwinTA text</DropdownMenu.Item>
          <DropdownMenu.Item className="vts-menu__item" onSelect={() => void exp('json')}>Canonical JSON (twin-ta/1)</DropdownMenu.Item>
        </DropdownMenu.Content>
      </DropdownMenu.Portal>
    </DropdownMenu.Root>
  );
}

function ImportModelDialog({ role, open, onOpenChange, onImported }: { role: 'pt' | 'dt'; open: boolean; onOpenChange: (o: boolean) => void; onImported: () => void }) {
  const e = useEditor();
  const [file, setFile] = useState<{ name: string; content: string } | null>(null);
  const [result, setResult] = useState<{ imported: boolean; diagnostics: TaDiagnostic[]; format: string } | null>(null);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<unknown>(null);
  const run = async () => {
    if (!file) return;
    setBusy(true);
    setError(null);
    try {
      const r = await e.enqueue(async (rev) => {
        const x = await blueprintApi.importModel(e.id, e.version, role, rev, file.name, file.content);
        return { ...x, revision: x.imported ? x.revision : undefined };
      });
      setResult({ imported: r.imported, diagnostics: r.report.diagnostics, format: r.report.format });
      if (r.imported) onImported();
    } catch (err) {
      setError(err);
    } finally {
      setBusy(false);
    }
  };
  return (
    <Dialog
      open={open}
      onOpenChange={(o) => { onOpenChange(o); if (!o) { setResult(null); setFile(null); } }}
      wide
      title={`Import the ${ROLE_TEXT[role].title}`}
      description="UPPAAL XML (flat, single template), TwinTA text or canonical JSON. Constructs outside the supported fragment are listed and nothing is imported — they are never approximated."
      footer={
        <>
          <Button onClick={() => onOpenChange(false)}>{result?.imported ? 'Done' : 'Cancel'}</Button>
          {!result?.imported && <Button variant="primary" icon={<FileUp size={14} />} disabled={!file} loading={busy} onClick={() => void run()}>Import into this draft</Button>}
        </>
      }
    >
      <div className="stack-sm">
        <input type="file" accept=".xml,.twinta,.ta,.json" aria-label="Model file" onChange={async (ev) => { const f = ev.target.files?.[0]; setResult(null); setFile(f ? { name: f.name, content: await f.text() } : null); }} />
        {error !== null && <ErrorBlock error={error} compact />}
        {result && (
          <>
            {result.imported ? (
              <Callout tone="ok" title={`Imported (${result.format})`}>The model replaced the current {role.toUpperCase()} view of this draft; the previous content stays in the artefact's history.</Callout>
            ) : (
              <Callout tone="critical" title="Not imported">The file uses constructs the verified toolchain does not support. Nothing was changed.</Callout>
            )}
            {result.diagnostics.length > 0 && (
              <table className="vts-table">
                <thead><tr><th>Severity</th><th>Code</th><th>Construct / problem</th><th>Where</th></tr></thead>
                <tbody>
                  {result.diagnostics.map((d, i) => (
                    <tr key={i}>
                      <td><StatusBadge tone={d.severity === 'error' ? 'critical' : d.severity === 'warning' ? 'warning' : 'neutral'} label={d.severity} /></td>
                      <td className="mono xsmall">{d.code}</td>
                      <td className="small">{d.message}{d.hint && <div className="xsmall subtle">{d.hint}</div>}</td>
                      <td className="mono xsmall">{d.element?.name || (d.range ? `line ${d.range.line}` : '—')}</td>
                    </tr>
                  ))}
                </tbody>
              </table>
            )}
          </>
        )}
      </div>
    </Dialog>
  );
}
