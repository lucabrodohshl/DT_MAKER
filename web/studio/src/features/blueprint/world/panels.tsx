/**
 * Side panels of the World & Layout editor: layers (role, visibility, lock, order), the
 * object inspector (with asset binding and two-way navigation to Structure), world settings,
 * file import (images as backgrounds; GeoJSON and twin-world/1 as geometry), the rasterised
 * simulator grids (ground truth vs the twin's initial knowledge) and the mobile-robot
 * simulation and observation model.
 */
import { ArrowDown, ArrowUp, Eye, EyeOff, FileUp, Lock, LockOpen, Plus, Trash2 } from 'lucide-react';
import { useMemo, useState } from 'react';
import { Link } from 'react-router-dom';
import { blueprintApi, blueprintRoute, useWorldRaster } from '@/api/blueprints';
import type { BlueprintAssetDef, WorldDocument, WorldLayer, WorldLayerRole, WorldObject, WorldObjectKind } from '@/api/types';
import { Button, Callout, Dialog, EmptyState, ErrorBlock, Segmented, StatusBadge } from '@/design';
import { nextId, useEditor } from '../editor';
import { usedByWorldObject } from '../refs';
import { CheckField, DecimalField, InspectorSection, IntField, KeyValueEditor, SelectField, TagsField, TextField, UsedBy } from '../ui';
import { isLinkKind, isPointKind, pointsOf } from './geometry';

export const ROLE_LABEL: Record<WorldLayerRole, string> = {
  shared: 'Shared (truth + knowledge)',
  'ground-truth': 'Ground truth only',
  knowledge: 'Twin knowledge only',
  event: 'Event geometry',
  annotation: 'Annotation',
  background: 'Background image',
};

export function LayersPanel({ world, active, onActive, onChange }: { world: WorldDocument; active: string; onActive: (id: string) => void; onChange: (w: WorldDocument, label: string) => void }) {
  const e = useEditor();
  const setLayer = (id: string, patch: Partial<WorldLayer>, label: string) => onChange({ ...world, layers: world.layers.map((l) => (l.id === id ? { ...l, ...patch } : l)) }, label);
  const move = (i: number, d: -1 | 1) => {
    const ls = [...world.layers];
    const j = i + d;
    if (j < 0 || j >= ls.length) return;
    [ls[i], ls[j]] = [ls[j]!, ls[i]!];
    onChange({ ...world, layers: ls }, 'Reorder layers');
  };
  const counts = new Map<string, number>();
  for (const o of world.objects) counts.set(o.layer, (counts.get(o.layer) ?? 0) + 1);
  return (
    <div className="stack-sm">
      <ul className="vts-ed-list" style={{ maxHeight: 'none', padding: 0 }} aria-label="Layers, top first">
        {[...world.layers].map((l, i) => ({ l, i })).reverse().map(({ l, i }) => (
          <li key={l.id}>
            <div className={`vts-ed-item${l.id === active ? ' is-selected' : ''}`} style={{ cursor: 'default' }}>
              <button type="button" className="vts-linkbtn grow truncate" style={{ color: 'var(--text)' }} onClick={() => onActive(l.id)} aria-pressed={l.id === active} title="Draw new objects on this layer">
                <strong className="small">{l.name}</strong> <span className="xsmall subtle">· {counts.get(l.id) ?? 0}</span>
                <div className="xsmall subtle">{ROLE_LABEL[l.role]}</div>
              </button>
              <Button size="sm" variant="ghost" iconOnly icon={l.visible ? <Eye size={13} /> : <EyeOff size={13} />} onClick={() => setLayer(l.id, { visible: !l.visible }, l.visible ? 'Hide layer' : 'Show layer')} disabled={!e.editable}>
                {l.visible ? `Hide ${l.name}` : `Show ${l.name}`}
              </Button>
              <Button size="sm" variant="ghost" iconOnly icon={l.locked ? <Lock size={13} /> : <LockOpen size={13} />} onClick={() => setLayer(l.id, { locked: !l.locked }, l.locked ? 'Unlock layer' : 'Lock layer')} disabled={!e.editable}>
                {l.locked ? `Unlock ${l.name}` : `Lock ${l.name}`}
              </Button>
              <Button size="sm" variant="ghost" iconOnly icon={<ArrowUp size={13} />} onClick={() => move(i, 1)} disabled={!e.editable || i === world.layers.length - 1}>Move layer up</Button>
              <Button size="sm" variant="ghost" iconOnly icon={<ArrowDown size={13} />} onClick={() => move(i, -1)} disabled={!e.editable || i === 0}>Move layer down</Button>
            </div>
          </li>
        ))}
      </ul>
      {(() => {
        const l = world.layers.find((x) => x.id === active);
        if (!l) return null;
        return (
          <div className="stack-sm" style={{ paddingTop: 6, borderTop: '1px solid var(--divider)' }}>
            <span className="vts-label">Layer “{l.name}”</span>
            <TextField label="Name" value={l.name} onChange={(v) => setLayer(l.id, { name: v }, 'Rename layer')} />
            <SelectField<WorldLayerRole>
              label="Role"
              value={l.role}
              onChange={(v) => setLayer(l.id, { role: v }, 'Layer role')}
              options={(Object.keys(ROLE_LABEL) as WorldLayerRole[]).map((r) => ({ value: r, label: ROLE_LABEL[r] }))}
              hint="Ground truth = shared + ground-truth layers; the twin's initial knowledge = shared + knowledge layers."
            />
            {e.editable && (
              <Button
                size="sm"
                variant="danger"
                icon={<Trash2 size={13} />}
                disabled={(counts.get(l.id) ?? 0) > 0 || world.layers.length === 1}
                title={(counts.get(l.id) ?? 0) > 0 ? 'Move or delete its objects first' : undefined}
                onClick={() => {
                  onChange({ ...world, layers: world.layers.filter((x) => x.id !== l.id) }, 'Delete layer');
                  onActive(world.layers.find((x) => x.id !== l.id)?.id ?? '');
                }}
              >
                Delete layer
              </Button>
            )}
          </div>
        );
      })()}
      {e.editable && (
        <Button
          size="sm"
          variant="ghost"
          icon={<Plus size={13} />}
          onClick={() => {
            const id = nextId('layer', world.layers.map((l) => l.id), '-');
            onChange({ ...world, layers: [...world.layers, { id, name: 'New layer', role: 'shared', visible: true, locked: false }] }, 'Add layer');
            onActive(id);
          }}
        >
          Add layer
        </Button>
      )}
    </div>
  );
}

const KIND_OPTIONS: WorldObjectKind[] = ['point', 'label', 'waypoint', 'line', 'polyline', 'polygon', 'region', 'zone', 'rect', 'image', 'node', 'edge', 'connector'];

export function ObjectInspector({ world, object, assets, onChange, onDelete, findings }: { world: WorldDocument; object: WorldObject; assets: BlueprintAssetDef[]; onChange: (o: WorldObject, label: string, key?: string) => void; onDelete: () => void; findings: string[] }) {
  const e = useEditor();
  const g = object.geometry;
  const set = (patch: Partial<WorldObject>, label: string) => onChange({ ...object, ...patch }, label, `${object.id}.${Object.keys(patch)[0]}`);
  const setG = (patch: Partial<typeof g>, label: string) => onChange({ ...object, geometry: { ...g, ...patch } }, label, `${object.id}.geom.${Object.keys(patch)[0]}`);
  const asset = assets.find((a) => a.id === object.asset);
  const base = blueprintRoute(e.id, e.version);
  const props = Object.fromEntries(Object.entries(object.properties).map(([k, v]) => [k, String(v)]));
  return (
    <div className="stack-sm">
      <InspectorSection title="Object">
        <TextField label="Name" value={object.name} onChange={(v) => set({ name: v }, 'Object name')} />
        <div className="vts-fgrid">
          <TextField label="Id" value={object.id} onChange={() => undefined} disabled mono hint="Rename via JSON view" />
          <SelectField<WorldObjectKind> label="Kind" value={object.kind} onChange={(v) => set({ kind: v }, 'Object kind')} options={KIND_OPTIONS.map((k) => ({ value: k, label: k }))} />
        </div>
        <TextField label="Semantic type" value={object.semanticType} onChange={(v) => set({ semanticType: v }, 'Semantic type')} mono hint="Domain meaning (wall, door, chamber…) read by adapters such as the robot simulator" />
        <SelectField label="Layer" value={object.layer} onChange={(v) => set({ layer: v }, 'Object layer')} options={world.layers.map((l) => ({ value: l.id, label: `${l.name} (${l.role})` }))} />
        <TagsField label="Tags" value={object.tags} onChange={(v) => set({ tags: v }, 'Object tags')} />
      </InspectorSection>
      <InspectorSection title="Asset binding">
        <SelectField label="Bound asset" value={object.asset ?? ''} onChange={(v) => set({ asset: v || null }, 'Asset binding')} options={assets.map((a) => ({ value: a.id, label: `${a.name || a.id} (${a.id})` }))} allowEmpty="— not bound —" />
        {asset && (
          <Link to={`${base}/build/structure?asset=${encodeURIComponent(asset.id)}`} className="small">
            Open {asset.name || asset.id} in Structure
          </Link>
        )}
      </InspectorSection>
      <InspectorSection title="Geometry">
        {isLinkKind(object.kind) ? (
          <div className="vts-fgrid">
            <SelectField label="From" value={g.from ?? ''} onChange={(v) => setG({ from: v }, 'Connector source')} options={world.objects.filter((o) => !isLinkKind(o.kind)).map((o) => ({ value: o.id, label: o.name || o.id }))} />
            <SelectField label="To" value={g.to ?? ''} onChange={(v) => setG({ to: v }, 'Connector target')} options={world.objects.filter((o) => !isLinkKind(o.kind)).map((o) => ({ value: o.id, label: o.name || o.id }))} />
            <CheckField label="Directed" checked={g.directed !== false} onChange={(v) => setG({ directed: v }, 'Connector direction')} />
          </div>
        ) : isPointKind(object.kind) || object.kind === 'node' || g.w !== undefined ? (
          <div className="vts-fgrid">
            <IntField label="x" value={g.x} onChange={(v) => setG({ x: v ?? 0 }, 'Move object')} unit={world.unit} />
            <IntField label="y" value={g.y} onChange={(v) => setG({ y: v ?? 0 }, 'Move object')} unit={world.unit} />
            {(object.kind === 'node' || g.w !== undefined) && (
              <>
                <IntField label="Width" value={g.w} onChange={(v) => setG({ w: Math.max(1, v ?? 1) }, 'Resize object')} unit={world.unit} min={1} />
                <IntField label="Height" value={g.h} onChange={(v) => setG({ h: Math.max(1, v ?? 1) }, 'Resize object')} unit={world.unit} min={1} />
              </>
            )}
          </div>
        ) : (
          <>
            <p className="xsmall subtle">{pointsOf(g).length} points · drag the vertices on the canvas</p>
            {(object.kind === 'line' || object.kind === 'polyline') && <IntField label="Stroke width" value={g.width ?? 0} onChange={(v) => setG({ width: Math.max(0, v ?? 0) }, 'Stroke width')} unit={world.unit} hint="Thickness used by the simulator (e.g. walls)" />}
          </>
        )}
        {object.kind === 'label' && <TextField label="Text" value={String(object.properties.text ?? '')} onChange={(v) => set({ properties: { ...object.properties, text: v } }, 'Label text')} />}
      </InspectorSection>
      <InspectorSection title="Properties">
        {object.semanticType === 'door' && (
          <Segmented
            label="Door state"
            value={String(object.properties.state ?? 'closed')}
            onChange={(v) => set({ properties: { ...object.properties, state: v } }, 'Door state')}
            options={[{ id: 'open', label: 'Open' }, { id: 'closed', label: 'Closed' }]}
          />
        )}
        <KeyValueEditor value={props} onChange={(v) => set({ properties: v }, 'Object properties')} />
      </InspectorSection>
      {findings.length > 0 && (
        <InspectorSection title="Problems">
          <ul className="vts-findings">{findings.map((f, i) => <li key={i} style={{ color: 'var(--crit)' }}>{f}</li>)}</ul>
        </InspectorSection>
      )}
      <InspectorSection title="Used by">
        <UsedBy deps={usedByWorldObject(e.doc, object.id)} />
      </InspectorSection>
      {e.editable && (
        <div>
          <Button size="sm" variant="danger" icon={<Trash2 size={13} />} onClick={onDelete}>Delete object</Button>
        </div>
      )}
    </div>
  );
}

export function WorldSettings({ world, onChange }: { world: WorldDocument; onChange: (w: WorldDocument, label: string) => void }) {
  return (
    <div className="stack-sm">
      <div className="vts-fgrid">
        <SelectField<WorldDocument['mode']>
          label="Mode"
          value={world.mode}
          onChange={(v) => onChange({ ...world, mode: v }, 'World mode')}
          options={[
            { value: 'spatial', label: '2D spatial (floor plan, site)' },
            { value: 'topology', label: 'Topology (process, network)' },
            { value: 'diagram', label: 'Generic diagram' },
          ]}
        />
        <SelectField<WorldDocument['unit']> label="Unit" value={world.unit} onChange={(v) => onChange({ ...world, unit: v }, 'World unit')} options={[{ value: 'mm', label: 'millimetres' }, { value: 'px', label: 'abstract units (px)' }]} />
      </div>
      <div className="vts-fgrid">
        <IntField label="Width" value={world.bounds.w} onChange={(v) => onChange({ ...world, bounds: { ...world.bounds, w: Math.max(1, v ?? 1) } }, 'World bounds')} unit={world.unit} min={1} />
        <IntField label="Height" value={world.bounds.h} onChange={(v) => onChange({ ...world, bounds: { ...world.bounds, h: Math.max(1, v ?? 1) } }, 'World bounds')} unit={world.unit} min={1} />
        <IntField label="Grid" value={world.grid?.size ?? 0} onChange={(v) => onChange({ ...world, grid: { size: Math.max(1, v ?? 1), snap: world.grid?.snap ?? true } }, 'Grid size')} unit={world.unit} min={1} />
        <CheckField label="Snap to grid" checked={world.grid?.snap ?? true} onChange={(v) => onChange({ ...world, grid: { size: world.grid?.size ?? 500, snap: v } }, 'Grid snapping')} />
      </div>
    </div>
  );
}

// ------------------------------------------------------------------ import
interface GeoFeature {
  geometry?: { type: string; coordinates: unknown };
  properties?: Record<string, unknown>;
}

export function ImportDialog({ open, onOpenChange, world, onImport }: { open: boolean; onOpenChange: (o: boolean) => void; world: WorldDocument; onImport: (w: WorldDocument, label: string) => void }) {
  const e = useEditor();
  const [file, setFile] = useState<File | null>(null);
  const [scale, setScale] = useState(world.unit === 'mm' ? '1000' : '1');
  const [layer, setLayer] = useState(world.layers.find((l) => l.role !== 'background')?.id ?? '');
  const [mode, setMode] = useState<'merge' | 'replace'>('merge');
  const [error, setError] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);
  const kind = !file ? null : /\.(png|jpe?g|svg)$/i.test(file.name) ? 'image' : /\.(geo)?json$/i.test(file.name) ? 'json' : 'unknown';
  const run = async () => {
    if (!file) return;
    setBusy(true);
    setError(null);
    try {
      if (kind === 'image') {
        const mime = file.type || (file.name.endsWith('.svg') ? 'image/svg+xml' : file.name.endsWith('.png') ? 'image/png' : 'image/jpeg');
        const buf = new Uint8Array(await file.arrayBuffer());
        let bin = '';
        for (let i = 0; i < buf.length; i += 0x8000) bin += String.fromCharCode(...buf.subarray(i, i + 0x8000));
        const blob = await blueprintApi.uploadBlob(mime, btoa(bin));
        let layers = world.layers;
        let bg = layers.find((l) => l.role === 'background');
        if (!bg) {
          bg = { id: nextId('background', layers.map((l) => l.id), '-'), name: 'Background', role: 'background', visible: true, locked: true };
          layers = [bg, ...layers];
        }
        const img: WorldObject = {
          id: nextId('plan-image', world.objects.map((o) => o.id), '-'),
          layer: bg.id,
          kind: 'image',
          semanticType: 'background',
          name: file.name,
          geometry: { x: world.bounds.x, y: world.bounds.y, w: world.bounds.w, h: world.bounds.h },
          properties: { src: blob.url, mime: blob.mime, sha256: blob.sha256 },
          tags: [],
          asset: null,
        };
        onImport({ ...world, layers, objects: [img, ...world.objects] }, `Import background ${file.name}`);
      } else if (kind === 'json') {
        const data = JSON.parse(await file.text()) as Record<string, unknown>;
        if (data.format === 'twin-world/1') {
          const w = data as unknown as WorldDocument;
          if (mode === 'replace') onImport(w, `Import world ${file.name}`);
          else {
            const taken = new Set(world.objects.map((o) => o.id));
            const layers = [...world.layers, ...w.layers.filter((l) => !world.layers.some((x) => x.id === l.id))];
            const objects = [...world.objects, ...w.objects.map((o) => (taken.has(o.id) ? { ...o, id: nextId(o.id, taken, '-') } : o))];
            onImport({ ...world, layers, objects }, `Merge world ${file.name}`);
          }
        } else if (data.type === 'FeatureCollection' || data.type === 'Feature') {
          const k = Number(scale) || 1;
          const features = (data.type === 'Feature' ? [data] : (data.features as GeoFeature[])) as GeoFeature[];
          const taken = new Set(world.objects.map((o) => o.id));
          const out: WorldObject[] = [];
          const pt = (c: unknown) => [Math.round(Number((c as number[])[0]) * k), Math.round(-Number((c as number[])[1]) * k)];
          for (const f of features) {
            const geom = f.geometry;
            if (!geom) continue;
            const name = String(f.properties?.name ?? f.properties?.id ?? geom.type);
            const semanticType = String(f.properties?.semanticType ?? f.properties?.type ?? '');
            const id = nextId(String(f.properties?.id ?? semanticType ?? 'feature').replace(/[^A-Za-z0-9_-]+/g, '-') || 'feature', taken, '-');
            taken.add(id);
            const base = { id, layer, name, semanticType, properties: {}, tags: [], asset: null };
            if (geom.type === 'Point') out.push({ ...base, kind: 'point', geometry: { x: pt(geom.coordinates)[0], y: pt(geom.coordinates)[1] } });
            else if (geom.type === 'LineString') out.push({ ...base, kind: 'polyline', geometry: { points: (geom.coordinates as unknown[]).map(pt), width: 0 } });
            else if (geom.type === 'Polygon') out.push({ ...base, kind: 'polygon', geometry: { points: ((geom.coordinates as unknown[][])[0] ?? []).slice(0, -1).map(pt) } });
          }
          if (out.length === 0) throw new Error('No Point, LineString or Polygon features found.');
          // Shift into the world's bounds (GeoJSON y grows north; the world's y grows south).
          const xs = out.flatMap((o) => (o.geometry.points ? o.geometry.points.map((p) => p[0]!) : [o.geometry.x ?? 0]));
          const ys = out.flatMap((o) => (o.geometry.points ? o.geometry.points.map((p) => p[1]!) : [o.geometry.y ?? 0]));
          const dx = world.bounds.x - Math.min(...xs);
          const dy = world.bounds.y - Math.min(...ys);
          const shifted = out.map((o) => ({ ...o, geometry: o.geometry.points ? { ...o.geometry, points: o.geometry.points.map((p) => [p[0]! + dx, p[1]! + dy]) } : { ...o.geometry, x: (o.geometry.x ?? 0) + dx, y: (o.geometry.y ?? 0) + dy } }));
          onImport({ ...world, objects: [...world.objects, ...shifted] }, `Import ${out.length} GeoJSON features`);
        } else {
          throw new Error('Unrecognised JSON: expected a twin-world/1 document or GeoJSON.');
        }
      }
      onOpenChange(false);
      setFile(null);
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    } finally {
      setBusy(false);
    }
  };
  return (
    <Dialog
      open={open}
      onOpenChange={onOpenChange}
      title="Import into the world"
      description="Images (PNG, JPEG, SVG) become a locked background layer — never semantic geometry. GeoJSON and twin-world/1 files become editable objects."
      footer={
        <>
          <Button onClick={() => onOpenChange(false)}>Cancel</Button>
          <Button variant="primary" icon={<FileUp size={14} />} disabled={!file || kind === 'unknown' || !e.editable} loading={busy} onClick={() => void run()}>Import</Button>
        </>
      }
    >
      <div className="stack-sm">
        <label className="vts-f">
          <span>File</span>
          <input type="file" accept=".png,.jpg,.jpeg,.svg,.json,.geojson" onChange={(ev) => setFile(ev.target.files?.[0] ?? null)} />
        </label>
        {kind === 'unknown' && <Callout tone="warning">Unsupported file type.</Callout>}
        {kind === 'image' && <p className="small muted">The image is stored content-addressed and stretched over the world bounds ({world.bounds.w} × {world.bounds.h} {world.unit}); adjust its rectangle in the inspector, then trace walls and areas on top of it.</p>}
        {kind === 'json' && (
          <div className="vts-fgrid">
            <SelectField label="Target layer (GeoJSON)" value={layer} onChange={setLayer} options={world.layers.filter((l) => l.role !== 'background').map((l) => ({ value: l.id, label: l.name }))} />
            <DecimalField label="Scale (GeoJSON units → world units)" value={scale} onChange={(v) => setScale(v ?? '1')} hint="e.g. 1000 for metres to mm" />
            <SelectField<'merge' | 'replace'> label="twin-world/1 file" value={mode} onChange={setMode} options={[{ value: 'merge', label: 'Merge objects into this world' }, { value: 'replace', label: 'Replace this world' }]} />
          </div>
        )}
        {error && <ErrorBlock error={new Error(error)} compact />}
      </div>
    </Dialog>
  );
}

// ------------------------------------------------------------------ simulator grids
const CELL_COLOUR: Record<string, string> = {
  '.': 'var(--surface)', '#': '#4b5563', o: '#d28b33', D: '#5cbf7f', d: '#a05a2c', '!': 'rgba(176,38,27,0.45)', '?': 'rgba(102,113,126,0.35)',
};

function Grid({ rows, cell, title }: { rows: string[]; cell: number; title: string }) {
  const h = rows.length;
  const w = rows[0]?.length ?? 0;
  return (
    <figure style={{ margin: 0 }} className="stack-sm">
      <figcaption className="small strong">{title}</figcaption>
      <svg viewBox={`0 0 ${w * cell} ${h * cell}`} style={{ width: '100%', maxHeight: 360, background: 'var(--bg-sunken)', borderRadius: 6, border: '1px solid var(--border)' }} role="img" aria-label={title}>
        {rows.flatMap((r, y) => [...r].map((c, x) => (c === '.' ? null : <rect key={`${x}-${y}`} x={x * cell} y={y * cell} width={cell} height={cell} fill={CELL_COLOUR[c] ?? 'magenta'} />)))}
      </svg>
    </figure>
  );
}

export function RasterPreview() {
  const e = useEditor();
  const raster = useWorldRaster(e.id, e.version, e.detail.documentSha256);
  if (e.doc.simulation?.kind !== 'mobile-robot') {
    return <EmptyState compact title="No simulator grid">The ground-truth / knowledge grids exist for the mobile-robot simulator (Simulation tab).</EmptyState>;
  }
  if (raster.isError) return <ErrorBlock error={raster.error} compact />;
  if (!raster.data) return <p className="small muted">Rasterising…</p>;
  const r = raster.data;
  const diff = r.groundTruth.reduce((n, row, y) => n + [...row].filter((c, x) => r.knowledge[y]?.[x] !== c).length, 0);
  return (
    <div className="stack-sm">
      <p className="small muted">
        Cells of {r.cellMm} mm ({r.width} × {r.height}) generated by the backend from the saved world. The simulator moves the physical robot in the <strong>ground truth</strong>;
        the twin starts from its <strong>initial knowledge</strong> and learns differences only through observations. <strong>{diff}</strong> cell{diff === 1 ? '' : 's'} differ.
      </p>
      <div className="grid-2">
        <Grid rows={r.groundTruth} cell={4} title="Ground truth (simulator)" />
        <Grid rows={r.knowledge} cell={4} title="Twin initial knowledge" />
      </div>
      <div className="row-wrap xsmall">
        {Object.entries(r.legend).map(([c, label]) => (
          <span key={c} className="row" style={{ gap: 4 }}>
            <span style={{ width: 12, height: 12, background: CELL_COLOUR[c], border: '1px solid var(--border)', display: 'inline-block' }} /> {label}
          </span>
        ))}
      </div>
      {r.findings.length > 0 && (
        <ul className="vts-findings">
          {r.findings.map((f, i) => (
            <li key={i}><StatusBadge tone={f.severity === 'error' ? 'critical' : 'warning'} label={f.code} /> {f.message}</li>
          ))}
        </ul>
      )}
    </div>
  );
}

// ------------------------------------------------------------------ mobile-robot simulation section
interface RobotSim {
  kind: 'mobile-robot';
  name?: string;
  description?: string;
  cellSize?: number;
  seed?: number;
  robot?: Record<string, string>;
  observation?: { sensorRange?: string; proximityRange?: string; updateIntervalMs?: number; observes?: string[]; noise?: string; discovery?: string; unobservableLayers?: string[] };
  mission?: { start?: string; targets?: string[] };
  timeline?: { id: string; at: string; kind: string; object: string; description?: string; notify?: boolean }[];
}

export function SimulationPanel({ world }: { world: WorldDocument }) {
  const e = useEditor();
  const sim = e.doc.simulation as unknown as RobotSim;
  const set = (patch: Partial<RobotSim>, label: string, key?: string) => e.update('simulation', (s) => ({ ...(s as object), ...patch }) as typeof s, { label, key });
  const points = useMemo(() => world.objects.filter((o) => isPointKind(o.kind)), [world.objects]);
  const eventObjects = useMemo(() => world.objects.filter((o) => world.layers.find((l) => l.id === o.layer)?.role === 'event'), [world]);
  if (sim?.kind !== 'mobile-robot') {
    return (
      <EmptyState
        compact
        title={`Simulator: ${sim?.kind ?? 'none'}`}
        action={
          e.editable && e.doc.world.mode === 'spatial' ? (
            <Button size="sm" onClick={() => e.update('simulation', () => ({ kind: 'mobile-robot', cellSize: 500, seed: 1, robot: { maxSpeed: '1.2', maxAccel: '1.5', batteryCapacityWh: '10', batteryStartPct: '100', reservePct: '20' }, observation: { sensorRange: '3', proximityRange: '1', updateIntervalMs: 0, observes: ['wall', 'obstacle', 'door'], noise: 'none', discovery: 'on-sight', unobservableLayers: [] }, mission: {}, timeline: [] }), { label: 'Use the mobile-robot simulator' })}>
              Use the mobile-robot simulator
            </Button>
          ) : undefined
        }
      >
        The mobile-robot simulator runs a robot through this spatial world (co-simulation). Event-script simulators are configured in Preview / Simulation.
      </EmptyState>
    );
  }
  const robot = sim.robot ?? {};
  const obs = sim.observation ?? {};
  const setRobot = (k: string, v: string | undefined) => set({ robot: { ...robot, [k]: v ?? '' } }, 'Robot parameter', `robot.${k}`);
  const setObs = (patch: Partial<NonNullable<RobotSim['observation']>>, label: string) => set({ observation: { ...obs, ...patch } }, label, `obs.${Object.keys(patch)[0]}`);
  return (
    <div className="stack">
      <div className="vts-fgrid">
        <IntField label="Grid cell" value={sim.cellSize} onChange={(v) => set({ cellSize: v }, 'Cell size')} unit="mm" min={50} hint="Resolution of the simulator and the twin's map" />
        <IntField label="Seed" value={sim.seed} onChange={(v) => set({ seed: v }, 'Seed')} hint="Deterministic simulation" />
      </div>
      <InspectorSection title="Robot">
        <div className="vts-fgrid">
          <DecimalField label="Max speed" value={robot.maxSpeed} onChange={(v) => setRobot('maxSpeed', v)} unit="m/s" />
          <DecimalField label="Max acceleration" value={robot.maxAccel} onChange={(v) => setRobot('maxAccel', v)} unit="m/s²" />
          <DecimalField label="Battery capacity" value={robot.batteryCapacityWh} onChange={(v) => setRobot('batteryCapacityWh', v)} unit="Wh" />
          <DecimalField label="Battery at start" value={robot.batteryStartPct} onChange={(v) => setRobot('batteryStartPct', v)} unit="%" />
          <DecimalField label="Reserve" value={robot.reservePct} onChange={(v) => setRobot('reservePct', v)} unit="%" />
        </div>
      </InspectorSection>
      <InspectorSection title="Observation model">
        <p className="xsmall subtle">How the twin learns about the world: only what the robot's sensors observe reaches the twin.</p>
        <div className="vts-fgrid">
          <DecimalField label="Sensor range" value={obs.sensorRange} onChange={(v) => setObs({ sensorRange: v }, 'Sensor range')} unit="m" />
          <DecimalField label="Proximity range" value={obs.proximityRange} onChange={(v) => setObs({ proximityRange: v }, 'Proximity range')} unit="m" />
          <IntField label="Update interval" value={obs.updateIntervalMs} onChange={(v) => setObs({ updateIntervalMs: v }, 'Update interval')} unit="ms" min={0} hint="0 = every simulation step" />
          <SelectField label="Noise" value={obs.noise ?? 'none'} onChange={(v) => setObs({ noise: v }, 'Noise')} options={[{ value: 'none', label: 'None (only supported model)' }]} />
        </div>
        <div className="row-wrap">
          {['wall', 'obstacle', 'door'].map((c) => (
            <CheckField key={c} label={`Observes ${c}s`} checked={(obs.observes ?? []).includes(c)} onChange={(v) => setObs({ observes: v ? [...(obs.observes ?? []), c] : (obs.observes ?? []).filter((x) => x !== c) }, 'Observed classes')} />
          ))}
        </div>
      </InspectorSection>
      <InspectorSection title="Mission">
        <SelectField label="Start" value={sim.mission?.start ?? ''} onChange={(v) => set({ mission: { ...sim.mission, start: v } }, 'Mission start')} options={points.map((o) => ({ value: o.id, label: `${o.name || o.id}` }))} allowEmpty="Choose a start point…" />
        <span className="vts-label">Targets (in order)</span>
        <div className="stack-sm">
          {(sim.mission?.targets ?? []).map((t, i) => (
            <div key={i} className="row">
              <select className="vts-select grow" value={t} disabled={!e.editable} aria-label={`Target ${i + 1}`} onChange={(ev) => set({ mission: { ...sim.mission, targets: (sim.mission?.targets ?? []).map((x, j) => (j === i ? ev.target.value : x)) } }, 'Mission target')}>
                {points.map((o) => <option key={o.id} value={o.id}>{o.name || o.id}</option>)}
              </select>
              <Button size="sm" variant="ghost" iconOnly icon={<Trash2 size={13} />} disabled={!e.editable} onClick={() => set({ mission: { ...sim.mission, targets: (sim.mission?.targets ?? []).filter((_, j) => j !== i) } }, 'Remove target')}>Remove target</Button>
            </div>
          ))}
          {e.editable && points.length > 0 && <Button size="sm" variant="ghost" icon={<Plus size={13} />} onClick={() => set({ mission: { ...sim.mission, targets: [...(sim.mission?.targets ?? []), points[0]!.id] } }, 'Add target')}>Add target</Button>}
        </div>
      </InspectorSection>
      <InspectorSection title="Timeline (world changes during the run)">
        <p className="xsmall subtle">Event-layer geometry applied at a time, e.g. a temporary no-fly zone. The twin learns it only if the event notifies it or the robot observes it.</p>
        {(sim.timeline ?? []).map((t, i) => {
          const upd = (patch: Partial<typeof t>) => set({ timeline: (sim.timeline ?? []).map((x, j) => (j === i ? { ...x, ...patch } : x)) }, 'Timeline event', `tl.${i}`);
          return (
            <div key={i} className="vts-fgrid" style={{ borderTop: '1px solid var(--divider)', paddingTop: 6 }}>
              <DecimalField label="At" value={t.at} onChange={(v) => upd({ at: v ?? '0' })} unit="s" />
              <SelectField label="Object" value={t.object} onChange={(v) => upd({ object: v })} options={eventObjects.map((o) => ({ value: o.id, label: o.name || o.id }))} allowEmpty="Choose event geometry…" />
              <TextField label="Description" value={t.description ?? ''} onChange={(v) => upd({ description: v })} />
              <CheckField label="Notify the twin" checked={!!t.notify} onChange={(v) => upd({ notify: v })} />
            </div>
          );
        })}
        {e.editable && (
          <Button size="sm" variant="ghost" icon={<Plus size={13} />} disabled={eventObjects.length === 0} title={eventObjects.length === 0 ? 'Draw geometry on an event layer first' : undefined} onClick={() => set({ timeline: [...(sim.timeline ?? []), { id: nextId('event', (sim.timeline ?? []).map((x) => x.id), '-'), at: '30', kind: 'hazard', object: eventObjects[0]!.id, notify: true }] }, 'Add timeline event')}>
            Add timeline event
          </Button>
        )}
      </InspectorSection>
    </div>
  );
}
