/**
 * The Blueprint editor store: one working copy of a version's document shared by every
 * Studio editor, with
 *  - a serialized save queue: each save sends the revision it is based on, the server
 *    refuses a stale one (409) and the queue never sends two saves at once, so formal
 *    saves (models, semantics) and section saves can never overwrite each other;
 *  - debounced autosave of drafts (sections), an explicit "Save now", and a visible save
 *    state (saved / unsaved / saving / failed / conflict);
 *  - undo / redo of section edits (Ctrl+Z, Ctrl+Shift+Z or Ctrl+Y), coalescing rapid edits;
 *  - a navigation guard while edits are not yet saved.
 *
 * Published versions are read-only: `update` is a no-op and editors show "Create draft".
 */
import { useQueryClient } from '@tanstack/react-query';
import { createContext, useCallback, useContext, useEffect, useMemo, useRef, useState, type ReactNode } from 'react';
import { useBlocker } from 'react-router-dom';
import { ApiError } from '@/api/client';
import { blueprintApi, bpKeys, useInvalidateBlueprints } from '@/api/blueprints';
import type { BlueprintDocument, BlueprintSectionId, BlueprintVersionDetail, SectionFinding } from '@/api/types';

export type SaveState = 'saved' | 'dirty' | 'saving' | 'error' | 'conflict' | 'readonly';

interface HistoryEntry {
  changes: { section: BlueprintSectionId; before: unknown; after: unknown }[];
  label: string;
  at: number;
  key?: string;
}

export interface EditorApi {
  id: string;
  version: number;
  detail: BlueprintVersionDetail;
  editable: boolean;
  /** Working copy (server document + unsaved local edits). */
  doc: BlueprintDocument;
  revision: number;
  saveState: SaveState;
  saveError: string | null;
  /** Findings returned by the last save of each section. */
  sectionFindings: Partial<Record<BlueprintSectionId, SectionFinding[]>>;
  /** Replace a section (or derive it from the previous value). `key` coalesces rapid edits into one undo step. */
  update: <S extends BlueprintSectionId>(
    section: S,
    next: BlueprintDocument[S] | ((prev: BlueprintDocument[S]) => BlueprintDocument[S]),
    opts?: { label?: string; key?: string },
  ) => void;
  /** Change several sections atomically (one undo step), e.g. a rename that updates every reference. */
  updateDoc: (fn: (doc: BlueprintDocument) => Partial<BlueprintDocument>, opts?: { label?: string }) => void;
  undo: () => void;
  redo: () => void;
  canUndo: boolean;
  canRedo: boolean;
  undoLabel: string | null;
  redoLabel: string | null;
  /** Flush unsaved section edits now. */
  saveNow: () => Promise<void>;
  /** Run a server operation that needs (and advances) the draft revision, after pending saves. */
  enqueue: <T extends { revision?: number }>(op: (revision: number) => Promise<T>) => Promise<T>;
  /** Discard local edits and reload from the server (after a conflict). */
  reload: () => void;
  dirtySections: BlueprintSectionId[];
}

const EditorContext = createContext<EditorApi | null>(null);

export function useEditor(): EditorApi {
  const e = useContext(EditorContext);
  if (!e) throw new Error('useEditor outside a Blueprint workspace');
  return e;
}

/** One section of the working copy with a setter (convenience over useEditor). */
export function useSection<S extends BlueprintSectionId>(section: S) {
  const e = useEditor();
  const value = e.doc[section];
  const set = useCallback(
    (next: BlueprintDocument[S] | ((prev: BlueprintDocument[S]) => BlueprintDocument[S]), opts?: { label?: string; key?: string }) =>
      e.update(section, next, opts),
    [e, section],
  );
  return [value, set, e] as const;
}

const AUTOSAVE_MS = 1200;
const COALESCE_MS = 900;

export function EditorProvider({ detail, children }: { detail: BlueprintVersionDetail; children: ReactNode }) {
  const qc = useQueryClient();
  const invalidate = useInvalidateBlueprints();
  const id = detail.blueprintId;
  const version = detail.version;
  const editable = detail.editable;

  const [local, setLocal] = useState<Partial<BlueprintDocument>>({});
  const [dirty, setDirty] = useState<Set<BlueprintSectionId>>(new Set());
  const [saveState, setSaveState] = useState<SaveState>(editable ? 'saved' : 'readonly');
  const [saveError, setSaveError] = useState<string | null>(null);
  const [findings, setFindings] = useState<Partial<Record<BlueprintSectionId, SectionFinding[]>>>({});
  const [history, setHistory] = useState<{ past: HistoryEntry[]; future: HistoryEntry[] }>({ past: [], future: [] });

  // The save queue works on refs (always the latest values); they are written only in event
  // handlers and effects, and mirrored into state for rendering.
  const [revision, setRevision] = useState(detail.revision);
  const revisionRef = useRef(detail.revision);
  const localRef = useRef<Partial<BlueprintDocument>>({});
  const dirtyRef = useRef<Set<BlueprintSectionId>>(new Set());
  const queueRef = useRef<Promise<unknown>>(Promise.resolve());
  const timerRef = useRef<ReturnType<typeof setTimeout> | null>(null);
  const setDirtySet = useCallback((next: Set<BlueprintSectionId>) => {
    dirtyRef.current = next;
    setDirty(next);
  }, []);

  // A refetch with a newer revision (e.g. after a formal save) becomes the base.
  useEffect(() => {
    if (detail.revision > revisionRef.current) revisionRef.current = detail.revision;
  }, [detail.revision]);

  const doc = useMemo(() => ({ ...detail.document, ...local }) as BlueprintDocument, [detail.document, local]);

  const enqueue = useCallback(
    <T extends { revision?: number }>(op: (revision: number) => Promise<T>): Promise<T> => {
      const run = queueRef.current.then(async () => {
        const result = await op(revisionRef.current);
        if (typeof result?.revision === 'number') {
          revisionRef.current = result.revision;
          setRevision(result.revision);
        }
        return result;
      });
      queueRef.current = run.catch(() => undefined);
      return run;
    },
    [],
  );

  const flush = useCallback(async () => {
    if (!editable) return;
    const sections = [...dirtyRef.current];
    if (sections.length === 0) return;
    setSaveState('saving');
    setSaveError(null);
    try {
      for (const section of sections) {
        const content = (localRef.current as Record<string, unknown>)[section];
        const r = await enqueue((rev) => blueprintApi.saveSection(id, version, section, rev, content));
        setFindings((f) => ({ ...f, [section]: r.findings }));
        // The section is clean unless it changed again while saving.
        if ((localRef.current as Record<string, unknown>)[section] === content) {
          const n = new Set(dirtyRef.current);
          n.delete(section);
          setDirtySet(n);
        }
        qc.setQueryData<BlueprintVersionDetail>(bpKeys.version(id, version), (old) =>
          old ? { ...old, revision: r.revision, documentSha256: r.documentSha256, updatedAt: r.updatedAt, document: { ...old.document, [section]: content } } : old,
        );
      }
      setSaveState(dirtyRef.current.size > 0 ? 'dirty' : 'saved');
      void qc.invalidateQueries({ queryKey: bpKeys.status(id, version) });
      void qc.invalidateQueries({ queryKey: bpKeys.validation(id, version) });
      void qc.invalidateQueries({ queryKey: bpKeys.list });
    } catch (e) {
      if (e instanceof ApiError && e.isConflict && /changed by someone else/i.test(e.message)) {
        setSaveState('conflict');
        setSaveError(e.message);
      } else {
        setSaveState('error');
        setSaveError(e instanceof Error ? e.message : 'Save failed');
      }
    }
  }, [editable, enqueue, id, version, qc, setDirtySet]);

  const schedule = useCallback(() => {
    if (timerRef.current) clearTimeout(timerRef.current);
    timerRef.current = setTimeout(() => void flush(), AUTOSAVE_MS);
  }, [flush]);

  useEffect(() => () => {
    if (timerRef.current) clearTimeout(timerRef.current);
  }, []);

  const apply = useCallback(
    (changes: { section: BlueprintSectionId; value: unknown }[]) => {
      const n = { ...localRef.current } as Record<string, unknown>;
      for (const c of changes) n[c.section] = c.value;
      localRef.current = n as Partial<BlueprintDocument>;
      setLocal(localRef.current);
      const d = new Set(dirtyRef.current);
      for (const c of changes) d.add(c.section);
      setDirtySet(d);
      setSaveState((s) => (s === 'conflict' ? s : 'dirty'));
      schedule();
    },
    [schedule, setDirtySet],
  );
  const current = useCallback(
    (section: BlueprintSectionId) =>
      (localRef.current as Record<string, unknown>)[section] ?? (detail.document as unknown as Record<string, unknown>)[section],
    [detail.document],
  );

  const update = useCallback<EditorApi['update']>(
    (section, next, opts) => {
      if (!editable) return;
      const prev = current(section);
      const value = typeof next === 'function' ? (next as (p: unknown) => unknown)(prev) : next;
      if (value === prev) return;
      const now = Date.now();
      setHistory((h) => {
        const last = h.past[h.past.length - 1];
        if (opts?.key && last && last.key === opts.key && last.changes.length === 1 && last.changes[0]!.section === section && now - last.at < COALESCE_MS) {
          return { past: [...h.past.slice(0, -1), { ...last, changes: [{ ...last.changes[0]!, after: value }], at: now }], future: [] };
        }
        return {
          past: [...h.past.slice(-199), { changes: [{ section, before: prev, after: value }], label: opts?.label ?? `Edit ${section}`, at: now, key: opts?.key }],
          future: [],
        };
      });
      apply([{ section, value }]);
    },
    [apply, current, editable],
  );

  const updateDoc = useCallback<EditorApi['updateDoc']>(
    (fn, opts) => {
      if (!editable) return;
      const whole = { ...detail.document, ...localRef.current } as BlueprintDocument;
      const partial = fn(whole) as Record<string, unknown>;
      const changes = Object.entries(partial)
        .filter(([section, value]) => value !== current(section as BlueprintSectionId))
        .map(([section, value]) => ({ section: section as BlueprintSectionId, before: current(section as BlueprintSectionId), after: value }));
      if (changes.length === 0) return;
      setHistory((h) => ({ past: [...h.past.slice(-199), { changes, label: opts?.label ?? 'Edit', at: Date.now() }], future: [] }));
      apply(changes.map((c) => ({ section: c.section, value: c.after })));
    },
    [apply, current, detail.document, editable],
  );

  const undo = useCallback(() => {
    setHistory((h) => {
      const last = h.past[h.past.length - 1];
      if (!last) return h;
      apply(last.changes.map((c) => ({ section: c.section, value: c.before })));
      return { past: h.past.slice(0, -1), future: [last, ...h.future] };
    });
  }, [apply]);
  const redo = useCallback(() => {
    setHistory((h) => {
      const next = h.future[0];
      if (!next) return h;
      apply(next.changes.map((c) => ({ section: c.section, value: c.after })));
      return { past: [...h.past, next], future: h.future.slice(1) };
    });
  }, [apply]);

  const reload = useCallback(() => {
    localRef.current = {};
    setLocal({});
    setDirtySet(new Set());
    setHistory({ past: [], future: [] });
    setSaveError(null);
    setSaveState(editable ? 'saved' : 'readonly');
    void invalidate();
  }, [editable, invalidate, setDirtySet]);

  // Keyboard: undo / redo / save (not while typing in a code editor that handles its own history).
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      const mod = e.metaKey || e.ctrlKey;
      if (!mod) return;
      const target = e.target as HTMLElement | null;
      const inCode = !!target?.closest('.cm-editor');
      const inField = target instanceof HTMLInputElement || target instanceof HTMLTextAreaElement || target?.isContentEditable;
      const k = e.key.toLowerCase();
      if (k === 's') {
        e.preventDefault();
        void flush();
        return;
      }
      if (inCode || inField || target?.closest('[data-own-history]')) return;
      if (k === 'z' && !e.shiftKey) {
        e.preventDefault();
        undo();
      } else if ((k === 'z' && e.shiftKey) || k === 'y') {
        e.preventDefault();
        redo();
      }
    };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, [flush, undo, redo]);

  // Unsaved edits: warn before leaving the page or the Blueprint.
  const pending = dirty.size > 0 || saveState === 'saving';
  useEffect(() => {
    if (!pending) return;
    const onBefore = (e: BeforeUnloadEvent) => {
      e.preventDefault();
    };
    window.addEventListener('beforeunload', onBefore);
    return () => window.removeEventListener('beforeunload', onBefore);
  }, [pending]);
  const blocker = useBlocker(({ currentLocation, nextLocation }) => {
    if (!pending) return false;
    const base = `/studio/blueprints/${encodeURIComponent(id)}/v/${version}`;
    return currentLocation.pathname.startsWith(base) && !nextLocation.pathname.startsWith(base);
  });
  useEffect(() => {
    if (blocker.state !== 'blocked') return;
    // Save first, then continue; if saving fails, ask.
    void flush().then(() => {
      if (dirtyRef.current.size === 0) blocker.proceed();
      else if (window.confirm('Some edits could not be saved. Leave anyway and lose them?')) blocker.proceed();
      else blocker.reset();
    });
  }, [blocker, flush]);

  const value = useMemo<EditorApi>(
    () => ({
      id,
      version,
      detail,
      editable,
      doc,
      revision: Math.max(revision, detail.revision),
      saveState,
      saveError,
      sectionFindings: findings,
      update,
      updateDoc,
      undo,
      redo,
      canUndo: editable && history.past.length > 0,
      canRedo: editable && history.future.length > 0,
      undoLabel: history.past[history.past.length - 1]?.label ?? null,
      redoLabel: history.future[0]?.label ?? null,
      saveNow: flush,
      enqueue,
      reload,
      dirtySections: [...dirty],
    }),
    [id, version, detail, editable, doc, revision, saveState, saveError, findings, update, updateDoc, undo, redo, history, flush, enqueue, reload, dirty],
  );
  return <EditorContext.Provider value={value}>{children}</EditorContext.Provider>;
}

/** Stable unique id for a new element of a list ("telemetry", existing ids) -> "telemetry_3". */
export function nextId(prefix: string, existing: Iterable<string>, sep = '_'): string {
  const taken = new Set(existing);
  if (!taken.has(prefix)) return prefix;
  for (let i = 2; ; ++i) {
    const c = `${prefix}${sep}${i}`;
    if (!taken.has(c)) return c;
  }
}
