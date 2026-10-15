/**
 * The Studio's model store: one working copy of the twin model, one undo history for every
 * edit (structure, behaviour, layout alike), debounced autosave with optimistic concurrency
 * (each save carries the revision it was based on; the server refuses a stale one), the
 * server's diagnostics and summary after each save, and the selection and view, mirrored in
 * the URL so a link opens the same element in the same view.
 */
import { useQueryClient } from '@tanstack/react-query';
import { createContext, useCallback, useContext, useEffect, useMemo, useRef, useState, type ReactNode } from 'react';
import { useBlocker, useSearchParams } from 'react-router-dom';
import { ApiError } from '@/api/client';
import { modelApi, modelKeys } from './api';
import type { Edit } from './ops';
import type { ModelDiagnostic, ModelSummary, TwinModel } from './types';

export type SaveState = 'saved' | 'dirty' | 'saving' | 'error' | 'conflict' | 'readonly';
export type View = 'structure' | 'behavior' | 'generated';

interface Entry {
  doc: TwinModel;
  label: string;
  key?: string;
  at: number;
}

export interface ModelStore {
  id: string;
  version: number;
  editable: boolean;
  doc: TwinModel;
  revision: number;
  saveState: SaveState;
  saveError: string | null;
  diagnostics: ModelDiagnostic[];
  summary?: ModelSummary;
  digest?: string;
  derived: boolean;
  /** Apply an edit (or a function producing one). Returns an error message if the edit was refused. */
  apply: (e: Edit | ((doc: TwinModel) => Edit), opts?: { key?: string }) => string | null;
  undo: () => void;
  redo: () => void;
  canUndo: boolean;
  canRedo: boolean;
  undoLabel: string | null;
  redoLabel: string | null;
  saveNow: () => Promise<void>;
  reload: () => void;
  selection: string | null;
  select: (id: string | null) => void;
  view: View;
  setView: (v: View) => void;
  /** The type whose parts the structure canvas shows (the system by default). */
  scope: string;
  setScope: (t: string) => void;
  /** The type whose behaviour the behaviour canvas shows. */
  machine: string | null;
  setMachine: (t: string | null) => void;
}

const Ctx = createContext<ModelStore | null>(null);

export function useModel(): ModelStore {
  const s = useContext(Ctx);
  if (!s) throw new Error('useModel outside the model Studio');
  return s;
}

const AUTOSAVE_MS = 700;
const COALESCE_MS = 900;

export function ModelStoreProvider({
  id,
  version,
  editable,
  initial,
  initialRevision,
  initialDiagnostics,
  initialSummary,
  initialDigest,
  children,
}: {
  id: string;
  version: number;
  editable: boolean;
  initial: TwinModel;
  initialRevision: number;
  initialDiagnostics: ModelDiagnostic[];
  initialSummary?: ModelSummary;
  initialDigest?: string;
  children: ReactNode;
}) {
  const qc = useQueryClient();
  const [doc, setDoc] = useState<TwinModel>(initial);
  const pastRef = useRef<Entry[]>([]);
  const futureRef = useRef<Entry[]>([]);
  const [history, setHistory] = useState({ canUndo: false, canRedo: false, undoLabel: null as string | null, redoLabel: null as string | null });
  const refreshHistory = useCallback(() => setHistory({
    canUndo: pastRef.current.length > 0,
    canRedo: futureRef.current.length > 0,
    undoLabel: pastRef.current[pastRef.current.length - 1]?.label ?? null,
    redoLabel: futureRef.current[0]?.label ?? null,
  }), []);
  const [saveState, setSaveState] = useState<SaveState>(editable ? 'saved' : 'readonly');
  const [saveError, setSaveError] = useState<string | null>(null);
  const [diagnostics, setDiagnostics] = useState(initialDiagnostics);
  const [summary, setSummary] = useState(initialSummary);
  const [digest, setDigest] = useState(initialDigest);
  const [derived, setDerived] = useState(!!initialSummary);
  const [revision, setRevision] = useState(initialRevision);

  const docRef = useRef(doc);
  const revisionRef = useRef(initialRevision);
  const dirtyRef = useRef(false);
  const savingRef = useRef(false);
  const queueRef = useRef<Promise<unknown>>(Promise.resolve());
  const timerRef = useRef<ReturnType<typeof setTimeout> | null>(null);

  const flush = useCallback(async () => {
    if (!editable) return;
    // A save already in flight is still outstanding work even after its snapshot was queued.
    if (!dirtyRef.current) { await queueRef.current; return; }
    if (timerRef.current) {
      clearTimeout(timerRef.current);
      timerRef.current = null;
    }
    const run = async () => {
      if (!dirtyRef.current) return;
      const sending = docRef.current;
      dirtyRef.current = false;
      savingRef.current = true;
      setSaveState('saving');
      try {
        const r = await modelApi.save(id, version, revisionRef.current, sending);
        revisionRef.current = r.revision;
        setRevision(r.revision);
        setDiagnostics(r.diagnostics);
        setSummary(r.summary);
        setDigest(r.digest);
        setDerived(r.derived);
        setSaveError(null);
        setSaveState(dirtyRef.current ? 'dirty' : 'saved');
        void qc.invalidateQueries({ queryKey: ['blueprints', 'version', id, version] });
        void qc.invalidateQueries({ queryKey: ['blueprints', 'status', id, version] });
      } catch (e) {
        dirtyRef.current = true;
        if (e instanceof ApiError && e.isConflict) {
          setSaveState('conflict');
          setSaveError(e.message);
        } else {
          setSaveState('error');
          setSaveError(e instanceof Error ? e.message : String(e));
        }
      } finally {
        savingRef.current = false;
      }
    };
    queueRef.current = queueRef.current.then(run, run);
    await queueRef.current;
  }, [editable, id, version, qc]);

  const schedule = useCallback(() => {
    dirtyRef.current = true;
    setSaveState('dirty');
    if (timerRef.current) clearTimeout(timerRef.current);
    timerRef.current = setTimeout(() => void flush(), AUTOSAVE_MS);
  }, [flush]);

  const [params, setParams] = useSearchParams();
  const selection = params.get('focus');
  const view = (params.get('view') as View | null) ?? 'structure';
  const scope = params.get('scope') ?? doc.root;
  const machine = params.get('machine');
  const setParam = useCallback(
    (key: string, value: string | null) =>
      setParams(
        (p) => {
          const n = new URLSearchParams(p);
          if (value === null || value === '') n.delete(key);
          else n.set(key, value);
          return n;
        },
        { replace: true },
      ),
    [setParams],
  );

  const apply = useCallback(
    (e: Edit | ((d: TwinModel) => Edit), opts?: { key?: string }): string | null => {
      if (!editable) return 'This version is published and cannot change. Create a draft to edit it.';
      let result: Edit;
      try {
        result = typeof e === 'function' ? e(docRef.current) : e;
      } catch (err) {
        return err instanceof Error ? err.message : String(err);
      }
      const before = docRef.current;
      if (result.doc === before) return null;
      const now = Date.now();
      const last = pastRef.current[pastRef.current.length - 1];
      const coalesce = opts?.key && last && last.key === opts.key && now - last.at < COALESCE_MS;
      if (coalesce) last.at = now;
      else pastRef.current = [...pastRef.current.slice(-199), { doc: before, label: result.label, key: opts?.key, at: now }];
      futureRef.current = [];
      refreshHistory();
      docRef.current = result.doc;
      setDoc(result.doc);
      if (result.select) setParam('focus', result.select);
      schedule();
      return null;
    },
    [editable, schedule, setParam, refreshHistory],
  );

  const undo = useCallback(() => {
    const last = pastRef.current[pastRef.current.length - 1];
    if (!last || !editable) return;
    futureRef.current = [{ doc: docRef.current, label: last.label, at: Date.now() }, ...futureRef.current];
    pastRef.current = pastRef.current.slice(0, -1);
    docRef.current = last.doc;
    setDoc(last.doc);
    schedule();
    refreshHistory();
  }, [schedule, editable, refreshHistory]);

  const redo = useCallback(() => {
    const next = futureRef.current[0];
    if (!next || !editable) return;
    pastRef.current = [...pastRef.current, { doc: docRef.current, label: next.label, at: Date.now() }];
    futureRef.current = futureRef.current.slice(1);
    docRef.current = next.doc;
    setDoc(next.doc);
    schedule();
    refreshHistory();
  }, [schedule, editable, refreshHistory]);

  const reload = useCallback(() => {
    dirtyRef.current = false;
    void qc.invalidateQueries({ queryKey: modelKeys.document(id, version) }).then(() => window.location.reload());
  }, [qc, id, version]);

  // Keyboard: undo/redo outside text fields; save now.
  useEffect(() => {
    const onKey = (ev: KeyboardEvent) => {
      const target = ev.target as HTMLElement | null;
      const typing = target && (target.tagName === 'INPUT' || target.tagName === 'TEXTAREA' || target.isContentEditable);
      const mod = ev.metaKey || ev.ctrlKey;
      if (mod && ev.key.toLowerCase() === 's') {
        ev.preventDefault();
        void flush();
        return;
      }
      if (typing || !mod) return;
      if (ev.key.toLowerCase() === 'z' && !ev.shiftKey) {
        ev.preventDefault();
        undo();
      } else if ((ev.key.toLowerCase() === 'z' && ev.shiftKey) || ev.key.toLowerCase() === 'y') {
        ev.preventDefault();
        redo();
      }
    };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, [flush, undo, redo]);

  // Leaving with unsaved edits saves them first; closing the tab asks.
  const blocker = useBlocker(({ currentLocation, nextLocation }) => (dirtyRef.current || savingRef.current) && currentLocation.pathname !== nextLocation.pathname);
  useEffect(() => {
    if (blocker.state !== 'blocked') return;
    void flush().then(() => ((dirtyRef.current || savingRef.current) ? blocker.reset?.() : blocker.proceed?.()));
  }, [blocker, flush]);
  useEffect(() => {
    const onUnload = (e: BeforeUnloadEvent) => {
      if (dirtyRef.current || savingRef.current) e.preventDefault();
    };
    window.addEventListener('beforeunload', onUnload);
    return () => window.removeEventListener('beforeunload', onUnload);
  }, []);

  const value = useMemo<ModelStore>(
    () => ({
      id,
      version,
      editable,
      doc,
      revision,
      saveState,
      saveError,
      diagnostics,
      summary,
      digest,
      derived,
      apply,
      undo,
      redo,
      ...history,
      saveNow: flush,
      reload,
      selection,
      select: (s) => setParam('focus', s),
      view,
      setView: (v) => setParam('view', v === 'structure' ? null : v),
      scope,
      setScope: (t) => setParam('scope', t === doc.root ? null : t),
      machine,
      setMachine: (t) => setParam('machine', t),
    }),
    [id, version, editable, doc, revision, saveState, saveError, diagnostics, summary, digest, derived, apply, undo, redo, history, flush, reload, selection, setParam, view, scope, machine],
  );
  return <Ctx.Provider value={value}>{children}</Ctx.Provider>;
}
