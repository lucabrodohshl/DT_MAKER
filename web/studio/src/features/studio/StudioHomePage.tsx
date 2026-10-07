/**
 * Studio home: the Twin Blueprints (all, drafts, published, verification required, recently
 * modified), the ways to start one (new, from a template, clone, import) and the templates,
 * then the engineering library behind them (changes to deployed twins, the shared formal
 * artefacts, recent engineering activity). States are the backend's: "verification required"
 * is a draft whose release gate does not pass for exactly its inputs.
 */
import { BookOpen, Copy, FileCode2, FileUp, GitPullRequest, LayoutTemplate, Lock, Plus, Search, ShieldCheck, Workflow } from 'lucide-react';
import { useState } from 'react';
import { Link, Navigate, useNavigate, useSearchParams } from 'react-router-dom';
import { blueprintRoute, useBlueprintTemplates, useBlueprints } from '@/api/blueprints';
import { api } from '@/api/client';
import { useArtifacts, useChanges, useEngineeringMutation, useTwin } from '@/api/queries';
import type { ArtifactKind, AssetDetail, BlueprintListItem, VersionDetail } from '@/api/types';
import { Button, Dialog, EmptyState, ErrorBlock, PageHeader, Panel, QueryState, StatusBadge, TimeStamp } from '@/design';
import { AuditTable } from '@/features/audit/EngineeringAuditPage';
import { NewChangeDialog } from '@/features/maintenance/ChangesPage';
import { versionRoute } from '@/features/common/links';
import { iconFor } from '@/features/blueprint/icons';
import '@/features/blueprint/blueprint.css';
import '@/features/twins/twins.css';

const KIND_LABEL: Record<ArtifactKind, string> = { ontology: 'Ontology', interpretation: 'Interpretation', pt_model: 'PT view (model)', dt_model: 'DT view (model)' };

function kindFromFile(name: string): ArtifactKind | null {
  if (name.endsWith('.ont')) return 'ontology';
  if (name.endsWith('.interp')) return 'interpretation';
  if (name.endsWith('.xml')) return /(^|[-_.])pt([-_.]|$)/i.test(name) ? 'pt_model' : 'dt_model';
  return null;
}

/** New artefact: create one from scratch or from a file. Its first version is a DRAFT. */
function NewArtifactDialog({ open, onOpenChange, importMode }: { open: boolean; onOpenChange: (o: boolean) => void; importMode: boolean }) {
  const navigate = useNavigate();
  const [kind, setKind] = useState<ArtifactKind>('ontology');
  const [id, setId] = useState('');
  const [name, setName] = useState('');
  const [content, setContent] = useState('');
  const [ontologyRef, setOntologyRef] = useState('');
  const ontologies = useArtifacts('ontology');
  const create = useEngineeringMutation((b: Record<string, unknown>) => api.post<VersionDetail>('/artifacts', b));
  const onFile = async (f: File | undefined) => {
    if (!f) return;
    const k = kindFromFile(f.name);
    if (k) setKind(k);
    const base = f.name.replace(/\.[^.]+$/, '').toLowerCase().replace(/[^a-z0-9._-]+/g, '-');
    if (!id) setId(base);
    if (!name) setName(f.name);
    setContent(await f.text());
  };
  return (
    <Dialog open={open} onOpenChange={onOpenChange} wide title={importMode ? 'Import an artefact' : 'New artefact'}
      description="Its first version is a DRAFT: save, validate, check and release it through a change before any twin uses it.">
      <div className="stack">
        {importMode && (
          <label className="vts-field"><span>File (.ont, .interp, UPPAAL .xml)</span>
            <input type="file" accept=".ont,.interp,.xml" onChange={(e) => void onFile(e.target.files?.[0])} />
          </label>
        )}
        <div className="grid-3">
          <label className="vts-field"><span>Kind</span>
            <select className="vts-select" value={kind} onChange={(e) => setKind(e.target.value as ArtifactKind)}>
              {(Object.keys(KIND_LABEL) as ArtifactKind[]).map((k) => <option key={k} value={k}>{KIND_LABEL[k]}</option>)}
            </select>
          </label>
          <label className="vts-field"><span>Identifier</span><input className="vts-input" value={id} onChange={(e) => setId(e.target.value)} placeholder="e.g. hvac-domain" /></label>
          <label className="vts-field"><span>Name</span><input className="vts-input" value={name} onChange={(e) => setName(e.target.value)} /></label>
        </div>
        {kind === 'interpretation' && (
          <label className="vts-field"><span>Interprets over ontology version</span>
            <select className="vts-select" value={ontologyRef} onChange={(e) => setOntologyRef(e.target.value)}>
              <option value="">Choose…</option>
              {(ontologies.data ?? []).flatMap((a) => [a.published, a.open].filter((v): v is NonNullable<typeof v> => !!v)).map((v) => <option key={v.ref} value={v.ref}>{v.ref} ({v.state})</option>)}
            </select>
          </label>
        )}
        {!importMode && (
          <label className="vts-field"><span>Initial content (optional; edit it in the editor afterwards)</span>
            <textarea className="vts-textarea mono" rows={6} value={content} onChange={(e) => setContent(e.target.value)} placeholder={kind === 'ontology' ? 'sort Temperature\nfun t : Temperature\naxiom a : (> t 0)' : ''} />
          </label>
        )}
        {create.error && <ErrorBlock error={create.error} compact />}
        <div className="row">
          <div className="grow" />
          <Button onClick={() => onOpenChange(false)}>Cancel</Button>
          <Button variant="primary" disabled={!id.trim() || !name.trim() || (kind === 'interpretation' && !ontologyRef) || (importMode && !content)} loading={create.isPending}
            onClick={() => create.mutate(
              { kind, id: id.trim(), name: name.trim(), content, ...(kind === 'interpretation' ? { refs: { ontology: ontologyRef } } : {}) },
              { onSuccess: (v) => { onOpenChange(false); navigate(versionRoute(v.kind, v.ref)); } },
            )}>
            {importMode ? 'Import as draft' : 'Create draft'}
          </Button>
        </div>
      </div>
    </Dialog>
  );
}

/** New asset definition (the "Create in Studio" path of Add asset). */
function NewAssetPanel({ twinId }: { twinId: string }) {
  const twin = useTwin(twinId);
  const navigate = useNavigate();
  const [f, setF] = useState({ id: '', name: '', type: '', description: '' });
  const create = useEngineeringMutation((b: Record<string, unknown>) => api.post<AssetDetail>('/assets', b));
  const parent = twin.data?.assetId ?? '';
  return (
    <Panel title="New asset definition" subtitle={twin.data ? `Created under ${twin.data.name}'s root asset (${parent}); it has no data source until one is bound` : undefined}>
      <div className="stack">
        <div className="grid-3">
          <label className="vts-field"><span>Identifier</span><input className="vts-input" value={f.id} onChange={(e) => setF({ ...f, id: e.target.value })} placeholder="e.g. pump-p101-seal" /></label>
          <label className="vts-field"><span>Name</span><input className="vts-input" value={f.name} onChange={(e) => setF({ ...f, name: e.target.value })} /></label>
          <label className="vts-field"><span>Type</span><input className="vts-input" value={f.type} onChange={(e) => setF({ ...f, type: e.target.value })} placeholder="e.g. MechanicalSeal" /></label>
        </div>
        <label className="vts-field"><span>Description</span><input className="vts-input" value={f.description} onChange={(e) => setF({ ...f, description: e.target.value })} /></label>
        {create.error && <ErrorBlock error={create.error} compact />}
        <div className="row"><div className="grow" />
          <Button variant="primary" disabled={!f.id.trim() || !f.name.trim() || !f.type.trim() || !parent} loading={create.isPending}
            onClick={() => create.mutate({ ...f, id: f.id.trim(), parentId: parent }, { onSuccess: () => navigate(`/twins/${encodeURIComponent(twinId)}/assets/${encodeURIComponent(f.id.trim())}`) })}>
            Create asset
          </Button>
        </div>
      </div>
    </Panel>
  );
}

type Shelf = 'all' | 'drafts' | 'published' | 'verify' | 'recent';

function BlueprintCard({ b }: { b: BlueprintListItem }) {
  const open = b.draft ?? b.latest;
  const to = open ? blueprintRoute(b.id, open.version) : `/studio/blueprints/${encodeURIComponent(b.id)}`;
  return (
    <article className="vts-bp-card" aria-label={b.name}>
      <Link to={to} className="vts-bp-card__open" aria-label={`Open ${b.name}`} />
      <div className="vts-bp-card__top">
        <div className="vts-bp-card__icon">{iconFor(b.icon)}</div>
        <div className="stack-sm" style={{ gap: 2, minWidth: 0 }}>
          <h3 className="truncate">{b.name}</h3>
          <span className="xsmall subtle truncate">
            {b.domain} · <span className="mono">{b.id}</span>
          </span>
        </div>
      </div>
      {b.description && (
        <p className="small muted" style={{ margin: 0, display: '-webkit-box', WebkitLineClamp: 2, WebkitBoxOrient: 'vertical', overflow: 'hidden' }}>
          {b.description}
        </p>
      )}
      <div className="row-wrap" style={{ gap: 6 }}>
        {b.published && <StatusBadge tone="ok" icon={Lock} label={`PUBLISHED v${b.published.version}`} />}
        {b.draft && <StatusBadge tone="info" label={`DRAFT v${b.draft.version}`} />}
        {b.draft && b.draftReadiness === 'ready' && <StatusBadge tone="formal" icon={ShieldCheck} label="READY TO RELEASE" />}
        {b.draft && b.draftReadiness === 'blocked' && <StatusBadge tone="warning" label={`VERIFICATION REQUIRED · ${b.draftBlockers ?? 0}`} title="Release gate items not passing for the draft" />}
        {(b.draftErrors ?? 0) > 0 && <StatusBadge tone="critical" label={`${b.draftErrors} error(s)`} />}
      </div>
      <div className="xsmall subtle">
        {b.instanceCount} instance{b.instanceCount === 1 ? '' : 's'} · updated <TimeStamp value={b.updatedAt} relative />
      </div>
      <div className="vts-bp-card__actions">
        {b.draft && (
          <Link to={blueprintRoute(b.id, b.draft.version)} className="vts-btn vts-btn--sm vts-btn--primary">
            Open draft
          </Link>
        )}
        {b.published && (
          <Link to={blueprintRoute(b.id, b.published.version)} className="vts-btn vts-btn--sm vts-btn--secondary">
            v{b.published.version}
          </Link>
        )}
        {b.published && (
          <Link to={blueprintRoute(b.id, b.published.version, 'release/instances')} className="vts-btn vts-btn--sm vts-btn--ghost">
            Instances
          </Link>
        )}
        <Link to={`/studio/new?mode=clone&from=${encodeURIComponent(b.id)}`} className="vts-btn vts-btn--sm vts-btn--ghost">
          <Copy size={13} aria-hidden="true" /> Clone
        </Link>
      </div>
    </article>
  );
}

export default function StudioHomePage() {
  const [params, setParams] = useSearchParams();
  const start = params.get('start');
  const twinId = params.get('twin');
  const navigate = useNavigate();
  const blueprints = useBlueprints();
  const templates = useBlueprintTemplates();
  const changes = useChanges('open');
  const artifacts = useArtifacts();
  const [newChange, setNewChange] = useState(false);
  const [artifactDialog, setArtifactDialog] = useState<'new' | 'import' | null>(start === 'import-artifact' ? 'import' : null);
  const shelf = (params.get('shelf') as Shelf | null) ?? 'all';
  const q = params.get('q') ?? '';
  const [now] = useState(() => Date.now());
  const clearStart = () => {
    const p = new URLSearchParams(params);
    p.delete('start');
    setParams(p, { replace: true });
  };
  const set = (k: string, v: string) => {
    const p = new URLSearchParams(params);
    if (v) p.set(k, v);
    else p.delete(k);
    setParams(p, { replace: true });
  };
  // Earlier entry points (?start=create|import|instantiate) now lead to the Blueprint flows.
  if (start === 'create' || start === 'import' || start === 'instantiate') {
    const to = start === 'create' ? '/studio/new' : start === 'import' ? '/studio/new?mode=import' : '/twins?create=instantiate';
    return <Navigate to={to} replace />;
  }
  const list = blueprints.data ?? [];
  const recentCut = now - 7 * 24 * 3600 * 1000;
  const shelves: { id: Shelf; label: string; test: (b: BlueprintListItem) => boolean }[] = [
    { id: 'all', label: 'All Blueprints', test: () => true },
    { id: 'drafts', label: 'Drafts', test: (b) => !!b.draft },
    { id: 'published', label: 'Published', test: (b) => !!b.published },
    { id: 'verify', label: 'Verification required', test: (b) => !!b.draft && b.draftReadiness !== 'ready' },
    { id: 'recent', label: 'Recently modified', test: (b) => new Date(b.updatedAt).getTime() >= recentCut },
  ];
  const active = shelves.find((s) => s.id === shelf) ?? shelves[0]!;
  const shown = list
    .filter(active.test)
    .filter((b) => !q || `${b.name} ${b.id} ${b.domain} ${b.description}`.toLowerCase().includes(q.toLowerCase()))
    .sort((a, b) => (shelf === 'recent' ? b.updatedAt.localeCompare(a.updatedAt) : a.name.localeCompare(b.name)));

  return (
    <div className="vts-page stack">
      <PageHeader
        eyebrow="Studio"
        title="Twin Blueprints"
        meta={<span>Design, verify and release Digital Twin types; create and deploy their instances.</span>}
        actions={
          <>
            <Button icon={<FileUp size={14} />} onClick={() => navigate('/studio/new?mode=import')}>
              Import
            </Button>
            <Button icon={<Copy size={14} />} onClick={() => navigate('/studio/new?mode=clone')}>
              Clone existing
            </Button>
            <Button icon={<LayoutTemplate size={14} />} onClick={() => navigate('/studio/new?mode=template')}>
              From template
            </Button>
            <Button variant="primary" icon={<Plus size={14} />} onClick={() => navigate('/studio/new')}>
              New Blueprint
            </Button>
          </>
        }
      />

      {start === 'asset' && twinId && <NewAssetPanel twinId={twinId} />}

      <div className="vts-lib__tools" role="search">
        <label className="vts-lib__search">
          <Search size={15} aria-hidden="true" />
          <input type="search" value={q} onChange={(e) => set('q', e.target.value)} placeholder="Search Blueprints…" aria-label="Search Blueprints" />
        </label>
        <div className="row-wrap" role="tablist" aria-label="Blueprint shelves">
          {shelves.map((s) => (
            <button key={s.id} type="button" role="tab" aria-selected={shelf === s.id} className="vts-chip" aria-pressed={shelf === s.id} onClick={() => set('shelf', s.id === 'all' ? '' : s.id)}>
              {s.label} <span className="vts-tag">{list.filter(s.test).length}</span>
            </button>
          ))}
        </div>
      </div>

      <QueryState
        query={blueprints}
        isEmpty={(d) => d.length === 0}
        empty={
          <EmptyState title="No Blueprints yet" action={<Button variant="primary" icon={<Plus size={14} />} onClick={() => navigate('/studio/new')}>New Blueprint</Button>}>
            A Blueprint defines a type of twin. Start blank, from a template, or import the formal models of an existing twin.
          </EmptyState>
        }
      >
        {() =>
          shown.length === 0 ? (
            <EmptyState compact title="Nothing on this shelf">{q ? 'No Blueprint matches the search.' : 'Choose another shelf.'}</EmptyState>
          ) : (
            <div className="vts-bp-cards">
              {shown.map((b) => (
                <BlueprintCard key={b.id} b={b} />
              ))}
            </div>
          )
        }
      </QueryState>

      <Panel title="Templates" subtitle="Domain starting points: asset types, world palette, simulator and data contract">
        <QueryState query={templates} isEmpty={(d) => d.length === 0} empty={<EmptyState compact title="No templates installed" />}>
          {(list2) => (
            <div className="vts-bp-cards">
              {list2.map((t) => (
                <article key={t.id} className="vts-bp-card" aria-label={t.name}>
                  <Link to={`/studio/new?mode=template&template=${encodeURIComponent(t.id)}`} className="vts-bp-card__open" aria-label={`Create a Blueprint from ${t.name}`} />
                  <div className="vts-bp-card__top">
                    <div className="vts-bp-card__icon">{iconFor(t.icon)}</div>
                    <div className="stack-sm" style={{ gap: 2, minWidth: 0 }}>
                      <h3>{t.name}</h3>
                      <span className="xsmall subtle">{t.domain}</span>
                    </div>
                  </div>
                  <p className="small muted" style={{ margin: 0 }}>
                    {t.description}
                  </p>
                  {t.includes.length > 0 && <span className="xsmall subtle">{t.includes.join(' · ')}</span>}
                </article>
              ))}
            </div>
          )}
        </QueryState>
      </Panel>

      <h2 style={{ margin: 'var(--s-4) 0 0', fontSize: 'var(--text-lg)' }}>Engineering library</h2>
      <div className="grid-main-side">
        <Panel
          title="Changes in progress"
          actions={
            <>
              <Button size="sm" variant="ghost" icon={<GitPullRequest size={13} />} onClick={() => setNewChange(true)}>
                New change
              </Button>
              <Link className="small" to="/studio/changes">
                All changes
              </Link>
            </>
          }
          flush
        >
          <QueryState query={changes} isEmpty={(d) => d.length === 0} empty={<EmptyState compact title="No change in progress">Changes evolve the artefacts of deployed twins through verification.</EmptyState>}>
            {(cl) => (
              <ul className="vts-list" style={{ padding: '0 var(--s-4)' }}>
                {cl.map((c) => (
                  <li key={c.id} className="row-wrap small">
                    <Link to={`/studio/changes/${c.id}`} className="strong">
                      {c.title}
                    </Link>
                    <span className="muted">{c.twinId}</span>
                    <span className="mono xsmall">{c.artifacts.join(', ')}</span>
                    <span className="grow" />
                    <span className="xsmall subtle">
                      opened <TimeStamp value={c.createdAt} relative />
                    </span>
                  </li>
                ))}
              </ul>
            )}
          </QueryState>
        </Panel>
        <Panel
          title="Formal artefacts"
          actions={
            <>
              <Button size="sm" variant="ghost" icon={<FileUp size={13} />} onClick={() => setArtifactDialog('import')}>
                Import
              </Button>
              <Button size="sm" variant="ghost" icon={<Plus size={13} />} onClick={() => setArtifactDialog('new')}>
                New
              </Button>
            </>
          }
        >
          <QueryState query={artifacts}>
            {(al) => (
              <div className="stack-sm">
                {([['models', Workflow, ['pt_model', 'dt_model']], ['ontologies', BookOpen, ['ontology']], ['interpretations', FileCode2, ['interpretation']]] as const).map(([section, Icon, kinds]) => {
                  const items = al.filter((a) => (kinds as readonly string[]).includes(a.kind));
                  const drafts = items.filter((a) => a.open).length;
                  return (
                    <Link key={section} to={`/studio/${section}`} className="row small" style={{ gap: 8 }}>
                      <Icon size={15} aria-hidden="true" /> <strong style={{ textTransform: 'capitalize' }}>{section}</strong>
                      <span className="muted">{items.length}</span>
                      {drafts > 0 && <StatusBadge tone="info" label={`${drafts} open draft${drafts === 1 ? '' : 's'}`} />}
                    </Link>
                  );
                })}
                <span className="xsmall subtle">Shared, versioned artefacts; Blueprints pin exact versions of them.</span>
              </div>
            )}
          </QueryState>
        </Panel>
      </div>
      <Panel title="Recent engineering activity" actions={<Link className="small" to="/studio/audit">Engineering audit</Link>} flush>
        <AuditTable />
      </Panel>
      <NewChangeDialog open={newChange} onOpenChange={setNewChange} defaultTwin={twinId ?? undefined} />
      <NewArtifactDialog
        open={artifactDialog !== null}
        onOpenChange={(o) => {
          if (!o) {
            setArtifactDialog(null);
            clearStart();
          }
        }}
        importMode={artifactDialog === 'import'}
      />
    </div>
  );
}
