/**
 * Source editor of a Blueprint's ontology or interpretation: the pinned artefact version's
 * text with the backend's strict-parser diagnostics, symbol autocompletion, validation
 * evidence, autosave into the draft's editable version, pinning of an existing version,
 * file import and comparison with another version.
 */
import { useQueryClient } from '@tanstack/react-query';
import { FileUp, GitCompare, Pin, ShieldCheck } from 'lucide-react';
import { useCallback, useEffect, useRef, useState, type ReactNode } from 'react';
import { Link } from 'react-router-dom';
import { blueprintApi, bpKeys, useBlueprintSemantics } from '@/api/blueprints';
import { api } from '@/api/client';
import { useArtifacts } from '@/api/queries';
import type { ArtifactKind, SemanticsView, Span, VersionDetail } from '@/api/types';
import { Button, Callout, Dialog, ErrorBlock, Skeleton, StatusBadge, TimeStamp } from '@/design';
import { CodeEditor, type SymbolEntry } from '@/editor/CodeEditor';
import { SourceDiff } from '@/editor/SourceDiff';
import { versionRoute } from '@/features/common/links';
import { useEditor } from '../editor';

export type SemanticsRole = 'ontology' | 'pt_interpretation' | 'dt_interpretation';

export function ValidationBadge({ detail }: { detail: VersionDetail | null | undefined }) {
  if (!detail) return <StatusBadge tone="neutral" label="Not defined" />;
  if (detail.validationRunning) return <StatusBadge tone="info" label="Validating…" spin />;
  const v = detail.validation;
  if (!v) return <StatusBadge tone="neutral" label="Not validated" />;
  if (v.outcome === 'pass') return <StatusBadge tone="ok" icon={ShieldCheck} label="VALID" title={`${v.id} · ${v.checker}`} />;
  return <StatusBadge tone="critical" label={v.outcome === 'fail' ? 'INVALID' : 'Check error'} title={v.summary} />;
}

function PinDialog({ role, open, onOpenChange }: { role: SemanticsRole; open: boolean; onOpenChange: (o: boolean) => void }) {
  const e = useEditor();
  const qc = useQueryClient();
  const kind: ArtifactKind = role === 'ontology' ? 'ontology' : 'interpretation';
  const artifacts = useArtifacts(kind);
  const [ref, setRef] = useState('');
  const [error, setError] = useState<unknown>(null);
  const [busy, setBusy] = useState(false);
  const options = (artifacts.data ?? []).flatMap((a) => [a.published, a.latest].filter((v, i, arr): v is NonNullable<typeof v> => !!v && arr.findIndex((x) => x?.ref === v.ref) === i).map((v) => ({ ref: v.ref, label: `${a.name} · ${v.ref} (${v.state})` })));
  return (
    <Dialog
      open={open}
      onOpenChange={onOpenChange}
      title={role === 'ontology' ? 'Use an existing ontology version' : 'Use an existing interpretation version'}
      description="Pinning reuses a version (e.g. a shared plant ontology) instead of editing a copy. Validation and alignment are re-run for the new combination."
      footer={
        <>
          <Button onClick={() => onOpenChange(false)}>Cancel</Button>
          <Button
            variant="primary"
            icon={<Pin size={14} />}
            disabled={!ref}
            loading={busy}
            onClick={async () => {
              setBusy(true);
              setError(null);
              try {
                await e.enqueue((rev) => blueprintApi.saveSemantics(e.id, e.version, role, rev, { ref }));
                await qc.invalidateQueries({ queryKey: ['blueprints'] });
                onOpenChange(false);
              } catch (err) {
                setError(err);
              } finally {
                setBusy(false);
              }
            }}
          >
            Pin version
          </Button>
        </>
      }
    >
      <div className="stack-sm">
        <select className="vts-select" value={ref} onChange={(ev) => setRef(ev.target.value)} aria-label="Version to pin">
          <option value="">Choose a version…</option>
          {options.map((o) => <option key={o.ref} value={o.ref}>{o.label}</option>)}
        </select>
        {error !== null && <ErrorBlock error={error} compact />}
      </div>
    </Dialog>
  );
}

function CompareDialog({ role, current, open, onOpenChange }: { role: SemanticsRole; current: VersionDetail; open: boolean; onOpenChange: (o: boolean) => void }) {
  const kind: ArtifactKind = role === 'ontology' ? 'ontology' : 'interpretation';
  const artifacts = useArtifacts(kind);
  const [ref, setRef] = useState('');
  const [other, setOther] = useState<VersionDetail | null>(null);
  const options = (artifacts.data ?? []).flatMap((a) => [a.published, a.latest, a.open].filter((v): v is NonNullable<typeof v> => !!v)).filter((v, i, arr) => arr.findIndex((x) => x.ref === v.ref) === i && v.ref !== current.ref);
  return (
    <Dialog open={open} onOpenChange={onOpenChange} wide title="Compare with another version" description="Line-by-line difference; the structural impact of an ontology change is analysed by the refinement check.">
      <div className="stack-sm">
        <select
          className="vts-select"
          value={ref}
          aria-label="Version to compare with"
          onChange={async (ev) => {
            setRef(ev.target.value);
            const [id, v] = ev.target.value.split('@');
            setOther(ev.target.value ? await api.get<VersionDetail>(`/artifacts/${encodeURIComponent(id!)}/versions/${v}`) : null);
          }}
        >
          <option value="">Choose a version…</option>
          {options.map((o) => <option key={o.ref} value={o.ref}>{o.ref} ({o.state})</option>)}
        </select>
        {other && <SourceDiff a={other.content} b={current.content} language="ontology" labelA={other.ref} labelB={`${current.ref} (this Blueprint)`} />}
      </div>
    </Dialog>
  );
}

export function SemanticsEditor({
  role,
  symbols,
  aside,
  onView,
  reveal,
  height = 'calc(100vh - var(--topbar-height) - 330px)',
}: {
  role: SemanticsRole;
  symbols: SymbolEntry[];
  aside?: (view: SemanticsView) => ReactNode;
  onView?: (view: SemanticsView | undefined) => void;
  reveal?: { span: Span; nonce: number } | null;
  height?: string | number;
}) {
  const e = useEditor();
  const qc = useQueryClient();
  const q = useBlueprintSemantics(e.id, e.version, role);
  const view = q.data;
  const detail = view?.artifact ?? null;
  const [text, setText] = useState<string | null>(null);
  const [state, setState] = useState<'saved' | 'dirty' | 'saving' | 'error'>('saved');
  const [error, setError] = useState<string | null>(null);
  const [pinOpen, setPinOpen] = useState(false);
  const [compareOpen, setCompareOpen] = useState(false);
  const timer = useRef<ReturnType<typeof setTimeout> | null>(null);
  const textRef = useRef<string | null>(null);
  // The editor is reloaded only when the server content differs from what it shows (another
  // version pinned, an external change) — never after our own autosave.
  const serverContent = detail?.content ?? '';
  const [editorKey, setEditorKey] = useState(0);
  const [lastServer, setLastServer] = useState(serverContent);
  if (lastServer !== serverContent) {
    setLastServer(serverContent);
    if (state !== 'dirty' && state !== 'saving' && (text === null || text !== serverContent)) {
      setText(null);
      setEditorKey((k) => k + 1);
    }
  }
  useEffect(() => {
    onView?.(view);
  }, [view, onView]);
  useEffect(() => {
    textRef.current = text;
  });

  const save = useCallback(async () => {
    const content = textRef.current;
    if (content === null || !e.editable) return;
    setState('saving');
    setError(null);
    try {
      await e.enqueue((rev) => blueprintApi.saveSemantics(e.id, e.version, role, rev, { content }));
      setState(textRef.current === content ? 'saved' : 'dirty');
      await qc.invalidateQueries({ queryKey: bpKeys.semantics(e.id, e.version, role) });
      void qc.invalidateQueries({ queryKey: bpKeys.status(e.id, e.version) });
      void qc.invalidateQueries({ queryKey: bpKeys.version(e.id, e.version) });
      if (role === 'ontology') {
        void qc.invalidateQueries({ queryKey: bpKeys.semantics(e.id, e.version, 'pt_interpretation') });
        void qc.invalidateQueries({ queryKey: bpKeys.semantics(e.id, e.version, 'dt_interpretation') });
      }
    } catch (err) {
      setState('error');
      setError(err instanceof Error ? err.message : String(err));
    }
  }, [e, qc, role]);

  const onChange = (t: string) => {
    setText(t);
    setState('dirty');
    if (timer.current) clearTimeout(timer.current);
    timer.current = setTimeout(() => void save(), 1500);
  };
  useEffect(() => () => {
    if (timer.current) clearTimeout(timer.current);
  }, []);

  if (q.isPending) return <Skeleton lines={8} />;
  if (q.isError) return <ErrorBlock error={q.error} onRetry={() => void q.refetch()} />;
  const content = text ?? detail?.content ?? '';
  const needsOntology = role !== 'ontology' && !e.detail.pins.ontology;
  return (
    <div className="stack-sm">
      <div className="row-wrap">
        {detail ? (
          <>
            <Link to={versionRoute(detail.kind, detail.ref)} className="mono small">{detail.ref}</Link>
            <StatusBadge tone={detail.state === 'published' ? 'ok' : detail.state === 'verified' ? 'formal' : 'info'} label={detail.state} />
            <ValidationBadge detail={detail} />
            {detail.validation && <span className="xsmall subtle">checked <TimeStamp value={detail.validation.createdAt} relative /></span>}
          </>
        ) : (
          <StatusBadge tone="neutral" label="Not defined yet" />
        )}
        {e.editable && <StatusBadge tone={state === 'error' ? 'critical' : state === 'saved' ? 'ok' : 'info'} label={state === 'saving' ? 'Saving…' : state === 'dirty' ? 'Unsaved' : state === 'error' ? 'Save failed' : 'Saved'} spin={state === 'saving'} title={error ?? undefined} />}
        <span className="grow" />
        {e.editable && <Button size="sm" variant="ghost" icon={<Pin size={13} />} onClick={() => setPinOpen(true)}>Use existing version</Button>}
        {e.editable && (
          <label className="vts-btn vts-btn--ghost vts-btn--sm" style={{ cursor: 'pointer' }}>
            <FileUp size={13} aria-hidden="true" /> Import file
            <input type="file" accept={role === 'ontology' ? '.ont,.txt' : '.interp,.txt'} style={{ display: 'none' }} onChange={async (ev) => { const f = ev.target.files?.[0]; if (f) { onChange(await f.text()); setEditorKey((k) => k + 1); } }} />
          </label>
        )}
        {detail && <Button size="sm" variant="ghost" icon={<GitCompare size={13} />} onClick={() => setCompareOpen(true)}>Compare</Button>}
      </div>
      {needsOntology && <Callout tone="warning" title="No ontology yet">An interpretation gives meaning in terms of an ontology. Define or pin the ontology first (Semantics → Ontology).</Callout>}
      {error && <Callout tone="critical" title="Not saved">{error}</Callout>}
      <div className={aside && view ? 'vts-ed-split vts-ed-split--wide-inspector' : undefined}>
        <CodeEditor
          value={content}
          resetKey={`${role}-${editorKey}`}
          onChange={onChange}
          readOnly={!e.editable || needsOntology}
          language={role === 'ontology' ? 'ontology' : 'interpretation'}
          diagnostics={detail && (text === null || text === detail.content) ? detail.diagnostics : []}
          symbols={symbols}
          reveal={reveal}
          ariaLabel={role === 'ontology' ? 'Ontology source' : 'Interpretation source'}
          height={height}
        />
        {aside && view && aside(view)}
      </div>
      <PinDialog role={role} open={pinOpen} onOpenChange={setPinOpen} />
      {detail && <CompareDialog role={role} current={detail} open={compareOpen} onOpenChange={setCompareOpen} />}
    </div>
  );
}
