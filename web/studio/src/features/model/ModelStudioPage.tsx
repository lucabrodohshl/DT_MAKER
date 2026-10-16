/** One working model and three views: structure, behaviour, and server-generated outputs. */
import { useState, useEffect } from 'react';
import { Link, useNavigate, useParams } from 'react-router-dom';
import { ChevronDown, ChevronLeft, PanelLeftClose, PanelLeftOpen, PanelRightClose, PanelRightOpen, Redo2, Save, ShieldCheck, Undo2 } from 'lucide-react';
import { blueprintApi, blueprintRoute, useBlueprint, useBlueprintVersion } from '@/api/blueprints';
import { Button } from '@/design';
import { BehaviorCanvas, currentMachine, machines } from './BehaviorCanvas';
import { Explorer } from './Explorer';
import { Generated, type GeneratedTab } from './Generated';
import { Inspector } from './Inspector';
import { modelApi, useModelDocument } from './api';
import { newId, toName, toTypeName, typeByName, locate } from './ops';
import { ModelStoreProvider, useModel, type View } from './store';
import { StructureCanvas } from './StructureCanvas';
import type { BlueprintVersionDetail } from '@/api/types';
import type { LiftResult, TwinModel } from './types';
import './model.css';

function blankModel(detail: BlueprintVersionDetail): TwinModel {
  const identity = detail.document.identity;
  const name = toName(identity.modelId || detail.blueprint.id, 'Twin');
  const root = toTypeName(identity.name || detail.blueprint.name) || 'System';
  return { format: 'twin-model/1', name, label: identity.name || detail.blueprint.name,
    description: identity.description || undefined, root,
    time: { unit: identity.timeUnit || 's', ticksPerUnit: identity.ticksPerUnit || 1000 },
    types: [{ id: newId('t'), name: root, label: identity.name || detail.blueprint.name }],
  };
}

function StartModel({ detail, revision, onCreated }: { detail: BlueprintVersionDetail; revision: number; onCreated: () => void }) {
  const navigate = useNavigate();
  const [lift, setLift] = useState<LiftResult | null>(null);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const editable = detail.editable;
  const base = blueprintRoute(detail.blueprint.id, detail.version);
  const hasDt = !!detail.pins.dt_model;
  const save = async (model: TwinModel) => {
    setBusy(true); setError(null);
    try { await modelApi.save(detail.blueprint.id, detail.version, revision, model); onCreated(); }
    catch (e) { setError(e instanceof Error ? e.message : String(e)); }
    finally { setBusy(false); }
  };
  const inspectLift = async () => {
    setBusy(true); setError(null);
    try { setLift(await modelApi.lift(detail.blueprint.id, detail.version)); }
    catch (e) { setError(e instanceof Error ? e.message : String(e)); }
    finally { setBusy(false); }
  };
  const makeDraft = async () => {
    setBusy(true); setError(null);
    try {
      const d = await blueprintApi.createDraft(detail.blueprint.id, detail.version, 'Model from this version');
      navigate(`${blueprintRoute(detail.blueprint.id, d.version)}/model`);
    } catch (e) { setError(e instanceof Error ? e.message : String(e)); }
    finally { setBusy(false); }
  };
  return <div className="ms-start">
    <Link to="/studio" className="small">← Twin designs</Link>
    <h1>{detail.blueprint.name}</h1>
    <p>This version has no canonical model. Start with a system type, or lift its existing Digital Twin view into a model and review the round trip before using it.</p>
    <div className="ms-start__actions">
      {editable ? hasDt
        ? <Button variant="primary" loading={busy} onClick={() => void inspectLift()}>Preview model from existing view</Button>
        : <Button variant="primary" loading={busy} onClick={() => void save(blankModel(detail))}>Start blank model</Button>
        : <Button variant="primary" loading={busy} onClick={() => void makeDraft()}>Create editable draft</Button>}
      <Link className="vts-btn vts-btn--sm" to={base}>Open advanced editor</Link>
    </div>
    {error && <p role="alert" className="ms-err">{error}</p>}
    {lift && <section className="ms-start__preview">
      <h2>Lift preview</h2>
      <p>{lift.roundTrip ? '✓ Round trip matches the existing Digital Twin automaton.' : `Round trip could not be verified: ${lift.difference ?? 'see diagnostics below'}`}</p>
      <p className="ms-note">{lift.model ? `${lift.model.types.length} asset types · ${lift.model.types.reduce((n, t) => n + (t.behavior?.states.length ?? 0), 0)} states` : 'No model was produced.'}</p>
      {lift.diagnostics.concat(lift.compileDiagnostics ?? []).map((d, i) => <p className="ms-err" key={i}>{d.code}: {d.message}</p>)}
      {lift.model && editable && <Button variant="primary" loading={busy} disabled={!lift.roundTrip} onClick={() => void save(lift.model!)}>Use lifted model</Button>}
    </section>}
  </div>;
}

function Drawer({ open, setOpen }: { open: boolean; setOpen: (v: boolean) => void }) {
  const m = useModel();
  const [tab, setTab] = useState<'problems' | 'checks'>('problems');
  const errors = m.diagnostics.filter((d) => d.severity === 'error').length;
  const warnings = m.diagnostics.filter((d) => d.severity === 'warning').length;
  const focus = (id: string | undefined) => {
    if (!id) return;
    const loc = locate(m.doc, id);
    if (loc) {
      m.select(id);
      if (['state', 'group', 'transition', 'timer'].includes(loc.kind)) { m.setView('behavior'); m.setMachine(loc.type.name); }
      else { m.setView('structure'); m.setScope(loc.type.name); }
    }
  };
  return <div className="ms-drawer">
    <div className="ms-drawer__bar">
      <button type="button" className="ms-drawer__tab" aria-selected={tab === 'problems' && open} onClick={() => { setTab('problems'); setOpen(tab === 'problems' ? !open : true); }}>Problems {errors + warnings || ''}</button>
      <button type="button" className="ms-drawer__tab" aria-selected={tab === 'checks' && open} onClick={() => { setTab('checks'); setOpen(tab === 'checks' ? !open : true); }}>Checks</button>
      <span className="ms-drawer__status">{m.derived ? '✓ compiled' : 'Compilation needs attention'} · {m.summary ? `${m.summary.assets} assets · ${m.summary.locations} locations` : 'no generated summary'}
        <button type="button" className="ms-drawer__toggle" aria-label={open ? 'Collapse drawer' : 'Open drawer'} onClick={() => setOpen(!open)}>{open ? '⌄' : '⌃'}</button>
      </span>
    </div>
    {open && <div className="ms-drawer__body">
      {tab === 'problems' ? (m.diagnostics.length ? m.diagnostics.map((d, i) => <button type="button" className="ms-problem" key={`${d.code}:${i}`} onClick={() => focus(d.element)}>
        <span className={`ms-problem__glyph is-${d.severity}`}>{d.severity === 'error' ? '●' : d.severity === 'warning' ? '▲' : '·'}</span><span className="ms-problem__code">{d.code}</span>
        <span>{d.message}{d.hint && <span className="ms-problem__path"> · {d.hint}</span>}<span className="ms-problem__path"> · {d.path}</span></span>
      </button>) : <p className="ms-note" style={{ padding: '8px 16px' }}>No model problems.</p>)
        : <div className="ms-check"><span>Model compiler</span><span>{m.derived ? '✓ compiled' : 'Needs attention'}</span><span>{m.digest ?? 'No digest yet'}</span><Button size="sm" onClick={() => void m.saveNow()} disabled={!m.editable}>Run now</Button></div>}
    </div>}
  </div>;
}

function Workspace({ detail }: { detail: BlueprintVersionDetail }) {
  const m = useModel();
  const [left, setLeft] = useState(true);
  const [right, setRight] = useState(true);
  const [drawer, setDrawer] = useState(false);
  const [generatedTab, setGeneratedTab] = useState<GeneratedTab>('automaton');
  const [versionMenu, setVersionMenu] = useState(false);
  const bp = useBlueprint(m.id);
  const selectedType = locate(m.doc, m.selection)?.type.name ?? null;
  const machine = currentMachine(m.doc, m.machine, selectedType);
  const scope = typeByName(m.doc, m.scope);
  const errors = m.diagnostics.filter((d) => d.severity === 'error').length;
  const warnings = m.diagnostics.filter((d) => d.severity === 'warning').length;
  const base = blueprintRoute(m.id, m.version);
  useEffect(() => {
    const handler = (e: KeyboardEvent) => {
      const el = e.target as HTMLElement | null;
      if (el && (['INPUT', 'TEXTAREA', 'SELECT'].includes(el.tagName) || el.isContentEditable || e.metaKey || e.ctrlKey || e.altKey)) return;
      if (e.key === '[') setLeft((v) => !v);
      else if (e.key === ']') setRight((v) => !v);
      else if (['1', '2', '3'].includes(e.key)) m.setView((['structure', 'behavior', 'generated'] as const)[Number(e.key) - 1]!);
    };
    window.addEventListener('keydown', handler);
    return () => window.removeEventListener('keydown', handler);
  }, [m]);
  return <div className={`ms${left ? '' : ' is-left-closed'}${right ? '' : ' is-right-closed'}`}>
    <header className="ms-head">
      <Link to="/studio" aria-label="Back to twin designs"><ChevronLeft size={16} /></Link>
      <div className="ms-crumbs"><Link to="/studio">Twin designs</Link><span className="ms-sep">/</span><strong>{m.doc.label ?? detail.blueprint.name}</strong></div>
      <div className="ms-version">
        <button type="button" className="vts-btn vts-btn--sm" aria-expanded={versionMenu} onClick={() => setVersionMenu(!versionMenu)}>{detail.label} <ChevronDown size={12} /></button>
        {versionMenu && <div className="ms-version__list">{(bp.data?.versions ?? []).map((v) => <Link key={v.version} to={`${blueprintRoute(m.id, v.version)}/model`} onClick={() => setVersionMenu(false)}>{v.label} · {v.state}</Link>)}</div>}
      </div>
      <span className={`ms-save${['error', 'conflict'].includes(m.saveState) ? ' is-error' : ''}`} title={m.saveError ?? undefined}>{m.saveState === 'saved' ? '✓ Saved' : m.saveState === 'readonly' ? 'Published · read only' : m.saveState === 'saving' ? 'Saving…' : m.saveState === 'dirty' ? 'Unsaved changes' : m.saveState === 'conflict' ? 'Save conflict' : 'Save failed'}</span>
      <button type="button" className={`ms-problems${errors ? ' has-errors' : warnings ? ' has-warnings' : ''}`} onClick={() => setDrawer(true)}>{errors ? `● ${errors} errors` : warnings ? `▲ ${warnings} warnings` : '✓ No problems'}</button>
      <span className="grow" />
      {m.editable && <>
        <Button size="sm" variant="ghost" iconOnly icon={<Undo2 size={15} />} disabled={!m.canUndo} onClick={m.undo} title={m.undoLabel ?? 'Undo'}>Undo</Button>
        <Button size="sm" variant="ghost" iconOnly icon={<Redo2 size={15} />} disabled={!m.canRedo} onClick={m.redo} title={m.redoLabel ?? 'Redo'}>Redo</Button>
        <Button size="sm" icon={<Save size={14} />} disabled={m.saveState === 'saved' || m.saveState === 'saving'} onClick={() => void m.saveNow()}>Save</Button>
      </>}
      <Button size="sm" icon={<ShieldCheck size={14} />} onClick={() => { setDrawer(true); void m.saveNow(); }}>Validate</Button>
      <Link className="vts-btn vts-btn--sm" to={base}>Advanced</Link>
      <Link className="vts-btn vts-btn--sm vts-btn--primary" to={`${base}/release/package`}>Publish</Link>
    </header>
    {left && <aside className="ms-left" aria-label="Model explorer"><Explorer /></aside>}
    <div className="ms-center">
      <div className="ms-views" role="tablist" aria-label="Model view">
        <button type="button" className="ms-panel-toggle" aria-label={left ? 'Hide explorer' : 'Show explorer'} onClick={() => setLeft(!left)}>{left ? <PanelLeftClose size={15} /> : <PanelLeftOpen size={15} />}</button>
        {(['structure', 'behavior', 'generated'] as View[]).map((v, i) => <button type="button" role="tab" key={v} className="ms-view" aria-selected={m.view === v} onClick={() => m.setView(v)}>{v === 'behavior' ? 'Behavior' : v.charAt(0).toUpperCase() + v.slice(1)} <kbd>{i + 1}</kbd></button>)}
        <div className="ms-views__right">
          {m.view === 'structure' && <span className="ms-scope">Editing <button type="button" onClick={() => { m.setScope(m.doc.root); m.select(null); }}>{m.doc.label ?? m.doc.root}</button>{scope && scope.name !== m.doc.root && <> › {scope.label ?? scope.name}</>}</span>}
          {m.view === 'behavior' && <label className="ms-scope">Machine <select className="ms-sel" value={machine?.name ?? ''} onChange={(e) => { m.setMachine(e.target.value); m.select(null); }}><option value="">Choose…</option>{machines(m.doc).map((t) => <option key={t.id} value={t.name}>{t.label ?? t.name}</option>)}{machine && !machines(m.doc).some((t) => t.name === machine.name) && <option value={machine.name}>{machine.name}</option>}</select></label>}
          <button type="button" className="ms-panel-toggle" aria-label={right ? 'Hide inspector' : 'Show inspector'} onClick={() => setRight(!right)}>{right ? <PanelRightClose size={15} /> : <PanelRightOpen size={15} />}</button>
        </div>
      </div>
      <div className="ms-canvas">{m.view === 'structure' ? <StructureCanvas /> : m.view === 'behavior' && machine ? <BehaviorCanvas type={machine} /> : m.view === 'behavior' ? <div className="ms-empty"><div><strong>No behavior yet.</strong><span>Select a type to add its first state.</span></div></div> : <Generated tab={generatedTab} setTab={setGeneratedTab} />}</div>
    </div>
    {right && <aside className="ms-right" aria-label="Inspector"><Inspector /></aside>}
    <Drawer open={drawer} setOpen={setDrawer} />
  </div>;
}

export default function ModelStudioPage() {
  const { bpId, version } = useParams();
  const v = Number(version);
  const q = useModelDocument(bpId, v);
  const detail = useBlueprintVersion(bpId, v);
  if (!bpId || !Number.isInteger(v) || v < 1) return <p className="ms-err">Invalid model version.</p>;
  if (q.isPending || detail.isPending) return <div className="ms-loading">Opening model…</div>;
  if (q.error || detail.error || !q.data || !detail.data) return <div role="alert" className="ms-loading ms-err">{String(q.error ?? detail.error ?? 'Model not found')}</div>;
  if (!q.data.model) return <StartModel detail={detail.data} revision={q.data.revision} onCreated={() => { void q.refetch(); void detail.refetch(); }} />;
  return <ModelStoreProvider key={`${bpId}:${v}`} id={bpId} version={v} editable={q.data.editable} initial={q.data.model} initialRevision={q.data.revision}
    initialDiagnostics={q.data.diagnostics ?? []} initialDigest={q.data.digest} initialSummary={q.data.summary}>
    <Workspace detail={detail.data} />
  </ModelStoreProvider>;
}
