/**
 * Release → Instances: the concrete twins created from this Blueprint. An instance is created from
 * a PUBLISHED version: it gets its own assets (instance scope; ids derived from the instance id
 * unless given), is bound to shared context assets, receives its identity values (per-instance
 * properties such as a serial number), a placement in the asset hierarchy and a deployment
 * target. The Blueprint stays the single definition: instances never copy its formal artefacts.
 */
import { ArrowUpCircle, Boxes, ExternalLink, Plus, Rocket } from 'lucide-react';
import { useMemo, useState } from 'react';
import { Link, useNavigate } from 'react-router-dom';
import { blueprintApi, blueprintRoute, useBlueprint, useBlueprintMutation, useInstances } from '@/api/blueprints';
import type { InstanceView } from '@/api/types';
import { Button, Callout, Dialog, EmptyState, ErrorBlock, Skeleton, StatusBadge } from '@/design';
import { useEditor } from '../editor';
import { EdPage, Pane, SLUG } from '../ui';

export function runtimeTone(s: string | undefined): 'ok' | 'info' | 'neutral' | 'critical' {
  return s === 'running' ? 'ok' : s === 'starting' ? 'info' : s === 'failed' ? 'critical' : 'neutral';
}

export function RuntimeBadge({ i }: { i: InstanceView }) {
  const s = i.runtime?.state ?? 'not_started';
  return <StatusBadge tone={runtimeTone(s)} label={s === 'running' ? 'DEPLOYED · RUNNING' : s === 'not_started' ? 'NOT DEPLOYED' : s.replace('_', ' ').toUpperCase()} spin={s === 'starting'} title={i.runtime?.message} />;
}

function CreateInstanceDialog({ open, onOpenChange }: { open: boolean; onOpenChange: (o: boolean) => void }) {
  const e = useEditor();
  const navigate = useNavigate();
  const bp = useBlueprint(e.id);
  const published = useMemo(() => (bp.data?.versions ?? []).filter((v) => v.state === 'published').sort((a, b) => b.version - a.version), [bp.data]);
  const [version, setVersion] = useState<number | null>(null);
  const v = version ?? (e.detail.state === 'published' ? e.version : published[0]?.version ?? null);
  const doc = e.doc;
  const [id, setId] = useState('');
  const [name, setName] = useState('');
  const [description, setDescription] = useState('');
  const [assetIds, setAssetIds] = useState<Record<string, string>>({});
  const [identity, setIdentity] = useState<Record<string, string>>({});
  const [parent, setParent] = useState('');
  const [paused, setPaused] = useState(false);
  const [created, setCreated] = useState<InstanceView | null>(null);
  const create = useBlueprintMutation((body: Record<string, unknown>) => blueprintApi.createInstance(body));
  const deploy = useBlueprintMutation((iid: string) => blueprintApi.deployInstance(iid));
  const root = doc.structure.root;
  const idOk = SLUG.test(id);
  const defaultAsset = (aid: string, scope: string) => (scope === 'context' ? aid : aid === root ? id || '<instance id>' : `${id || '<instance id>'}-${aid}`);
  const perInstance = doc.data.properties.filter((p) => p.perInstance);
  const reset = () => {
    setId('');
    setName('');
    setDescription('');
    setAssetIds({});
    setIdentity({});
    setParent('');
    setCreated(null);
    create.reset();
    deploy.reset();
  };
  const submit = () => {
    const ids = Object.fromEntries(Object.entries(assetIds).filter(([, x]) => x.trim() !== ''));
    create.mutate(
      {
        blueprintId: e.id,
        version: v,
        id,
        name: name || id,
        ...(description ? { description } : {}),
        assetIds: ids,
        identity,
        placement: parent ? { parentAssetId: parent } : {},
        target: { kind: 'local', ...(doc.identity.runtimeMode === 'cosimulation' ? { paused } : {}) },
      },
      { onSuccess: (r) => setCreated(r) },
    );
  };
  return (
    <Dialog
      open={open}
      onOpenChange={(o) => {
        if (!o) reset();
        onOpenChange(o);
      }}
      wide
      title={created ? `Instance ${created.name} created` : 'New instance'}
      description={created ? 'It is not running yet: deploy it to start its runtime.' : `A concrete ${doc.identity.name} twin, created from a published version of this Blueprint.`}
      footer={
        created ? (
          <>
            <Button
              onClick={() => {
                reset();
                onOpenChange(false);
              }}
            >
              Close
            </Button>
            <Button
              variant="primary"
              icon={<Rocket size={14} />}
              loading={deploy.isPending}
              onClick={() =>
                deploy.mutate(created.id, {
                  onSuccess: () => {
                    onOpenChange(false);
                    navigate(blueprintRoute(e.id, e.version, 'release/deployment'));
                  },
                })
              }
            >
              Deploy now
            </Button>
          </>
        ) : (
          <>
            <Button onClick={() => onOpenChange(false)}>Cancel</Button>
            <Button variant="primary" icon={<Plus size={14} />} loading={create.isPending} disabled={!idOk || v === null} onClick={submit}>
              Create instance
            </Button>
          </>
        )
      }
    >
      {created ? (
        <div className="stack-sm small">
          <p style={{ margin: 0 }}>
            <strong>{created.name}</strong> ({created.id}) runs v{created.blueprintVersion}. {Object.keys((created.instanceConfig?.assetMap as Record<string, string>) ?? {}).length} assets were created or bound.
          </p>
          {deploy.isError && <ErrorBlock error={deploy.error} compact />}
        </div>
      ) : published.length === 0 ? (
        <Callout tone="warning" title="No published version yet">
          Instances are created from published versions only. Publish this version in <Link to={blueprintRoute(e.id, e.version, 'release/package')}>Release → Package</Link>.
        </Callout>
      ) : (
        <div className="stack">
          <div className="vts-fgrid">
            <label className="vts-f">
              <span>Version</span>
              <select className="vts-select" value={v ?? ''} onChange={(x) => setVersion(Number(x.target.value))}>
                {published.map((p) => (
                  <option key={p.version} value={p.version}>
                    v{p.version} (published){p.note ? ` — ${p.note}` : ''}
                  </option>
                ))}
              </select>
              {v !== e.version && <span className="vts-f__hint">The form below shows this workspace&apos;s structure (v{e.version}).</span>}
            </label>
            <label className="vts-f">
              <span>Instance id</span>
              <input className={`vts-input mono${id && !idOk ? ' is-invalid' : ''}`} value={id} onChange={(x) => setId(x.target.value.trim())} placeholder="pump-p102-dt" autoFocus />
              {id && !idOk ? <span className="vts-f__error">Lower-case letters, digits and dashes.</span> : <span className="vts-f__hint">Also the id of the root asset.</span>}
            </label>
            <label className="vts-f">
              <span>Display name</span>
              <input className="vts-input" value={name} onChange={(x) => setName(x.target.value)} placeholder="Pump P-102" />
            </label>
            <label className="vts-f">
              <span>Description</span>
              <input className="vts-input" value={description} onChange={(x) => setDescription(x.target.value)} />
            </label>
          </div>
          {perInstance.length > 0 && (
            <fieldset className="vts-f" style={{ border: 0, padding: 0, margin: 0 }}>
              <legend className="vts-label">Identity</legend>
              <div className="vts-fgrid">
                {perInstance.map((p) => (
                  <label key={p.id} className="vts-f">
                    <span>{p.label || p.id}</span>
                    <input className="vts-input" value={identity[p.id] ?? ''} onChange={(x) => setIdentity((m) => ({ ...m, [p.id]: x.target.value }))} />
                  </label>
                ))}
              </div>
            </fieldset>
          )}
          <table className="vts-table">
            <caption className="small" style={{ textAlign: 'left' }}>
              Assets of the instance
            </caption>
            <thead>
              <tr>
                <th scope="col">Blueprint asset</th>
                <th scope="col">Scope</th>
                <th scope="col">Concrete asset id</th>
              </tr>
            </thead>
            <tbody>
              {doc.structure.assets.map((a) => (
                <tr key={a.id}>
                  <td className="small">
                    {a.name} <span className="mono xsmall subtle">{a.id}</span>
                  </td>
                  <td className="xsmall">{a.scope === 'context' ? 'context (shared, bound)' : a.id === root ? 'instance · root' : 'instance (created)'}</td>
                  <td>
                    <input className="vts-input mono" value={assetIds[a.id] ?? ''} placeholder={defaultAsset(a.id, a.scope)} onChange={(x) => setAssetIds((m) => ({ ...m, [a.id]: x.target.value.trim() }))} aria-label={`Concrete id of ${a.id}`} />
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
          <div className="vts-fgrid">
            <label className="vts-f">
              <span>Placement: parent asset (optional)</span>
              <input className="vts-input mono" value={parent} onChange={(x) => setParent(x.target.value.trim())} placeholder="site-north" />
              <span className="vts-f__hint">Where the root asset hangs in the estate hierarchy.</span>
            </label>
            <div className="vts-f">
              <span>Deployment target</span>
              <span className="small">Local supervisor (this Studio host)</span>
              {doc.identity.runtimeMode === 'cosimulation' && (
                <label className="vts-check small">
                  <input type="checkbox" checked={paused} onChange={(x) => setPaused(x.target.checked)} /> Start the co-simulation paused
                </label>
              )}
            </div>
          </div>
          <p className="xsmall subtle" style={{ margin: 0 }}>
            Data sources and bindings come from the Blueprint&apos;s connectivity ({doc.connectivity.sources.map((s) => s.name || s.id).join(', ') || 'none'}).
          </p>
          {create.isError && <ErrorBlock error={create.error} compact />}
        </div>
      )}
    </Dialog>
  );
}

export default function InstancesPage() {
  const e = useEditor();
  const q = useInstances(e.id);
  const [open, setOpen] = useState(false);
  const deploy = useBlueprintMutation((x: { id: string; version?: number }) => blueprintApi.deployInstance(x.id, x.version ? { version: x.version } : {}));
  return (
    <EdPage
      title="Instances"
      wide
      description="Concrete twins created from this Blueprint, and the version each one runs."
      actions={
        <Button size="sm" variant="primary" icon={<Plus size={14} />} onClick={() => setOpen(true)}>
          New instance
        </Button>
      }
      guide={<>An instance is one real asset&apos;s twin — Pump P-101, Drone D-7, Chamber TC-1. It is created from a published version, gets its own assets and identity, and is deployed (its runtime started) separately.</>}
    >
      {q.isPending ? (
        <Skeleton lines={4} />
      ) : q.isError ? (
        <ErrorBlock error={q.error} onRetry={() => void q.refetch()} />
      ) : q.data.length === 0 ? (
        <EmptyState icon={<Boxes size={28} />} title="No instances yet" action={<Button variant="primary" onClick={() => setOpen(true)}>Create the first instance</Button>}>
          Publish a version, then create an instance of it for each real asset.
        </EmptyState>
      ) : (
        <Pane flush>
          <table className="vts-table">
            <caption className="sr-only">Instances of this Blueprint</caption>
            <thead>
              <tr>
                <th scope="col">Instance</th>
                <th scope="col">Version</th>
                <th scope="col">Assets</th>
                <th scope="col">Runtime</th>
                <th scope="col" aria-label="Actions" />
              </tr>
            </thead>
            <tbody>
              {q.data.map((i) => {
                const map = (i.instanceConfig?.assetMap as Record<string, string> | undefined) ?? {};
                return (
                  <tr key={i.id}>
                    <td>
                      <strong className="small">{i.name}</strong> <span className="mono xsmall subtle">{i.id}</span>
                      {i.description && <div className="xsmall muted">{i.description}</div>}
                    </td>
                    <td className="small">
                      <Link to={blueprintRoute(e.id, i.blueprintVersion ?? 1)}>v{i.blueprintVersion}</Link>
                      {i.upgradeAvailable && (
                        <div>
                          <StatusBadge tone="info" icon={ArrowUpCircle} label={`v${i.latestPublishedVersion} available`} />
                        </div>
                      )}
                    </td>
                    <td className="xsmall">
                      {Object.keys(map).length} ({Object.values(map).slice(0, 3).join(', ')}
                      {Object.keys(map).length > 3 ? ', …' : ''})
                    </td>
                    <td>
                      <RuntimeBadge i={i} />
                    </td>
                    <td style={{ whiteSpace: 'nowrap' }}>
                      {i.upgradeAvailable && (
                        <Button size="sm" icon={<ArrowUpCircle size={13} />} loading={deploy.isPending && deploy.variables?.id === i.id} onClick={() => deploy.mutate({ id: i.id, version: i.latestPublishedVersion ?? undefined })}>
                          Upgrade
                        </Button>
                      )}
                      {i.runtime?.state !== 'running' && !i.upgradeAvailable && (
                        <Button size="sm" icon={<Rocket size={13} />} loading={deploy.isPending && deploy.variables?.id === i.id} onClick={() => deploy.mutate({ id: i.id })}>
                          Deploy
                        </Button>
                      )}
                      <Link to={`/twins/${encodeURIComponent(i.id)}`} className="vts-btn vts-btn--ghost vts-btn--sm">
                        <ExternalLink size={13} aria-hidden="true" /> Open in Operate
                      </Link>
                    </td>
                  </tr>
                );
              })}
            </tbody>
          </table>
          {deploy.isError && (
            <div style={{ padding: 12 }}>
              <ErrorBlock error={deploy.error} compact />
            </div>
          )}
        </Pane>
      )}
      <CreateInstanceDialog open={open} onOpenChange={setOpen} />
    </EdPage>
  );
}
