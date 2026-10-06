/**
 * An artefact version: editor (drafts) or read-only source (published), with the
 * engineering actions kept explicitly separate:
 *
 *   Save Draft · Validate · Compare · Check Refinement · Analyze Impact · Publish
 *
 * Saving never validates, publishes or deploys. Published versions are immutable:
 * editing starts a new draft. Live data never overwrites the editor's content.
 */
import { useQueryClient } from '@tanstack/react-query';
import { CheckCircle2, FileDiff, GitPullRequest, Network, Save, ShieldCheck, Upload, XCircle } from 'lucide-react';
import { useEffect, useMemo, useState } from 'react';
import { Link, useBlocker, useLocation, useNavigate, useParams, useSearchParams } from 'react-router-dom';
import { api, ApiError } from '@/api/client';
import { keys, useArtifact, useChanges, useEngineeringMutation, useTwin, useVersion } from '@/api/queries';
import type { ArtifactKind, ArtifactVersion, EvidenceRecord, Span, VersionDetail } from '@/api/types';
import {
  Button,
  Callout,
  Dialog,
  ErrorBlock,
  HashChip,
  KeyValue,
  LifecycleBadge,
  OutcomeBadge,
  PageHeader,
  Panel,
  QueryState,
  Tabs,
  TimeStamp,
} from '@/design';
import { CodeEditor, type SymbolEntry } from '@/editor/CodeEditor';
import { Crumbs, EvidenceLink, RefLink, artifactSection, versionRoute, LearnMore } from '@/features/common/links';
import { useDisclosure } from '@/app/disclosure';
import { DependenciesPanel, EvaluatePanel, LineagePanel, OntologyGraph, StructurePanel, SymbolPanel, isInterpretation, isOntology } from './versionPanels';

type Tab = 'source' | 'structure' | 'symbol' | 'dependencies' | 'visual' | 'evaluate' | 'validation' | 'history';

function symbolsOf(v: VersionDetail): SymbolEntry[] {
  const s = v.structure;
  if (isOntology(s)) {
    return [
      ...s.sorts.map((x) => ({ name: x.name, kind: 'sort' as const, detail: 'sort', span: x.span })),
      ...s.functions.map((x) => ({ name: x.name, kind: 'function' as const, detail: x.signature, span: x.span })),
      ...s.relations.map((x) => ({ name: x.name, kind: 'relation' as const, detail: `rel ${x.signature}`, span: x.span })),
      ...s.axioms.map((x) => ({ name: x.id, kind: 'axiom' as const, detail: 'axiom', span: x.span })),
    ];
  }
  if (isInterpretation(s)) return s.entries.map((e) => ({ name: e.key, kind: 'entry' as const, detail: e.isEvent ? 'event' : 'location', span: e.span }));
  return [];
}

function NewDraftDialog({ v, open, onOpenChange }: { v: VersionDetail; open: boolean; onOpenChange: (o: boolean) => void }) {
  const [description, setDescription] = useState('');
  const [changeId, setChangeId] = useState('');
  const changes = useChanges('open');
  const navigate = useNavigate();
  const create = useEngineeringMutation((body: { description: string; changeId?: string }) =>
    api.post<ArtifactVersion>(`/artifacts/${encodeURIComponent(v.artifactId)}/versions/${v.version}/drafts`, body),
  );
  return (
    <Dialog
      open={open}
      onOpenChange={onOpenChange}
      title={`New draft from ${v.ref}`}
      description="The published version stays unchanged; the draft is a new version with this one as its parent."
      footer={
        <>
          <Button onClick={() => onOpenChange(false)}>Cancel</Button>
          <Button
            variant="primary"
            loading={create.isPending}
            disabled={!description.trim()}
            onClick={() =>
              create.mutate(
                { description: description.trim(), ...(changeId ? { changeId } : {}) },
                { onSuccess: (d) => { onOpenChange(false); navigate(versionRoute(v.kind, d.ref)); } },
              )
            }
          >
            Create draft
          </Button>
        </>
      }
    >
      <div className="stack">
        <label className="vts-field">
          <span>Change description</span>
          <textarea className="vts-textarea" value={description} onChange={(e) => setDescription(e.target.value)} placeholder="Why is this version needed?" />
        </label>
        <label className="vts-field">
          <span>Attach to change workspace (optional)</span>
          <select className="vts-select" value={changeId} onChange={(e) => setChangeId(e.target.value)}>
            <option value="">— none —</option>
            {changes.data?.map((c) => <option key={c.id} value={c.id}>{c.id}: {c.title}</option>)}
          </select>
          <span className="vts-hint">A change workspace coordinates ontology, interpretation and model drafts through the release pipeline.</span>
        </label>
        {create.error && <ErrorBlock error={create.error} compact />}
      </div>
    </Dialog>
  );
}

function RefinementDialog({ v, open, onOpenChange }: { v: VersionDetail; open: boolean; onOpenChange: (o: boolean) => void }) {
  const artifact = useArtifact(v.artifactId);
  const published = artifact.data?.versions.find((x) => x.state === 'published' && x.version !== v.version);
  // null = not chosen yet: defaults to the parent version and the first deployment.
  const [baseChoice, setBase] = useState<string | null>(null);
  const [scopeChoice, setScope] = useState<string | null>(null);
  const base = baseChoice ?? (v.parentVersion ? `${v.artifactId}@${v.parentVersion}` : published?.ref) ?? '';
  const deployments = artifact.data?.deployedIn ?? [];
  const scope = scopeChoice ?? deployments[0]?.twinId ?? '';
  const twin = useTwin(scope || null);
  const navigate = useNavigate();
  const run = useEngineeringMutation(() => {
    const interps = twin.data?.bindings ?? [];
    const pt = interps.find((b) => b.role === 'pt_interpretation')?.ref;
    const dt = interps.find((b) => b.role === 'dt_interpretation')?.ref;
    return api.post<EvidenceRecord>('/refinement-checks', {
      base: { ontology: base, ...(scope && pt ? { ptInterpretation: pt } : {}), ...(scope && dt ? { dtInterpretation: dt } : {}) },
      candidate: { ontology: v.ref },
    });
  });
  return (
    <Dialog
      open={open}
      onOpenChange={onOpenChange}
      wide
      title="Check refinement (Definition 4)"
      description="Decided by the formal backend (aligner parser + Z3). The result is stored as evidence."
      footer={
        <>
          <Button onClick={() => onOpenChange(false)}>Cancel</Button>
          <Button
            variant="primary"
            icon={<ShieldCheck size={14} />}
            loading={run.isPending}
            disabled={!base || (!!scope && !twin.data)}
            onClick={() => run.mutate(undefined, { onSuccess: (ev) => { onOpenChange(false); navigate(`/studio/refinement/${ev.id}`); } })}
          >
            Run refinement check
          </Button>
        </>
      }
    >
      <div className="stack">
        <div className="grid-2">
          <label className="vts-field">
            <span>Base ontology (K₂)</span>
            <select className="vts-select" value={base} onChange={(e) => setBase(e.target.value)}>
              {artifact.data?.versions.filter((x) => x.version !== v.version).map((x) => (
                <option key={x.ref} value={x.ref}>{x.ref} ({x.state})</option>
              ))}
            </select>
          </label>
          <div className="vts-field">
            <span className="vts-label">Candidate (K₁)</span>
            <span className="mono">{v.ref}</span>
          </div>
        </div>
        <label className="vts-field">
          <span>Interpretations (condition c)</span>
          <select className="vts-select" value={scope} onChange={(e) => setScope(e.target.value)}>
            <option value="">Ontology only (K₁ ⊑ K₂; condition c not evaluated)</option>
            {deployments.map((d) => (
              <option key={d.twinId} value={d.twinId}>Deployed domain knowledge of {d.twinName} (I_P, I_D)</option>
            ))}
          </select>
          <span className="vts-hint">
            Including the deployed interpretations checks Φ′ ⊑ Φ, which is what Theorem 3 needs to preserve the existing alignment.
          </span>
        </label>
        {run.error && <ErrorBlock error={run.error} compact />}
      </div>
    </Dialog>
  );
}

function ValidationPanel({ v }: { v: VersionDetail }) {
  const e = v.validation;
  if (!e) return <Callout tone="neutral" title="Not validated">No validation evidence exists for the exact content of this version.</Callout>;
  const doc = e.document as { diagnostics?: { code: string; severity: string; message: string; span?: Span; where?: string }[]; consistent?: string } | undefined;
  return (
    <div className="stack">
      <KeyValue
        compact
        items={[
          ['Result', <OutcomeBadge key="o" outcome={e.outcome} verdict={e.verdict} />],
          ['Evidence', <EvidenceLink key="e" id={e.id} />],
          ['Checked', <TimeStamp key="t" value={e.createdAt} kind="evidence" />],
          ['Checker', <span key="c" className="small">{e.checker}</span>],
          ...(doc?.consistent ? [['Axioms consistent', doc.consistent] as [string, React.ReactNode]] : []),
          ['Evidence hash', <HashChip key="h" value={e.evidenceSha256} />],
        ]}
      />
      {doc?.diagnostics && doc.diagnostics.length > 0 && (
        <ul className="vts-list">
          {doc.diagnostics.map((d, i) => (
            <li key={i} className="small">
              <strong className="mono">{d.code}</strong> {d.severity} {d.span?.line ? `(line ${d.span.line})` : d.where ? `(${d.where})` : ''}: {d.message}
            </li>
          ))}
        </ul>
      )}
    </div>
  );
}

/**
 * @param artifactRef Version to show ("id@v"), used by the twin workspace's read-only views;
 *        otherwise the :artifactId/:version route parameters.
 */
export default function VersionPage({ artifactRef }: { artifactRef?: string } = {}) {
  const params0 = useParams();
  const [refId, refVersion] = artifactRef ? artifactRef.split('@') : [undefined, undefined];
  const artifactId = refId ?? params0.artifactId ?? '';
  const version = refVersion ?? params0.version ?? '';
  const vnum = Number(version);
  const location = useLocation();
  const kindSection = (artifactRef ? undefined : location.pathname.split('/')[2]) as 'ontologies' | 'interpretations' | 'models' | undefined;
  const q = useVersion(artifactId, vnum);
  const qc = useQueryClient();
  const navigate = useNavigate();
  const { engineering } = useDisclosure();
  const [params] = useSearchParams();
  const [tab, setTab] = useState<Tab>('source');
  const [draft, setDraft] = useState<string | null>(null);
  const [symbol, setSymbol] = useState<string | null>(params.get('symbol'));
  const [reveal, setReveal] = useState<{ span: Span; nonce: number } | null>(null);
  const [newDraftOpen, setNewDraftOpen] = useState(false);
  const [refineOpen, setRefineOpen] = useState(false);
  const [rejectReason, setRejectReason] = useState('');
  const [rejectOpen, setRejectOpen] = useState(false);
  const v = q.data;
  const dirty = draft !== null && v !== undefined && draft !== v.content;
  const editable = v?.state === 'draft' || v?.state === 'verified';

  // Never lose unsaved edits silently.
  const blocker = useBlocker(({ currentLocation, nextLocation }) => dirty && currentLocation.pathname !== nextLocation.pathname);
  useEffect(() => {
    if (blocker.state === 'blocked') {
      if (window.confirm('You have unsaved changes in this draft. Leave without saving?')) blocker.proceed();
      else blocker.reset();
    }
  }, [blocker]);
  useEffect(() => {
    const onUnload = (e: BeforeUnloadEvent) => {
      if (dirty) e.preventDefault();
    };
    window.addEventListener('beforeunload', onUnload);
    return () => window.removeEventListener('beforeunload', onUnload);
  }, [dirty]);

  // Reveal ?symbol= / ?axiom= / ?entry= from deep links.
  useEffect(() => {
    if (!v) return;
    const target = params.get('symbol') ?? params.get('axiom') ?? params.get('entry');
    if (!target) return;
    const entry = symbolsOf(v).find((s) => s.name === target);
    // Syncing the editor to the URL (an external system) is what this effect is for.
    // eslint-disable-next-line react-hooks/set-state-in-effect
    if (entry) setReveal({ span: entry.span, nonce: Date.now() });
    if (params.get('symbol')) setSymbol(params.get('symbol'));
  }, [v, params]);

  const refreshVersion = () => qc.invalidateQueries({ queryKey: keys.version(artifactId, vnum) });
  const save = useEngineeringMutation((content: string) => api.put<ArtifactVersion & { diagnostics: VersionDetail['diagnostics'] }>(`/artifacts/${encodeURIComponent(artifactId)}/versions/${vnum}`, { content }));
  const validate = useEngineeringMutation(() => api.post<VersionDetail>(`/artifacts/${encodeURIComponent(artifactId)}/versions/${vnum}/validate`));
  const publish = useEngineeringMutation(() => api.post<ArtifactVersion>(`/artifacts/${encodeURIComponent(artifactId)}/versions/${vnum}/publish`));
  const reject = useEngineeringMutation((reason: string) => api.post<ArtifactVersion>(`/artifacts/${encodeURIComponent(artifactId)}/versions/${vnum}/reject`, { reason }));
  const busy = save.isPending || validate.isPending || publish.isPending;

  const symbols = useMemo(() => (v ? symbolsOf(v) : []), [v]);
  const kind: ArtifactKind = v?.kind ?? (kindSection === 'ontologies' ? 'ontology' : kindSection === 'interpretations' ? 'interpretation' : 'dt_model');
  const language = kind === 'pt_model' || kind === 'dt_model' ? 'xml' : kind === 'interpretation' ? 'interpretation' : 'ontology';
  const ontologyRef = v?.kind === 'ontology' ? v.ref : v?.ontologyRef;
  const diagnostics = (save.data && !dirty ? save.data.diagnostics : v?.diagnostics) ?? [];
  const sectionLabel = kindSection === 'ontologies' ? 'Ontologies' : kindSection === 'interpretations' ? 'Interpretations' : 'Models';

  return (
    <div className="vts-page">
      <QueryState query={q} skeletonLines={10}>
        {(ver) => (
          <div className="stack">
            <PageHeader
              eyebrow={<Crumbs items={[{ label: 'Studio', to: '/studio' }, { label: sectionLabel, to: `/studio/${artifactSection(kind)}` }, { label: ver.name ?? artifactId, to: `/studio/${artifactSection(ver.kind)}/${encodeURIComponent(artifactId)}` }, { label: `v${ver.version}` }]} />}
              title={
                <span className="row-wrap">
                  {ver.name ?? artifactId} <span className="muted">v{ver.version}</span> <LifecycleBadge state={ver.state} />
                </span>
              }
              meta={
                <>
                  <span className="mono xsmall">{ver.ref}</span>
                  <HashChip value={ver.contentSha256} label="content hash" />
                  {ver.parentVersion && <span className="small">from <RefLink refId={`${artifactId}@${ver.parentVersion}`} kind={ver.kind} /></span>}
                  {ver.ontologyRef && <span className="small">over <RefLink refId={ver.ontologyRef} kind="ontology" /></span>}
                  {ver.validation && <OutcomeBadge outcome={ver.validation.outcome} verdict={`validation ${ver.validation.verdict}`} />}
                  {dirty && <span className="vts-badge vts-badge--warning">Unsaved changes</span>}
                  <LearnMore page="ontology-management.html#save-validate-check-refinement-analyze-impact-publish">Lifecycle &amp; diagnostics</LearnMore>
                </>
              }
              actions={
                editable ? (
                  <>
                    <Button
                      icon={<Save size={14} />}
                      variant={dirty ? 'primary' : 'secondary'}
                      disabled={!dirty || busy}
                      loading={save.isPending}
                      onClick={() => save.mutate(draft!, { onSuccess: () => { setDraft(null); void refreshVersion(); } })}
                    >
                      Save draft
                    </Button>
                    <Button icon={<CheckCircle2 size={14} />} disabled={dirty || busy} loading={validate.isPending} onClick={() => validate.mutate(undefined)} title={dirty ? 'Save first: validation applies to saved content' : undefined}>
                      Validate
                    </Button>
                    {ver.parentVersion && (
                      <Link className="vts-btn" to={`/studio/history?from=${encodeURIComponent(`${artifactId}@${ver.parentVersion}`)}&to=${encodeURIComponent(ver.ref)}`}>
                        <FileDiff size={14} /> Compare
                      </Link>
                    )}
                    {ver.kind === 'ontology' && (
                      <Button icon={<ShieldCheck size={14} />} disabled={dirty} onClick={() => setRefineOpen(true)}>Check refinement</Button>
                    )}
                    <Link className="vts-btn" to={`/studio/impact?ref=${encodeURIComponent(ver.ref)}`}>
                      <Network size={14} /> Analyze impact
                    </Link>
                    <Button
                      icon={<Upload size={14} />}
                      disabled={ver.state !== 'verified' || dirty || busy}
                      loading={publish.isPending}
                      title={ver.state !== 'verified' ? 'Only VERIFIED versions can be published: validate first' : undefined}
                      onClick={() => {
                        if (window.confirm(`Publish ${ver.ref}? Published versions are immutable.`)) publish.mutate(undefined);
                      }}
                    >
                      Publish
                    </Button>
                    <Button variant="danger" icon={<XCircle size={14} />} onClick={() => setRejectOpen(true)}>Reject</Button>
                  </>
                ) : (
                  <>
                    <Button variant="primary" icon={<GitPullRequest size={14} />} onClick={() => setNewDraftOpen(true)}>
                      Create draft from v{ver.version}
                    </Button>
                    <Link className="vts-btn" to={`/studio/impact?ref=${encodeURIComponent(ver.ref)}`}>
                      <Network size={14} /> Impact
                    </Link>
                  </>
                )
              }
            />
            {!editable && (
              <Callout tone="neutral" title={`This version is ${ver.state} and immutable`}>
                Historical packages and executions keep referring to exactly these bytes. To change it, create a new draft.
              </Callout>
            )}
            {[save.error, validate.error, publish.error, reject.error].filter(Boolean).map((e, i) => (
              <Callout key={i} tone="critical" title={e instanceof ApiError && e.isConflict ? 'Not allowed in the current lifecycle state' : 'The operation failed'}>
                {(e as Error).message}
              </Callout>
            ))}
            {validate.data && (
              <Callout tone={validate.data.state === 'verified' ? 'ok' : 'critical'} title={validate.data.state === 'verified' ? 'Validation passed — the version is VERIFIED' : 'Validation found errors — the version stays DRAFT'}>
                See the Validation tab for the full report (evidence {validate.data.validation?.id}).
              </Callout>
            )}
            <Tabs
              label="Version views"
              value={tab}
              onChange={setTab}
              tabs={[
                { id: 'source', label: editable ? 'Editor' : 'Source' },
                ...(ver.structure ? [{ id: 'structure' as Tab, label: 'Structure' }] : []),
                ...(ontologyRef ? [{ id: 'symbol' as Tab, label: `Symbol${symbol ? `: ${symbol}` : 's'}` }] : []),
                { id: 'dependencies', label: 'Dependencies' },
                ...(isOntology(ver.structure) ? [{ id: 'visual' as Tab, label: 'Visualisation' }] : []),
                ...(isInterpretation(ver.structure) ? [{ id: 'evaluate' as Tab, label: 'Evaluate' }] : []),
                { id: 'validation', label: 'Validation' },
                { id: 'history', label: 'History' },
              ]}
            />
            {tab === 'source' && (
              <div className="grid-main-side">
                <CodeEditor
                  value={ver.content}
                  resetKey={`${ver.ref}:${ver.contentSha256}`}
                  readOnly={!editable}
                  onChange={(t) => setDraft(t)}
                  language={language}
                  diagnostics={dirty ? [] : diagnostics}
                  symbols={symbols}
                  onGoToSymbol={(n) => setSymbol(n)}
                  reveal={reveal}
                  ariaLabel={`Source of ${ver.ref}`}
                  height={620}
                />
                <Panel title="Diagnostics" subtitle={dirty ? 'Save to re-check the edited text' : 'Strict parser (fast); run Validate for the aligner and Z3 checks'}>
                  {diagnostics.length === 0 ? (
                    <p className="small muted">No structural problems.</p>
                  ) : (
                    <ul className="vts-list">
                      {diagnostics.map((d, i) => (
                        <li key={i}>
                          <button type="button" className="vts-link-row" style={{ border: 0, background: 'none', width: '100%', textAlign: 'left', cursor: 'pointer' }} onClick={() => setReveal({ span: d.span, nonce: Date.now() })}>
                            <span className={`vts-badge vts-badge--${d.severity === 'error' ? 'critical' : d.severity === 'warning' ? 'warning' : 'neutral'}`}>{d.code}</span>
                            <span className="small">
                              {d.span.line ? `L${d.span.line}: ` : ''}
                              {d.message}
                            </span>
                          </button>
                        </li>
                      ))}
                    </ul>
                  )}
                  {engineering && <p className="xsmall subtle" style={{ marginTop: 8 }}>F12 or Cmd/Ctrl-click: go to definition · Cmd/Ctrl-F: search · Ctrl-Space: complete</p>}
                </Panel>
              </div>
            )}
            {tab === 'structure' && <StructurePanel v={ver} onReveal={(span) => { setTab('source'); setReveal({ span, nonce: Date.now() }); }} onSymbol={(s) => { setSymbol(s); setTab('symbol'); }} />}
            {tab === 'symbol' && ontologyRef && (
              <Panel title="Symbol cross references">
                <SymbolPanel ontologyRef={ontologyRef} name={symbol} onReveal={ver.kind === 'ontology' ? (span) => { setTab('source'); setReveal({ span, nonce: Date.now() }); } : undefined} />
              </Panel>
            )}
            {tab === 'dependencies' && <Panel title="Dependencies"><DependenciesPanel artifactId={artifactId} /></Panel>}
            {tab === 'visual' && isOntology(ver.structure) && <Panel title="Ontology structure"><OntologyGraph structure={ver.structure} onSymbol={(s) => { setSymbol(s); setTab('symbol'); }} /></Panel>}
            {tab === 'evaluate' && <Panel title="Evaluate against observations"><EvaluatePanel v={ver} /></Panel>}
            {tab === 'validation' && <Panel title="Validation evidence"><ValidationPanel v={ver} /></Panel>}
            {tab === 'history' && <Panel title="Version lineage"><LineagePanel artifactId={artifactId} current={ver.version} /></Panel>}
            <NewDraftDialog v={ver} open={newDraftOpen} onOpenChange={setNewDraftOpen} />
            {ver.kind === 'ontology' && <RefinementDialog v={ver} open={refineOpen} onOpenChange={setRefineOpen} />}
            <Dialog
              open={rejectOpen}
              onOpenChange={setRejectOpen}
              title={`Reject ${ver.ref}`}
              description="The version is kept for the record but can no longer be edited or published."
              footer={
                <>
                  <Button onClick={() => setRejectOpen(false)}>Cancel</Button>
                  <Button variant="danger" disabled={!rejectReason.trim()} loading={reject.isPending} onClick={() => reject.mutate(rejectReason.trim(), { onSuccess: () => { setRejectOpen(false); navigate(`/studio/${artifactSection(kind)}/${encodeURIComponent(artifactId)}`); } })}>
                    Reject version
                  </Button>
                </>
              }
            >
              <label className="vts-field">
                <span>Reason (required)</span>
                <textarea className="vts-textarea" value={rejectReason} onChange={(e) => setRejectReason(e.target.value)} />
              </label>
            </Dialog>
            <span className="sr-only">{kind}</span>
          </div>
        )}
      </QueryState>
    </div>
  );
}
