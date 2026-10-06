/**
 * Studio home: what is being changed (open changes), the definitions under engineering
 * control, and the entry points used across the product ("Create in Studio", "Import",
 * "Add asset → Create in Studio"). Only operations the platform actually supports are offered.
 */
import { BookOpen, Boxes, FileCode2, FileUp, GitPullRequest, Plus, Workflow } from 'lucide-react';
import { useState } from 'react';
import { Link, useNavigate, useSearchParams } from 'react-router-dom';
import { api } from '@/api/client';
import { useArtifacts, useChanges, useEngineeringMutation, useTwin } from '@/api/queries';
import type { ArtifactKind, AssetDetail, VersionDetail } from '@/api/types';
import { Button, Callout, Dialog, EmptyState, ErrorBlock, LifecycleBadge, PageHeader, Panel, QueryState, StatusBadge, TimeStamp } from '@/design';
import { AuditTable } from '@/features/audit/EngineeringAuditPage';
import { NewChangeDialog } from '@/features/maintenance/ChangesPage';
import { versionRoute } from '@/features/common/links';

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

export default function StudioHomePage() {
  const [params, setParams] = useSearchParams();
  const start = params.get('start');
  const twinId = params.get('twin');
  const changes = useChanges('open');
  const artifacts = useArtifacts();
  const [newChange, setNewChange] = useState(false);
  const [artifactDialog, setArtifactDialog] = useState<'new' | 'import' | null>(start === 'import' ? 'import' : start === 'create' ? 'new' : null);
  const clearStart = () => { const p = new URLSearchParams(params); p.delete('start'); setParams(p, { replace: true }); };

  return (
    <div className="vts-page stack">
      <PageHeader
        eyebrow="Studio"
        title="Studio"
        meta={<span>Author and evolve twin definitions: models, ontologies and interpretations, verified and released through changes</span>}
        actions={
          <>
            <Button icon={<FileUp size={14} />} onClick={() => setArtifactDialog('import')}>Import artefact</Button>
            <Button icon={<Plus size={14} />} onClick={() => setArtifactDialog('new')}>New artefact</Button>
            <Button variant="primary" icon={<GitPullRequest size={14} />} onClick={() => setNewChange(true)}>New change</Button>
          </>
        }
      />

      {start === 'asset' && twinId && <NewAssetPanel twinId={twinId} />}
      {start === 'instantiate' && (
        <Callout tone="info" title="Instantiating an existing twin type">
          Released packages are listed under <Link to="/studio/packages">Packages</Link> and can be deployed to an existing twin from
          <Link to="/studio/deployments"> Deployments</Link>. Registering a brand-new twin instance (its own asset, runtime and data
          sources) is done with <span className="mono">twin-studio seed &lt;example&gt;</span> in this version; in-app registration is not available yet.
          <div style={{ marginTop: 8 }}><Button size="sm" onClick={clearStart}>Dismiss</Button></div>
        </Callout>
      )}
      {start === 'create' && (
        <Callout tone="info" title="Creating a new twin">
          A twin is defined by four kinds of artefact: the PT and DT views (timed automata), an ontology and two interpretations.
          Create or import each one here; a change then validates them, checks alignment, compiles the DT view and builds a verified
          package. Registering the new twin instance itself uses <span className="mono">twin-studio seed</span> in this version.
          <div style={{ marginTop: 8 }}><Button size="sm" onClick={clearStart}>Dismiss</Button></div>
        </Callout>
      )}

      <div className="grid-main-side">
        <Panel title="Changes in progress" actions={<Link className="small" to="/studio/changes">All changes</Link>} flush>
          <QueryState query={changes} isEmpty={(d) => d.length === 0} empty={<EmptyState compact title="No change in progress" action={<Button size="sm" onClick={() => setNewChange(true)}>Open a change</Button>} />}>
            {(list) => (
              <ul className="vts-list" style={{ padding: '0 var(--s-4)' }}>
                {list.map((c) => (
                  <li key={c.id} className="row-wrap small">
                    <Link to={`/studio/changes/${c.id}`} className="strong">{c.title}</Link>
                    <span className="muted">{c.twinId}</span>
                    <span className="mono xsmall">{c.artifacts.join(', ')}</span>
                    <span className="grow" />
                    <span className="xsmall subtle">opened <TimeStamp value={c.createdAt} relative /></span>
                  </li>
                ))}
              </ul>
            )}
          </QueryState>
        </Panel>
        <Panel title="Definitions">
          <QueryState query={artifacts}>
            {(list) => (
              <div className="stack-sm">
                {([['models', Workflow, ['pt_model', 'dt_model']], ['ontologies', BookOpen, ['ontology']], ['interpretations', FileCode2, ['interpretation']]] as const).map(([section, Icon, kinds]) => {
                  const items = list.filter((a) => (kinds as readonly string[]).includes(a.kind));
                  const drafts = items.filter((a) => a.open).length;
                  return (
                    <Link key={section} to={`/studio/${section}`} className="row small" style={{ gap: 8 }}>
                      <Icon size={15} aria-hidden="true" /> <strong style={{ textTransform: 'capitalize' }}>{section}</strong>
                      <span className="muted">{items.length}</span>
                      {drafts > 0 && <StatusBadge tone="info" label={`${drafts} open draft${drafts === 1 ? '' : 's'}`} />}
                    </Link>
                  );
                })}
                <Link to="/twins" className="row small" style={{ gap: 8 }}><Boxes size={15} aria-hidden="true" /> <strong>Twins</strong> <span className="muted">operate deployed twins</span></Link>
              </div>
            )}
          </QueryState>
        </Panel>
      </div>
      <Panel title="Open drafts" flush>
        <QueryState query={artifacts} isEmpty={(d) => !d.some((a) => a.open)} empty={<EmptyState compact title="No open drafts" />}>
          {(list) => (
            <ul className="vts-list" style={{ padding: '0 var(--s-4)' }}>
              {list.filter((a) => a.open).map((a) => (
                <li key={a.id} className="row-wrap small">
                  <Link to={versionRoute(a.kind, a.open!.ref)} className="mono">{a.open!.ref}</Link>
                  <span>{a.name}</span>
                  <LifecycleBadge state={a.open!.state} />
                  <span className="grow" />
                  <span className="xsmall subtle">{a.open!.changeDescription}</span>
                </li>
              ))}
            </ul>
          )}
        </QueryState>
      </Panel>
      <Panel title="Recent engineering activity" actions={<Link className="small" to="/studio/audit">Engineering audit</Link>} flush>
        <AuditTable />
      </Panel>
      <NewChangeDialog open={newChange} onOpenChange={setNewChange} defaultTwin={twinId ?? undefined} />
      <NewArtifactDialog open={artifactDialog !== null} onOpenChange={(o) => { if (!o) { setArtifactDialog(null); clearStart(); } }} importMode={artifactDialog === 'import'} />
    </div>
  );
}
