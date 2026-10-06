/**
 * Assets of the selected twin: the asset subtree as a tree and as a searchable table, with
 * each asset's data-source status, and "+ Add asset".
 *
 * Adding never blurs identity: LINK relates an existing physical asset to this twin's asset
 * (the asset is not copied); CLONE creates a new asset definition with a new identity and
 * never copies telemetry/data-source bindings. Both are audited by twin-studio.
 */
import { useQueries } from '@tanstack/react-query';
import { ChevronDown, ChevronRight, Copy, FileUp, Link2, Plus, Search, Wand2 } from 'lucide-react';
import { useMemo, useState } from 'react';
import { Link, useNavigate } from 'react-router-dom';
import { api } from '@/api/client';
import { keys, useAssets, useChannels, useEngineeringMutation, useTwins } from '@/api/queries';
import type { AssetDetail, TelemetryChannel } from '@/api/types';
import { Button, Dialog, EmptyState, ErrorBlock, FreshnessBadge, PageHeader, Panel, StatusBadge } from '@/design';
import { useTwinScope, twinRoute } from '@/app/twinScope';

/** Loads an asset and all its descendants (breadth-first, from the cache as it fills). */
function useSubtree(rootId: string | undefined) {
  const [ids, setIds] = useState<string[]>([]);
  const all = rootId ? [rootId, ...ids.filter((i) => i !== rootId)] : [];
  const results = useQueries({
    queries: all.map((id) => ({ queryKey: keys.asset(id), queryFn: () => api.get<AssetDetail>(`/assets/${encodeURIComponent(id)}`) })),
  });
  const loaded = results.map((r) => r.data).filter((d): d is AssetDetail => !!d);
  const discovered = loaded.flatMap((a) => a.children.map((c) => c.id)).filter((id) => !all.includes(id));
  if (discovered.length) setIds((prev) => [...new Set([...prev, ...discovered])]);
  const loading = results.some((r) => r.isLoading);
  return { assets: loaded, loading };
}

const WORST: Record<string, number> = { invalid: 4, missing: 3, stale: 2, fresh: 1 };
function sourceStatus(channels: TelemetryChannel[]) {
  if (!channels.length) return null;
  const worst = channels.reduce((w, c) => ((WORST[c.freshness ?? 'fresh'] ?? 0) > (WORST[w] ?? 0) ? c.freshness ?? 'fresh' : w), 'fresh' as string);
  return { count: channels.length, freshness: worst as TelemetryChannel['freshness'] };
}

function TreeNode({ asset, byId, depth, base, filter, open, toggle }: {
  asset: AssetDetail; byId: Map<string, AssetDetail>; depth: number; base: string; filter: string; open: Set<string>; toggle: (id: string) => void;
}) {
  const expanded = open.has(asset.id) || !!filter;
  const kids = asset.children.map((c) => byId.get(c.id)).filter((c): c is AssetDetail => !!c);
  const matchesSelf = !filter || `${asset.name} ${asset.type} ${asset.id}`.toLowerCase().includes(filter);
  const anyDesc = (a: AssetDetail): boolean => a.children.some((c) => { const d = byId.get(c.id); return !!d && (`${d.name} ${d.type} ${d.id}`.toLowerCase().includes(filter) || anyDesc(d)); });
  if (filter && !matchesSelf && !anyDesc(asset)) return null;
  return (
    <li>
      <div className="vts-tree__row" style={{ paddingLeft: depth * 18 }}>
        {asset.children.length ? (
          <button type="button" className="vts-tree__toggle" onClick={() => toggle(asset.id)} aria-expanded={expanded} aria-label={expanded ? `Collapse ${asset.name}` : `Expand ${asset.name}`}>
            {expanded ? <ChevronDown size={14} /> : <ChevronRight size={14} />}
          </button>
        ) : <span className="vts-tree__toggle" aria-hidden="true" />}
        <Link to={`${base}/assets/${encodeURIComponent(asset.id)}`}>{asset.name}</Link>
        <span className="xsmall subtle">{asset.type}</span>
      </div>
      {expanded && kids.length > 0 && (
        <ul>{kids.map((k) => <TreeNode key={k.id} asset={k} byId={byId} depth={depth + 1} base={base} filter={filter} open={open} toggle={toggle} />)}</ul>
      )}
    </li>
  );
}

type Mode = 'choose' | 'twin' | 'file' | 'link';
const LINK_TYPES = ['uses', 'connectedTo', 'dependsOn', 'feeds', 'powers', 'monitors', 'controls', 'locatedIn', 'observedBy'];

function AddAssetDialog({ open, onOpenChange }: { open: boolean; onOpenChange: (o: boolean) => void }) {
  const { twin, asset: root, base } = useTwinScope()!;
  const navigate = useNavigate();
  const [mode, setMode] = useState<Mode>('choose');
  const twins = useTwins();
  const [fromTwin, setFromTwin] = useState('');
  const otherTwin = twins.data?.find((t) => t.id === fromTwin);
  const sub = useSubtree(otherTwin?.assetId ?? undefined);
  const [picked, setPicked] = useState('');
  const [action, setAction] = useState<'link' | 'clone'>('link');
  const [relType, setRelType] = useState('uses');
  const [newId, setNewId] = useState('');
  const [newName, setNewName] = useState('');
  const [q, setQ] = useState('');
  const search = useAssets({ q: q || undefined, limit: 12 });
  const [fileRows, setFileRows] = useState<{ id: string; name: string; type: string; parentId?: string; description?: string; tags?: string[]; properties?: Record<string, unknown> }[] | null>(null);
  const [fileError, setFileError] = useState<string | null>(null);

  const link = useEngineeringMutation((b: { source: string; type: string; targetId: string }) => api.post<AssetDetail>(`/assets/${encodeURIComponent(b.source)}/relationships`, { type: b.type, targetId: b.targetId }));
  const create = useEngineeringMutation(async (rows: Record<string, unknown>[]) => {
    for (const r of rows) await api.post<AssetDetail>('/assets', r);
  });
  const reset = () => { setMode('choose'); setFromTwin(''); setPicked(''); setNewId(''); setNewName(''); setFileRows(null); setFileError(null); setQ(''); link.reset(); create.reset(); };
  const close = (o: boolean) => { if (!o) reset(); onOpenChange(o); };
  const source = sub.assets.find((a) => a.id === picked);

  const onFile = async (f: File | undefined) => {
    setFileError(null);
    setFileRows(null);
    if (!f) return;
    try {
      const data = JSON.parse(await f.text());
      const rows = Array.isArray(data) ? data : Array.isArray(data?.assets) ? data.assets : null;
      if (!rows) throw new Error('expected a JSON array of assets, or {"assets": [...]}');
      for (const [i, r] of rows.entries()) {
        if (!r || typeof r.id !== 'string' || typeof r.name !== 'string' || typeof r.type !== 'string') throw new Error(`entry ${i + 1} needs string "id", "name" and "type"`);
      }
      setFileRows(rows);
    } catch (e) {
      setFileError(e instanceof Error ? e.message : String(e));
    }
  };

  return (
    <Dialog open={open} onOpenChange={close} wide title="Add an asset to this twin" description={`New and linked assets appear under ${root.name}. Nothing here changes the twin's verified behavioural model.`}>
      {mode === 'choose' && (
        <div className="vts-choice">
          <button type="button" className="vts-choice__opt" onClick={() => { close(false); navigate(`/studio?start=asset&twin=${encodeURIComponent(twin.id)}`); }}>
            <Wand2 size={20} aria-hidden="true" /><div><strong>Create in Studio</strong><span>Create or extend asset and twin definitions in Studio (asset schema, telemetry, models).</span></div>
          </button>
          <button type="button" className="vts-choice__opt" onClick={() => setMode('twin')}>
            <Copy size={20} aria-hidden="true" /><div><strong>Add from an existing twin</strong><span>Browse the assets of another twin, then link one (same physical asset) or clone its definition (a new asset).</span></div>
          </button>
          <button type="button" className="vts-choice__opt" onClick={() => setMode('file')}>
            <FileUp size={20} aria-hidden="true" /><div><strong>Import from file</strong><span>Asset definitions as JSON: id, name, type, and optional parentId, description, tags, properties.</span></div>
          </button>
          <button type="button" className="vts-choice__opt" onClick={() => setMode('link')}>
            <Link2 size={20} aria-hidden="true" /><div><strong>Link an existing asset</strong><span>Reference an asset already registered on the platform without copying it: one physical asset, one identity.</span></div>
          </button>
        </div>
      )}

      {mode === 'twin' && (
        <div className="stack">
          <label className="vts-field"><span>1 · Twin</span>
            <select className="vts-select" value={fromTwin} onChange={(e) => { setFromTwin(e.target.value); setPicked(''); }}>
              <option value="">Choose a twin…</option>
              {twins.data?.filter((t) => t.id !== twin.id && t.assetId).map((t) => <option key={t.id} value={t.id}>{t.name}</option>)}
            </select>
          </label>
          {otherTwin && (
            <div className="vts-field"><span>2 · Asset</span>
              <div className="vts-pick-list" role="listbox" aria-label="Assets of the selected twin">
                {sub.assets.map((a) => (
                  <button key={a.id} type="button" role="option" aria-selected={picked === a.id} className="vts-pick" onClick={() => { setPicked(a.id); setNewId(`${a.id}-copy`); setNewName(`${a.name} (copy)`); }}>
                    <strong>{a.name}</strong> <span className="xsmall subtle">{a.type} · {a.id}</span>
                  </button>
                ))}
              </div>
            </div>
          )}
          {source && (
            <div className="vts-field"><span>3 · How to add {source.name}</span>
              <div className="vts-choice">
                <button type="button" className="vts-choice__opt" aria-pressed={action === 'link'} onClick={() => setAction('link')}>
                  <Link2 size={18} aria-hidden="true" /><div><strong>Link (same physical asset)</strong><span>Adds a relationship from {root.name} to {source.name}. The asset keeps its single identity and its data sources stay where they are.</span></div>
                </button>
                <button type="button" className="vts-choice__opt" aria-pressed={action === 'clone'} onClick={() => setAction('clone')}>
                  <Copy size={18} aria-hidden="true" /><div><strong>Clone definition (new asset)</strong><span>Creates a NEW asset with its own identity, copying type, description, tags and properties. Telemetry and data-source bindings are not copied.</span></div>
                </button>
              </div>
              {action === 'link' ? (
                <label className="vts-field"><span>Relationship</span>
                  <select className="vts-select" value={relType} onChange={(e) => setRelType(e.target.value)}>{LINK_TYPES.map((t) => <option key={t} value={t}>{root.name} {t} {source.name}</option>)}</select>
                </label>
              ) : (
                <div className="grid-2">
                  <label className="vts-field"><span>New asset id</span><input className="vts-input" value={newId} onChange={(e) => setNewId(e.target.value)} /></label>
                  <label className="vts-field"><span>Name</span><input className="vts-input" value={newName} onChange={(e) => setNewName(e.target.value)} /></label>
                </div>
              )}
            </div>
          )}
          {(link.error || create.error) && <ErrorBlock error={(link.error ?? create.error)!} compact />}
          <div className="row">
            <Button onClick={() => setMode('choose')}>Back</Button>
            <div className="grow" />
            <Button
              variant="primary"
              disabled={!source || (action === 'clone' && (!newId.trim() || !newName.trim()))}
              loading={link.isPending || create.isPending}
              onClick={() =>
                action === 'link'
                  ? link.mutate({ source: root.id, type: relType, targetId: source!.id }, { onSuccess: () => close(false) })
                  : create.mutate(
                      [{ id: newId.trim(), name: newName.trim(), type: source!.type, parentId: root.id, description: source!.description, tags: source!.tags, properties: source!.properties, clonedFrom: source!.id }],
                      { onSuccess: () => { close(false); navigate(`${base}/assets/${encodeURIComponent(newId.trim())}`); } },
                    )
              }
            >
              {action === 'link' ? 'Link asset' : 'Create clone'}
            </Button>
          </div>
        </div>
      )}

      {mode === 'file' && (
        <div className="stack">
          <label className="vts-field"><span>JSON file</span><input type="file" accept=".json,application/json" onChange={(e) => void onFile(e.target.files?.[0])} /></label>
          {fileError && <ErrorBlock error={new Error(fileError)} compact />}
          {fileRows && (
            <Panel title={`${fileRows.length} asset(s) to create`} flush>
              <ul className="vts-list" style={{ padding: '0 var(--s-4)' }}>
                {fileRows.map((r) => <li key={r.id} className="small"><strong>{r.name}</strong> <span className="muted">{r.type}</span> <span className="mono xsmall">{r.id}</span> <span className="xsmall subtle">under {r.parentId ?? root.id}</span></li>)}
              </ul>
            </Panel>
          )}
          <p className="xsmall subtle">Imported assets have no data sources until they are bound. Existing ids are refused, never overwritten.</p>
          {create.error && <ErrorBlock error={create.error} compact />}
          <div className="row">
            <Button onClick={() => setMode('choose')}>Back</Button>
            <div className="grow" />
            <Button variant="primary" disabled={!fileRows?.length} loading={create.isPending}
              onClick={() => create.mutate(fileRows!.map((r) => ({ ...r, parentId: r.parentId ?? root.id })), { onSuccess: () => close(false) })}>
              Import {fileRows?.length ?? 0} asset(s)
            </Button>
          </div>
        </div>
      )}

      {mode === 'link' && (
        <div className="stack">
          <label className="vts-field"><span>Find an asset</span>
            <input className="vts-input" value={q} onChange={(e) => { setQ(e.target.value); setPicked(''); }} placeholder="Name, type or id…" autoFocus />
          </label>
          <div className="vts-pick-list" role="listbox" aria-label="Matching assets">
            {(search.data?.items ?? []).filter((a) => a.id !== root.id).map((a) => (
              <button key={a.id} type="button" role="option" aria-selected={picked === a.id} className="vts-pick" onClick={() => setPicked(a.id)}>
                <strong>{a.name}</strong> <span className="xsmall subtle">{a.type} · {a.id}{a.twinId ? ` · twin ${a.twinId}` : ''}</span>
              </button>
            ))}
          </div>
          {picked && (
            <label className="vts-field"><span>Relationship</span>
              <select className="vts-select" value={relType} onChange={(e) => setRelType(e.target.value)}>{LINK_TYPES.map((t) => <option key={t} value={t}>{root.name} {t} {picked}</option>)}</select>
            </label>
          )}
          {link.error && <ErrorBlock error={link.error} compact />}
          <div className="row">
            <Button onClick={() => setMode('choose')}>Back</Button>
            <div className="grow" />
            <Button variant="primary" disabled={!picked} loading={link.isPending} onClick={() => link.mutate({ source: root.id, type: relType, targetId: picked }, { onSuccess: () => close(false) })}>Link asset</Button>
          </div>
        </div>
      )}
    </Dialog>
  );
}

export default function TwinAssetsPage() {
  const { twin, asset: root, base } = useTwinScope()!;
  const { assets, loading } = useSubtree(root.id);
  const channels = useChannels(root.id, { refetchMs: 10_000 });
  const [filter, setFilter] = useState('');
  const [open, setOpen] = useState<Set<string>>(() => new Set([root.id]));
  const [addOpen, setAddOpen] = useState(false);
  const byId = useMemo(() => new Map(assets.map((a) => [a.id, a])), [assets]);
  const byAsset = useMemo(() => {
    const m = new Map<string, TelemetryChannel[]>();
    for (const c of channels.data?.channels ?? []) m.set(c.assetId, [...(m.get(c.assetId) ?? []), c]);
    return m;
  }, [channels.data]);
  const f = filter.trim().toLowerCase();
  const rows = assets.filter((a) => !f || `${a.name} ${a.type} ${a.id}`.toLowerCase().includes(f));
  const toggle = (id: string) => setOpen((s) => { const n = new Set(s); if (n.has(id)) n.delete(id); else n.add(id); return n; });
  const rootNode = byId.get(root.id);
  const links = root.relationships.filter((r) => r.type !== 'contains');

  return (
    <div className="vts-page stack">
      <PageHeader
        title="Assets"
        meta={<span>{assets.length} asset(s) in {twin.name}{loading ? ' (loading…)' : ''}</span>}
        actions={
          <>
            <label className="vts-lib__search" style={{ height: 34 }}>
              <Search size={14} aria-hidden="true" />
              <input type="search" value={filter} onChange={(e) => setFilter(e.target.value)} placeholder="Search assets…" aria-label="Search assets" />
            </label>
            <Button variant="primary" icon={<Plus size={14} />} onClick={() => setAddOpen(true)}>Add asset</Button>
          </>
        }
      />
      <div className="grid-main-side" style={{ gridTemplateColumns: '320px minmax(0, 1fr)' }}>
        <Panel title="Hierarchy">
          {rootNode ? <ul className="vts-tree">{<TreeNode asset={rootNode} byId={byId} depth={0} base={base} filter={f} open={open} toggle={toggle} />}</ul> : <p className="small muted">Loading…</p>}
          {links.length > 0 && (
            <div style={{ marginTop: 12 }}>
              <div className="xsmall subtle" style={{ marginBottom: 4 }}>LINKED ASSETS (not part of this twin's hierarchy)</div>
              <ul className="vts-list">
                {links.map((r) => <li key={r.id} className="small"><span className="muted">{r.type}</span> <Link to={`/assets/${encodeURIComponent(r.direction === 'outgoing' ? r.targetId : r.sourceId)}`}>{r.other?.name ?? r.targetId}</Link></li>)}
              </ul>
            </div>
          )}
        </Panel>
        <Panel title="All assets" flush>
          {rows.length === 0 ? <EmptyState compact title={f ? 'No asset matches' : 'No assets'} /> : (
            <div className="vts-table-wrap">
              <table className="vts-table">
                <caption className="sr-only">Assets of {twin.name}</caption>
                <thead><tr><th>Name</th><th>Type</th><th>Part of</th><th>Data sources</th><th>Relationships</th></tr></thead>
                <tbody>
                  {rows.map((a) => {
                    const src = sourceStatus(byAsset.get(a.id) ?? []);
                    return (
                      <tr key={a.id}>
                        <td><Link to={`${base}/assets/${encodeURIComponent(a.id)}`} className="strong">{a.name}</Link><div className="mono xsmall subtle">{a.id}</div></td>
                        <td className="small">{a.type}</td>
                        <td className="small">{a.ancestors.length ? a.ancestors[a.ancestors.length - 1]!.name : <StatusBadge tone="info" label="Twin root" />}</td>
                        <td>{src ? <span className="row" style={{ gap: 6 }}><FreshnessBadge freshness={src.freshness!} /> <span className="xsmall subtle">{src.count} channel(s)</span></span> : <span className="xsmall subtle">none</span>}</td>
                        <td className="small">{a.relationships.filter((r) => r.type !== 'contains').length || '—'}</td>
                      </tr>
                    );
                  })}
                </tbody>
              </table>
            </div>
          )}
        </Panel>
      </div>
      <p className="xsmall subtle">Assets and their relationships are operational context. They are distinct from the formal ontology; changing them never changes the twin's verified behaviour. <Link to={twinRoute(twin.id, 'knowledge')}>Open the knowledge graph</Link>.</p>
      <AddAssetDialog open={addOpen} onOpenChange={setAddOpen} />
    </div>
  );
}
