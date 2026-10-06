/**
 * Build → Structure: the asset/structural model of the twin type — its identity, asset
 * TYPES (with property schemas) and the ASSETS every instance gets (instance scope) or
 * shares (context scope: the site, the line...), their hierarchy and relationships.
 * Kept separate from the world (where things are) and from behaviour and semantics.
 */
import { ReactFlow, Background, Controls, MiniMap, type Edge, type Node } from '@xyflow/react';
import '@xyflow/react/dist/style.css';
import Dagre from '@dagrejs/dagre';
import { Boxes, Copy, FolderTree, Link2, Plus, Shapes, Trash2 } from 'lucide-react';
import { useMemo, useState } from 'react';
import { useSearchParams } from 'react-router-dom';
import { useBlueprintValidation } from '@/api/blueprints';
import type { AssetTypeDef, BlueprintAssetDef, RelationshipDef, StructureSection } from '@/api/types';
import { Button, EmptyState, StatusBadge, Tabs } from '@/design';
import { safeLayout } from '@/design/graphLayout';
import { nextId, useEditor, useSection } from '../editor';
import { deleteAsset, renameAsset, renameAssetType } from '../refactor';
import { usedByAsset, usedByAssetType } from '../refs';
import {
  ConfirmDelete,
  EdPage,
  FindingsInline,
  IDENT,
  InspectorSection,
  JsonSectionView,
  KeyValueEditor,
  Pane,
  SelectField,
  SelectList,
  SLUG,
  TagsField,
  TextArea,
  TextField,
  UsedBy,
  type ListEntry,
} from '../ui';
import { useWorkspace } from '../workspace';

type Tab = 'identity' | 'assets' | 'types' | 'relationships' | 'graph';

/** An id field that renames on commit (Enter / blur), so references follow in one undo step. */
export function IdField({
  label,
  value,
  onRename,
  taken,
  pattern = IDENT,
  hint,
}: {
  label: string;
  value: string;
  onRename: (next: string) => void;
  taken: Set<string>;
  pattern?: RegExp;
  hint?: string;
}) {
  const e = useEditor();
  const [text, setText] = useState(value);
  const [prev, setPrev] = useState(value);
  if (prev !== value) {
    setPrev(value);
    setText(value);
  }
  const error = text === '' ? 'Required' : !pattern.test(text) ? 'Letters, digits, "_" or "-" (no spaces)' : text !== value && taken.has(text) ? 'Already used' : null;
  const commit = () => {
    if (!error && text !== value) onRename(text);
    else if (error) setText(value);
  };
  return (
    <label className="vts-f">
      <span>{label}</span>
      <input
        className={`vts-input mono${error ? ' is-invalid' : ''}`}
        value={text}
        disabled={!e.editable}
        aria-invalid={!!error || undefined}
        onChange={(ev) => setText(ev.target.value)}
        onBlur={commit}
        onKeyDown={(ev) => {
          if (ev.key === 'Enter') (ev.target as HTMLInputElement).blur();
          if (ev.key === 'Escape') setText(value);
        }}
      />
      {error ? <span className="vts-f__error">{error}</span> : <span className="vts-f__hint">{hint ?? 'Renaming updates every reference in this Blueprint.'}</span>}
    </label>
  );
}

function IdentityTab() {
  const [identity, setIdentity] = useSection('identity');
  const ws = useWorkspace();
  const set = <K extends keyof typeof identity>(k: K, v: (typeof identity)[K]) => setIdentity((p) => ({ ...p, [k]: v }), { label: `Identity: ${String(k)}`, key: `identity.${String(k)}` });
  return (
    <div className="grid-2">
      <Pane title="Identity">
        <div className="stack-sm">
          <TextField label="Name" value={identity.name} onChange={(v) => set('name', v)} />
          <TextArea label="Description" value={identity.description ?? ''} onChange={(v) => set('description', v)} rows={3} />
          <div className="vts-fgrid">
            <TextField label="Domain" value={identity.domain} onChange={(v) => set('domain', v)} hint="Selects the World & Layout tool palettes" />
            <TextField label="Icon" value={identity.icon ?? ''} onChange={(v) => set('icon', v)} hint="Icon name shown on cards" />
          </div>
          <TagsField label="Tags" value={identity.tags ?? []} onChange={(v) => set('tags', v)} placeholder="laboratory, thermal" />
        </div>
      </Pane>
      <Pane title="Runtime identity" actions={<StatusBadge tone="formal" label="Affects the verified core" />}>
        <div className="stack-sm">
          <TextField label="Model id" value={identity.modelId} onChange={(v) => set('modelId', v)} mono pattern={SLUG} patternMessage="lowercase letters, digits and '-'" hint="Stamped into the Twin IR and the package manifest" />
          <div className="vts-fgrid">
            <TextField label="Time unit" value={identity.timeUnit} onChange={(v) => set('timeUnit', v)} hint="Unit of clock constants (e.g. s)" />
            <label className="vts-f">
              <span>Ticks per unit</span>
              <input className="vts-input num" inputMode="numeric" value={identity.ticksPerUnit} onChange={(ev) => /^\d+$/.test(ev.target.value) && set('ticksPerUnit', Number(ev.target.value))} />
              <span className="vts-f__hint">Logical-time resolution of the kernel</span>
            </label>
          </div>
          <SelectField
            label="Runtime mode"
            value={identity.runtimeMode}
            onChange={(v) => set('runtimeMode', v)}
            options={[
              { value: 'monitor', label: 'Monitor — follow a data source and check conformance' },
              { value: 'cosimulation', label: 'Co-simulation — drive a simulator (mobile robots)' },
            ]}
          />
          {ws.expert && <TextField label="Operate plugin" value={identity.plugin ?? ''} onChange={(v) => set('plugin', v || null)} hint="Optional domain view in Operate (e.g. drone)" />}
        </div>
      </Pane>
    </div>
  );
}

function assetTree(s: StructureSection): ListEntry[] {
  const byParent = new Map<string, BlueprintAssetDef[]>();
  const ids = new Set(s.assets.map((a) => a.id));
  for (const a of s.assets) {
    const p = a.parent && ids.has(a.parent) ? a.parent : '';
    byParent.set(p, [...(byParent.get(p) ?? []), a]);
  }
  const out: ListEntry[] = [];
  const walk = (parent: string, depth: number) => {
    for (const a of (byParent.get(parent) ?? []).sort((x, y) => Number(y.id === s.root) - Number(x.id === s.root))) {
      out.push({
        id: a.id,
        label: a.name || a.id,
        depth,
        search: `${a.id} ${a.name} ${a.type}`,
        icon: a.scope === 'context' ? <Shapes size={13} aria-hidden="true" className="subtle" /> : <Boxes size={13} aria-hidden="true" />,
        meta: a.id === s.root ? 'root' : a.type,
      });
      walk(a.id, depth + 1);
    }
  };
  walk('', 0);
  return out;
}

function AssetInspector({ asset }: { asset: BlueprintAssetDef }) {
  const e = useEditor();
  const [structure, setStructure] = useSection('structure');
  const validation = useBlueprintValidation(e.id, e.version);
  const [confirm, setConfirm] = useState(false);
  const [, setParams] = useSearchParams();
  const set = (patch: Partial<BlueprintAssetDef>, label: string) =>
    setStructure((s) => ({ ...s, assets: s.assets.map((a) => (a.id === asset.id ? { ...a, ...patch } : a)) }), { label, key: `asset.${asset.id}.${Object.keys(patch)[0]}` });
  const type = structure.assetTypes.find((t) => t.id === asset.type);
  const deps = usedByAsset(e.doc, asset.id);
  const findings = (validation.data?.findings ?? []).filter((f) => f.section === 'structure' && f.target === asset.id);
  return (
    <Pane className="vts-inspector" title={`Asset · ${asset.name || asset.id}`} actions={asset.id === structure.root ? <StatusBadge tone="info" label="Root" /> : undefined}>
      <InspectorSection title="Identity">
        <IdField
          label="Id"
          value={asset.id}
          taken={new Set(structure.assets.map((a) => a.id))}
          pattern={/^[a-z0-9][a-z0-9_-]*$/}
          onRename={(next) => {
            e.updateDoc((d) => renameAsset(d, asset.id, next), { label: `Rename asset ${asset.id} → ${next}` });
            setParams({ asset: next }, { replace: true });
          }}
        />
        <TextField label="Name" value={asset.name} onChange={(v) => set({ name: v }, 'Asset name')} />
        <SelectField
          label="Type"
          value={asset.type}
          onChange={(v) => set({ type: v }, 'Asset type')}
          options={structure.assetTypes.map((t) => ({ value: t.id, label: t.name || t.id }))}
          allowEmpty="Choose a type…"
          error={!type ? 'Unknown asset type' : null}
        />
        <SelectField
          label="Scope"
          value={asset.scope}
          onChange={(v) => set({ scope: v }, 'Asset scope')}
          options={[
            { value: 'instance', label: 'Instance — every twin gets its own' },
            { value: 'context', label: 'Context — shared site/estate asset' },
          ]}
        />
        <SelectField
          label="Parent"
          value={asset.parent ?? ''}
          onChange={(v) => set({ parent: v || undefined }, 'Asset parent')}
          options={structure.assets.filter((a) => a.id !== asset.id).map((a) => ({ value: a.id, label: `${a.name || a.id} (${a.id})` }))}
          allowEmpty="— none (top level) —"
        />
        <TextArea label="Description" value={asset.description ?? ''} onChange={(v) => set({ description: v }, 'Asset description')} rows={2} />
        <TagsField label="Tags" value={asset.tags ?? []} onChange={(v) => set({ tags: v }, 'Asset tags')} />
      </InspectorSection>
      <InspectorSection title="Properties">
        {type && type.properties.length > 0 && (
          <p className="xsmall subtle">
            Type defaults: {type.properties.map((p) => `${p.key}${p.default ? ` = ${p.default}` : ''}`).join(', ')}. Values here override them; instances can override again.
          </p>
        )}
        <KeyValueEditor value={asset.properties ?? {}} onChange={(v) => set({ properties: v }, 'Asset properties')} />
      </InspectorSection>
      {findings.length > 0 && (
        <InspectorSection title="Problems">
          <FindingsInline findings={findings} />
        </InspectorSection>
      )}
      <InspectorSection title="Used by">
        <UsedBy deps={deps} />
      </InspectorSection>
      {e.editable && (
        <div className="row-wrap">
          {asset.id !== structure.root && asset.scope === 'instance' && (
            <Button size="sm" onClick={() => setStructure((s) => ({ ...s, root: asset.id }), { label: 'Set root asset' })}>Make root</Button>
          )}
          <Button size="sm" variant="danger" icon={<Trash2 size={13} />} onClick={() => setConfirm(true)}>Delete</Button>
        </div>
      )}
      <ConfirmDelete
        open={confirm}
        onOpenChange={setConfirm}
        title={`Delete asset ${asset.name || asset.id}?`}
        dependencies={deps}
        consequence="Child assets and relationships of this asset are deleted with it."
        onConfirm={() => {
          e.updateDoc((d) => deleteAsset(d, asset.id), { label: `Delete asset ${asset.id}` });
          setParams({}, { replace: true });
        }}
      />
    </Pane>
  );
}

function AssetsTab() {
  const e = useEditor();
  const [structure, setStructure] = useSection('structure');
  const [params, setParams] = useSearchParams();
  const validation = useBlueprintValidation(e.id, e.version);
  const selected = params.get('asset') ?? params.get('select');
  const asset = structure.assets.find((a) => a.id === selected) ?? null;
  const bad = new Map<string, 'error' | 'warning'>();
  for (const f of validation.data?.findings ?? []) if (f.section === 'structure' && f.target) bad.set(f.target, bad.get(f.target) === 'error' ? 'error' : f.severity);
  const items = assetTree(structure).map((i) => ({ ...i, tone: bad.get(i.id) }));
  const select = (id: string) => setParams({ asset: id }, { replace: true });
  const add = (scope: 'instance' | 'context') => {
    const id = nextId(scope === 'context' ? 'site-asset' : 'asset', structure.assets.map((a) => a.id), '-');
    const parent = scope === 'instance' ? (asset?.scope === 'instance' ? asset.id : structure.root || undefined) : undefined;
    const type = structure.assetTypes[0]?.id ?? '';
    setStructure((s) => ({ ...s, root: s.root || (scope === 'instance' ? id : s.root), assets: [...s.assets, { id, name: scope === 'context' ? 'Site asset' : 'New asset', type, scope, parent, properties: {}, tags: [] }] }), { label: 'Add asset' });
    select(id);
  };
  const duplicate = () => {
    if (!asset) return;
    const id = nextId(asset.id, structure.assets.map((a) => a.id), '-');
    setStructure((s) => ({ ...s, assets: [...s.assets, { ...asset, id, name: `${asset.name} (copy)` }] }), { label: 'Duplicate asset' });
    select(id);
  };
  return (
    <div className="vts-ed-split">
      <Pane
        title={<span className="row"><FolderTree size={14} aria-hidden="true" /> Assets</span>}
        flush
        actions={
          e.editable && (
            <>
              <Button size="sm" variant="ghost" iconOnly icon={<Copy size={13} />} disabled={!asset} onClick={duplicate}>Duplicate selected asset</Button>
              <Button size="sm" icon={<Plus size={13} />} onClick={() => add('instance')}>Asset</Button>
            </>
          )
        }
      >
        <SelectList
          items={items}
          selected={selected}
          onSelect={select}
          label="Assets"
          empty={<EmptyState compact title="No assets yet" action={e.editable ? <Button size="sm" onClick={() => add('instance')}>Add the root asset</Button> : undefined}>Every instance gets the instance-scoped assets; context assets are shared.</EmptyState>}
        />
        {e.editable && (
          <div style={{ padding: 8 }}>
            <Button size="sm" variant="ghost" icon={<Shapes size={13} />} onClick={() => add('context')}>Add context (site) asset</Button>
          </div>
        )}
      </Pane>
      <Pane title="Hierarchy">
        {structure.assets.length === 0 ? (
          <EmptyState compact title="Start with the root asset">The root asset is the twin itself (e.g. the chamber, the drone, the pump); its parts hang below it.</EmptyState>
        ) : (
          <div className="stack-sm small">
            <p className="muted">
              <strong>{structure.assets.filter((a) => a.scope !== 'context').length}</strong> instance-scoped assets (created for every twin) ·{' '}
              <strong>{structure.assets.filter((a) => a.scope === 'context').length}</strong> context assets (shared, bound per instance) ·{' '}
              <strong>{structure.relationships.length}</strong> relationships
            </p>
            <table className="vts-table">
              <thead>
                <tr><th>Asset</th><th>Type</th><th>Scope</th><th>Parent</th></tr>
              </thead>
              <tbody>
                {structure.assets.map((a) => (
                  <tr key={a.id} className="is-clickable" aria-selected={a.id === selected} onClick={() => select(a.id)}>
                    <td><strong>{a.name}</strong> <span className="mono xsmall subtle">{a.id}</span></td>
                    <td className="small">{structure.assetTypes.find((t) => t.id === a.type)?.name ?? <span style={{ color: 'var(--crit)' }}>{a.type || '—'}</span>}</td>
                    <td className="small">{a.scope === 'context' ? 'context' : a.id === structure.root ? 'instance · root' : 'instance'}</td>
                    <td className="mono xsmall">{a.parent ?? '—'}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        )}
      </Pane>
      {asset ? <AssetInspector key={asset.id} asset={asset} /> : <Pane className="vts-inspector" title="Inspector"><p className="small muted">Select an asset to edit it.</p></Pane>}
    </div>
  );
}

function TypesTab() {
  const e = useEditor();
  const [structure, setStructure] = useSection('structure');
  const [params, setParams] = useSearchParams();
  const selected = params.get('type');
  const type = structure.assetTypes.find((t) => t.id === selected) ?? null;
  const [confirm, setConfirm] = useState(false);
  const select = (id: string) => setParams({ tab: 'types', type: id }, { replace: true });
  const set = (patch: Partial<AssetTypeDef>, label: string) =>
    setStructure((s) => ({ ...s, assetTypes: s.assetTypes.map((t) => (t.id === type!.id ? { ...t, ...patch } : t)) }), { label, key: `type.${type!.id}.${Object.keys(patch)[0]}` });
  const add = () => {
    const id = nextId('NewType', structure.assetTypes.map((t) => t.id), '');
    setStructure((s) => ({ ...s, assetTypes: [...s.assetTypes, { id, name: 'New type', category: '', description: '', properties: [] }] }), { label: 'Add asset type' });
    select(id);
  };
  return (
    <div className="vts-ed-split vts-ed-split--two">
      <Pane title="Asset types" flush actions={e.editable && <Button size="sm" icon={<Plus size={13} />} onClick={add}>Type</Button>}>
        <SelectList
          items={structure.assetTypes.map((t) => ({ id: t.id, label: t.name || t.id, meta: `${structure.assets.filter((a) => a.type === t.id).length} assets`, search: `${t.id} ${t.name} ${t.category ?? ''}` }))}
          selected={selected}
          onSelect={select}
          label="Asset types"
          empty={<EmptyState compact title="No asset types">Types describe kinds of assets and their property schema; assets are instances of them.</EmptyState>}
        />
      </Pane>
      {type ? (
        <Pane title={`Type · ${type.name || type.id}`}>
          <div className="stack">
            <div className="vts-fgrid">
              <IdField label="Id" value={type.id} taken={new Set(structure.assetTypes.map((t) => t.id))} onRename={(next) => { e.updateDoc((d) => renameAssetType(d, type.id, next), { label: `Rename type ${type.id}` }); select(next); }} />
              <TextField label="Name" value={type.name} onChange={(v) => set({ name: v }, 'Type name')} />
              <TextField label="Category" value={type.category ?? ''} onChange={(v) => set({ category: v }, 'Type category')} placeholder="Equipment, Sensor, Site…" />
              <TextField label="Description" value={type.description ?? ''} onChange={(v) => set({ description: v }, 'Type description')} />
            </div>
            <div className="stack-sm">
              <span className="vts-label">Property schema</span>
              <table className="vts-table">
                <thead><tr><th>Key</th><th>Label</th><th>Type</th><th>Unit</th><th>Default</th><th /></tr></thead>
                <tbody>
                  {type.properties.map((p, i) => {
                    const upd = (patch: Partial<typeof p>) => set({ properties: type.properties.map((x, j) => (j === i ? { ...x, ...patch } : x)) }, 'Type property');
                    return (
                      <tr key={i}>
                        <td><input className="vts-input mono" value={p.key} disabled={!e.editable} aria-label="Property key" onChange={(ev) => upd({ key: ev.target.value })} /></td>
                        <td><input className="vts-input" value={p.label ?? ''} disabled={!e.editable} aria-label="Property label" onChange={(ev) => upd({ label: ev.target.value })} /></td>
                        <td>
                          <select className="vts-select" value={p.type ?? 'string'} disabled={!e.editable} aria-label="Property type" onChange={(ev) => upd({ type: ev.target.value })}>
                            {['string', 'integer', 'real', 'boolean'].map((t) => <option key={t}>{t}</option>)}
                          </select>
                        </td>
                        <td><input className="vts-input" style={{ width: 80 }} value={p.unit ?? ''} disabled={!e.editable} aria-label="Unit" onChange={(ev) => upd({ unit: ev.target.value })} /></td>
                        <td><input className="vts-input" value={p.default ?? ''} disabled={!e.editable} aria-label="Default value" onChange={(ev) => upd({ default: ev.target.value })} /></td>
                        <td>{e.editable && <Button size="sm" variant="ghost" iconOnly icon={<Trash2 size={13} />} onClick={() => set({ properties: type.properties.filter((_, j) => j !== i) }, 'Remove type property')}>Remove property</Button>}</td>
                      </tr>
                    );
                  })}
                </tbody>
              </table>
              {e.editable && <Button size="sm" variant="ghost" icon={<Plus size={13} />} onClick={() => set({ properties: [...type.properties, { key: nextId('property', type.properties.map((p) => p.key)), label: '', type: 'string' }] }, 'Add type property')}>Add property</Button>}
            </div>
            <InspectorSection title="Assets of this type">
              <UsedBy deps={usedByAssetType(e.doc, type.id)} />
            </InspectorSection>
            {e.editable && <div><Button size="sm" variant="danger" icon={<Trash2 size={13} />} onClick={() => setConfirm(true)}>Delete type</Button></div>}
            <ConfirmDelete
              open={confirm}
              onOpenChange={setConfirm}
              title={`Delete asset type ${type.name || type.id}?`}
              dependencies={usedByAssetType(e.doc, type.id)}
              onConfirm={() => {
                setStructure((s) => ({ ...s, assetTypes: s.assetTypes.filter((t) => t.id !== type.id) }), { label: `Delete type ${type.id}` });
                setParams({ tab: 'types' }, { replace: true });
              }}
            />
          </div>
        </Pane>
      ) : (
        <Pane title="Asset type"><p className="small muted">Select a type to edit its property schema.</p></Pane>
      )}
    </div>
  );
}

function RelationshipsTab() {
  const e = useEditor();
  const [structure, setStructure] = useSection('structure');
  const ids = structure.assets.map((a) => ({ value: a.id, label: `${a.name || a.id} (${a.id})` }));
  const set = (i: number, patch: Partial<RelationshipDef>) =>
    setStructure((s) => ({ ...s, relationships: s.relationships.map((r, j) => (j === i ? { ...r, ...patch } : r)) }), { label: 'Edit relationship', key: `rel.${i}` });
  return (
    <Pane title={<span className="row"><Link2 size={14} aria-hidden="true" /> Relationships</span>} actions={e.editable && (
      <Button size="sm" icon={<Plus size={13} />} disabled={structure.assets.length < 2} onClick={() => setStructure((s) => ({ ...s, relationships: [...s.relationships, { id: nextId('r', s.relationships.map((r) => r.id), ''), source: s.assets[0]!.id, type: 'relatedTo', target: s.assets[1]!.id }] }), { label: 'Add relationship' })}>
        Relationship
      </Button>
    )}>
      {structure.relationships.length === 0 ? (
        <EmptyState compact title="No relationships">Typed links between assets (heats, measures, feeds, locatedIn…) beyond the parent hierarchy.</EmptyState>
      ) : (
        <table className="vts-table">
          <thead><tr><th>Source</th><th>Relationship</th><th>Target</th><th /></tr></thead>
          <tbody>
            {structure.relationships.map((r, i) => (
              <tr key={r.id}>
                <td><select className="vts-select" value={r.source} disabled={!e.editable} aria-label="Source asset" onChange={(ev) => set(i, { source: ev.target.value })}>{ids.map((o) => <option key={o.value} value={o.value}>{o.label}</option>)}</select></td>
                <td><input className="vts-input mono" value={r.type} disabled={!e.editable} aria-label="Relationship type" list="vts-rel-types" onChange={(ev) => set(i, { type: ev.target.value })} /></td>
                <td><select className="vts-select" value={r.target} disabled={!e.editable} aria-label="Target asset" onChange={(ev) => set(i, { target: ev.target.value })}>{ids.map((o) => <option key={o.value} value={o.value}>{o.label}</option>)}</select></td>
                <td>{e.editable && <Button size="sm" variant="ghost" iconOnly icon={<Trash2 size={13} />} onClick={() => setStructure((s) => ({ ...s, relationships: s.relationships.filter((_, j) => j !== i) }), { label: 'Delete relationship' })}>Delete relationship</Button>}</td>
              </tr>
            ))}
          </tbody>
        </table>
      )}
      <datalist id="vts-rel-types">
        {['locatedIn', 'partOf', 'heats', 'cools', 'measures', 'monitors', 'feeds', 'drives', 'controls', 'powers', 'connectedTo', 'assignedTo', 'homesAt', 'standbyFor'].map((t) => <option key={t} value={t} />)}
      </datalist>
    </Pane>
  );
}

function GraphTab() {
  const [structure] = useSection('structure');
  const [params, setParams] = useSearchParams();
  const selected = params.get('asset');
  const asset = structure.assets.find((a) => a.id === selected) ?? null;
  const { nodes, edges } = useMemo(() => {
    const g = new Dagre.graphlib.Graph();
    g.setGraph({ rankdir: 'TB', nodesep: 30, ranksep: 60 });
    g.setDefaultEdgeLabel(() => ({}));
    for (const a of structure.assets) g.setNode(a.id, { width: 170, height: 46 });
    for (const a of structure.assets) if (a.parent && structure.assets.some((x) => x.id === a.parent)) g.setEdge(a.parent, a.id);
    safeLayout(g);
    const nodes: Node[] = structure.assets.map((a) => {
      const p = g.node(a.id) as unknown as { x: number; y: number } | undefined;
      return {
        id: a.id,
        position: { x: (p?.x ?? 0) - 85, y: (p?.y ?? 0) - 23 },
        data: { label: `${a.name || a.id}\n${a.type}` },
        style: {
          width: 170, fontSize: 11, whiteSpace: 'pre-line', borderRadius: 8,
          border: `2px solid ${a.id === selected ? 'var(--accent)' : a.scope === 'context' ? 'var(--border)' : 'var(--border-strong)'}`,
          borderStyle: a.scope === 'context' ? 'dashed' : 'solid', background: a.id === structure.root ? 'var(--accent-soft)' : 'var(--surface)',
        },
      };
    });
    const edges: Edge[] = [
      ...structure.assets.filter((a) => a.parent).map((a) => ({ id: `p-${a.id}`, source: a.parent!, target: a.id, style: { strokeDasharray: '4 3' }, label: 'part of', labelStyle: { fontSize: 9 } })),
      ...structure.relationships.map((r) => ({ id: `r-${r.id}`, source: r.source, target: r.target, label: r.type, labelStyle: { fontSize: 10 }, animated: false, style: { stroke: 'var(--accent)' } })),
    ];
    return { nodes, edges };
  }, [structure, selected]);
  return (
    <div className="vts-ed-split vts-ed-split--wide-inspector">
      <div className="vts-canvas-wrap" style={{ height: 'calc(100vh - var(--topbar-height) - 230px)' }}>
        <ReactFlow nodes={nodes} edges={edges} fitView nodesDraggable={false} onNodeClick={(_, n) => setParams({ tab: 'graph', asset: n.id }, { replace: true })} proOptions={{ hideAttribution: true }}>
          <Background gap={20} />
          <Controls showInteractive={false} />
          <MiniMap pannable zoomable />
        </ReactFlow>
      </div>
      {asset ? <AssetInspector key={asset.id} asset={asset} /> : <Pane className="vts-inspector" title="Inspector"><p className="small muted">Click an asset in the graph. Dashed boxes are context assets; dashed lines the part-of hierarchy; blue lines typed relationships.</p></Pane>}
    </div>
  );
}

export default function StructurePage() {
  const [params, setParams] = useSearchParams();
  const ws = useWorkspace();
  const [structure, setStructure] = useSection('structure');
  const tab = (params.get('tab') as Tab | null) ?? (params.get('type') ? 'types' : 'assets');
  const [json, setJson] = useState(false);
  return (
    <EdPage
      title="Structure"
      description="The asset model of this twin type: what it consists of, which assets every instance gets, and how they relate. Kept separate from where things are (World) and how they behave (Behavior)."
      actions={ws.expert && <Button size="sm" variant="ghost" onClick={() => setJson((j) => !j)}>{json ? 'Form view' : 'JSON view'}</Button>}
      guide={<>Define <strong>asset types</strong> first (e.g. ThermalChamber, Heater, DoorSensor), then the <strong>assets</strong>: the root asset is the twin itself; instance-scoped assets are created for every instance, context assets (the lab, the line) are shared and bound to existing estate assets when an instance is created.</>}
    >
      <Tabs<Tab>
        label="Structure views"
        value={tab}
        onChange={(t) => setParams({ tab: t }, { replace: true })}
        tabs={[
          { id: 'identity', label: 'Identity' },
          { id: 'assets', label: 'Assets' },
          { id: 'types', label: 'Asset types' },
          { id: 'relationships', label: 'Relationships' },
          { id: 'graph', label: 'Graph' },
        ]}
      />
      {json ? (
        <JsonSectionView value={structure} label="Structure" onApply={(v) => setStructure(v, { label: 'Edit structure (JSON)' })} />
      ) : tab === 'identity' ? (
        <IdentityTab />
      ) : tab === 'types' ? (
        <TypesTab />
      ) : tab === 'relationships' ? (
        <RelationshipsTab />
      ) : tab === 'graph' ? (
        <GraphTab />
      ) : (
        <AssetsTab />
      )}
    </EdPage>
  );
}
