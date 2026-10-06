/**
 * Build → World & Layout: a generic scene editor (2D spatial, topology or diagram) over
 * twin-world/1 — layers with roles, geometry, topology, asset placement and semantic tags.
 * Domain tool palettes are data (templates/palettes.json); every tool creates an ordinary
 * object (geometry kind + semantic type). The world is outside the verified core.
 */
import {
  Circle,
  Crosshair,
  Diamond,
  FileUp,
  Hand,
  Maximize,
  Minus,
  MousePointer2,
  Pentagon,
  Plus,
  RectangleHorizontal,
  Share2,
  Spline,
  Square,
  SquareDashed,
  Type,
  Waypoints,
  ZoomIn,
  ZoomOut,
  type LucideIcon,
} from 'lucide-react';
import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import { useSearchParams } from 'react-router-dom';
import { useBlueprintValidation, usePalettes } from '@/api/blueprints';
import type { WorldDocument, WorldObject } from '@/api/types';
import { Button, Segmented, Tabs } from '@/design';
import { nextId, useEditor, useSection } from '../editor';
import { usedByWorldObject } from '../refs';
import { ConfirmDelete, EdPage, JsonSectionView, Pane } from '../ui';
import { useWorkspace } from '../workspace';
import { emptyWorld, isLinkKind, moveGeometry } from '../world/geometry';
import { ImportDialog, LayersPanel, ObjectInspector, RasterPreview, SimulationPanel, WorldSettings } from '../world/panels';
import { WorldCanvas, type CanvasApi, type ToolSpec } from '../world/WorldCanvas';

const BASE_TOOLS: (ToolSpec & { icon: LucideIcon; key: string })[] = [
  { id: 'select', label: 'Select', kind: 'select', semanticType: '', icon: MousePointer2, key: 'v' },
  { id: 'pan', label: 'Pan', kind: 'pan', semanticType: '', icon: Hand, key: 'h' },
  { id: 'point', label: 'Point', kind: 'point', semanticType: 'marker', icon: Circle, key: 'p' },
  { id: 'line', label: 'Line', kind: 'line', semanticType: 'line', icon: Minus, key: 'l' },
  { id: 'polyline', label: 'Polyline', kind: 'polyline', semanticType: 'line', icon: Spline, key: 'y' },
  { id: 'rect', label: 'Rectangle', kind: 'rect', semanticType: 'area', icon: Square, key: 'r' },
  { id: 'polygon', label: 'Polygon', kind: 'polygon', semanticType: 'area', icon: Pentagon, key: 'g' },
  { id: 'region', label: 'Region', kind: 'zone', semanticType: 'zone', icon: SquareDashed, key: 'e' },
  { id: 'text', label: 'Text', kind: 'label', semanticType: 'label', icon: Type, key: 't' },
  { id: 'waypoint', label: 'Waypoint', kind: 'waypoint', semanticType: 'waypoint', icon: Waypoints, key: 'w' },
  { id: 'node', label: 'Node', kind: 'node', semanticType: 'node', icon: RectangleHorizontal, key: 'n' },
  { id: 'connector', label: 'Connector', kind: 'connector', semanticType: 'connection', icon: Share2, key: 'c' },
];

type SideTab = 'inspector' | 'layers' | 'world' | 'simulation';
type MainTab = 'canvas' | 'grids' | 'json';

export default function WorldPage() {
  const e = useEditor();
  const ws = useWorkspace();
  const [world, setWorld] = useSection('world');
  const palettes = usePalettes();
  const validation = useBlueprintValidation(e.id, e.version);
  const [params, setParams] = useSearchParams();
  const [toolId, setToolId] = useState('select');
  const [assetTool, setAssetTool] = useState<string>('');
  const [view, setView] = useState<'all' | 'truth' | 'knowledge'>('all');
  const [side, setSide] = useState<SideTab>('inspector');
  const [main, setMain] = useState<MainTab>('canvas');
  const [activeLayer, setActiveLayer] = useState(() => world.layers.find((l) => l.role === 'shared')?.id ?? world.layers[0]?.id ?? '');
  const [importOpen, setImportOpen] = useState(false);
  const [deleting, setDeleting] = useState<string[] | null>(null);
  const canvas = useRef<CanvasApi | null>(null);
  const selection = useMemo(() => (params.get('object') ?? '').split(',').filter(Boolean), [params]);
  const setSelection = useCallback(
    (ids: string[]) => {
      const p = new URLSearchParams(params);
      if (ids.length) p.set('object', ids.join(','));
      else p.delete('object');
      setParams(p, { replace: true });
      if (ids.length === 1) setSide('inspector');
    },
    [params, setParams],
  );

  const domainTools: (ToolSpec & { group: string; description?: string })[] = useMemo(() => {
    const p = palettes.data;
    if (!p) return [];
    const ids = p.domains[e.doc.identity.domain] ?? p.domains.generic ?? ['generic'];
    return ids
      .filter((id) => id !== 'generic')
      .flatMap((id) => (p.palettes[id]?.tools ?? []).map((t) => ({ ...t, id: `${id}:${t.id}`, group: p.palettes[id]!.label, kind: t.kind, semanticType: t.semanticType })));
  }, [palettes.data, e.doc.identity.domain]);

  const assets = e.doc.structure.assets;
  const tool: ToolSpec = useMemo(() => {
    if (toolId === 'asset') {
      const a = assets.find((x) => x.id === assetTool);
      return { id: `asset:${assetTool}`, label: 'Asset', kind: 'point', semanticType: 'asset', asset: a?.id, assetName: a?.name };
    }
    return BASE_TOOLS.find((t) => t.id === toolId) ?? domainTools.find((t) => t.id === toolId) ?? BASE_TOOLS[0]!;
  }, [toolId, assetTool, assets, domainTools]);

  const findings = useMemo(() => {
    const m = new Map<string, 'error' | 'warning'>();
    for (const f of validation.data?.findings ?? []) if (f.section === 'world' && f.target) m.set(f.target, m.get(f.target) === 'error' ? 'error' : f.severity);
    return m;
  }, [validation.data]);
  const objFindings = (id: string) => (validation.data?.findings ?? []).filter((f) => f.section === 'world' && f.target === id).map((f) => `${f.code}: ${f.message}`);

  const commit = useCallback((next: WorldDocument, label: string) => setWorld(next, { label }), [setWorld]);

  const duplicate = useCallback(
    (ids: string[]) => {
      const taken = new Set(world.objects.map((o) => o.id));
      const step = world.grid?.size ?? 10;
      const copies: WorldObject[] = [];
      for (const id of ids) {
        const o = world.objects.find((x) => x.id === id);
        if (!o || isLinkKind(o.kind)) continue;
        const nid = nextId(o.id, taken, '-');
        taken.add(nid);
        copies.push({ ...o, id: nid, name: `${o.name} (copy)`, geometry: moveGeometry(o, step, step) });
      }
      if (copies.length) {
        commit({ ...world, objects: [...world.objects, ...copies] }, `Duplicate ${copies.length} object(s)`);
        setSelection(copies.map((c) => c.id));
      }
    },
    [world, commit, setSelection],
  );

  const doDelete = (ids: string[]) => {
    const set = new Set(ids);
    commit({ ...world, objects: world.objects.filter((o) => !set.has(o.id) && !(isLinkKind(o.kind) && (set.has(o.geometry.from ?? '') || set.has(o.geometry.to ?? '')))) }, ids.length === 1 ? `Delete ${ids[0]}` : `Delete ${ids.length} objects`);
    setSelection([]);
  };
  const requestDelete = (ids: string[]) => {
    const deps = ids.flatMap((id) => usedByWorldObject(e.doc, id));
    if (deps.length > 0) setDeleting(ids);
    else doDelete(ids);
  };

  // Tool shortcuts (when not typing).
  useEffect(() => {
    const onKey = (ev: KeyboardEvent) => {
      const t = ev.target as HTMLElement | null;
      if (ev.metaKey || ev.ctrlKey || ev.altKey || t instanceof HTMLInputElement || t instanceof HTMLTextAreaElement || t instanceof HTMLSelectElement || t?.isContentEditable || t?.closest('.cm-editor')) return;
      const tl = BASE_TOOLS.find((x) => x.key === ev.key.toLowerCase());
      if (tl && main === 'canvas') {
        setToolId(tl.id);
      }
    };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, [main]);

  const selected = selection.length === 1 ? world.objects.find((o) => o.id === selection[0]) : undefined;
  const hasWorld = world && Array.isArray(world.layers);

  return (
    <EdPage
      wide
      fill
      title="World & Layout"
      description="Where the twin lives: floor plan, site or topology, with layers that separate physical ground truth from what the twin initially knows. Geometry here is versioned and integrity-protected, not formally verified."
      actions={
        <>
          <Segmented
            label="World view"
            value={view}
            onChange={setView}
            options={[
              { id: 'all', label: 'All layers' },
              { id: 'truth', label: 'Ground truth' },
              { id: 'knowledge', label: 'Twin knowledge' },
            ]}
          />
          <Button size="sm" icon={<FileUp size={14} />} onClick={() => setImportOpen(true)} disabled={!e.editable}>Import</Button>
          {ws.expert && <Button size="sm" variant="ghost" onClick={() => setMain(main === 'json' ? 'canvas' : 'json')}>{main === 'json' ? 'Canvas' : 'JSON view'}</Button>}
        </>
      }
      guide={
        <>
          Choose a tool (keys in brackets), draw on the active layer, and bind objects to assets in the inspector. Use <strong>Ground truth</strong> and <strong>Twin knowledge</strong> to compare what
          physically exists with what the twin is told; an outdated plan goes on a knowledge layer. Space+drag pans, the wheel zooms, Delete removes, Ctrl+D duplicates, arrows nudge.
        </>
      }
    >
      {!hasWorld ? (
        <Button onClick={() => commit(emptyWorld('spatial'), 'Create world')}>Create a world</Button>
      ) : (
        <div className="vts-ed-fill">
          <Tabs<MainTab>
            label="World views"
            value={main}
            onChange={setMain}
            tabs={[
              { id: 'canvas', label: 'Layout' },
              { id: 'grids', label: 'Ground truth vs knowledge (simulator)' },
              ...(ws.expert ? [{ id: 'json' as MainTab, label: 'JSON' }] : []),
            ]}
          />
          {main === 'json' ? (
            <JsonSectionView value={world} label="World" onApply={(v) => commit(v, 'Edit world (JSON)')} />
          ) : main === 'grids' ? (
            <Pane title="Rasterised simulator grids"><RasterPreview /></Pane>
          ) : (
            <div className="vts-ed-split vts-ed-split--wide-inspector" style={{ alignItems: 'stretch' }}>
              <div className="stack-sm" style={{ minWidth: 0, display: 'flex', flexDirection: 'column' }}>
                <div className="vts-toolbar" role="toolbar" aria-label="Drawing tools">
                  {BASE_TOOLS.map((t) => (
                    <button key={t.id} type="button" className="vts-tool" aria-pressed={toolId === t.id} disabled={!e.editable && t.id !== 'select' && t.id !== 'pan'} onClick={() => setToolId(t.id)} title={`${t.label} (${t.key.toUpperCase()})`}>
                      <t.icon size={15} aria-hidden="true" />
                      <span className="sr-only">{t.label}</span>
                    </button>
                  ))}
                  <span className="vts-toolbar__sep" />
                  <label className="row xsmall" title="Asset placement: click to place a marker bound to the asset">
                    <Crosshair size={14} aria-hidden="true" />
                    <select
                      className="vts-select"
                      style={{ minHeight: 28, maxWidth: 170 }}
                      aria-label="Place asset"
                      value={toolId === 'asset' ? assetTool : ''}
                      disabled={!e.editable}
                      onChange={(ev) => {
                        setAssetTool(ev.target.value);
                        setToolId(ev.target.value ? 'asset' : 'select');
                      }}
                    >
                      <option value="">Place asset…</option>
                      {assets.map((a) => (
                        <option key={a.id} value={a.id}>
                          {a.name || a.id}{world.objects.some((o) => o.asset === a.id) ? ' ✓' : ''}
                        </option>
                      ))}
                    </select>
                  </label>
                  {domainTools.length > 0 && <span className="vts-toolbar__sep" />}
                  {domainTools.map((t) => (
                    <button key={t.id} type="button" className="vts-tool" aria-pressed={toolId === t.id} disabled={!e.editable} onClick={() => setToolId(t.id)} title={`${t.group}: ${t.label}${t.description ? ` — ${t.description}` : ''}`}>
                      <Diamond size={12} aria-hidden="true" />
                      {t.label}
                    </button>
                  ))}
                  <span className="grow" />
                  <button type="button" className="vts-tool" onClick={() => canvas.current?.zoomBy(1.25)} title="Zoom in (+)"><ZoomIn size={15} /><span className="sr-only">Zoom in</span></button>
                  <button type="button" className="vts-tool" onClick={() => canvas.current?.zoomBy(0.8)} title="Zoom out (-)"><ZoomOut size={15} /><span className="sr-only">Zoom out</span></button>
                  <button type="button" className="vts-tool" onClick={() => canvas.current?.fit()} title="Fit to content (F)"><Maximize size={15} /><span className="sr-only">Fit to content</span></button>
                </div>
                <div style={{ flex: '1 1 auto', minHeight: 380 }}>
                  <WorldCanvas
                    world={world}
                    view={view}
                    tool={tool}
                    activeLayer={activeLayer}
                    selection={selection}
                    onSelect={setSelection}
                    onCommit={commit}
                    onCreated={(id) => {
                      setSelection([id]);
                      setToolId('select');
                    }}
                    onDeleteRequest={requestDelete}
                    onDuplicate={duplicate}
                    findings={findings}
                    editable={e.editable}
                    apiRef={(api) => {
                      canvas.current = api;
                    }}
                    ariaLabel={`World canvas, ${world.mode}, ${world.objects.length} objects. Tools: ${tool.label}.`}
                  />
                </div>
                <p className="xsmall subtle">
                  {world.objects.length} objects on {world.layers.length} layers · active layer “{world.layers.find((l) => l.id === activeLayer)?.name ?? '—'}” · drawing with {tool.label}
                </p>
              </div>
              <Pane className="vts-inspector" flush>
                <div style={{ padding: '0 8px' }}>
                  <Tabs<SideTab>
                    label="World panels"
                    value={side}
                    onChange={setSide}
                    tabs={[
                      { id: 'inspector', label: 'Inspector' },
                      { id: 'layers', label: 'Layers' },
                      { id: 'world', label: 'World' },
                      { id: 'simulation', label: 'Simulation' },
                    ]}
                  />
                </div>
                <div className="vts-ed-pane__body">
                  {side === 'inspector' &&
                    (selected ? (
                      <ObjectInspector
                        key={selected.id}
                        world={world}
                        object={selected}
                        assets={assets}
                        findings={objFindings(selected.id)}
                        onChange={(o, label, key) => setWorld((w) => ({ ...w, objects: w.objects.map((x) => (x.id === o.id ? o : x)) }), { label, key })}
                        onDelete={() => requestDelete([selected.id])}
                      />
                    ) : selection.length > 1 ? (
                      <div className="stack-sm small">
                        <p><strong>{selection.length}</strong> objects selected.</p>
                        <div className="row-wrap">
                          <Button size="sm" onClick={() => duplicate(selection)} disabled={!e.editable}>Duplicate</Button>
                          <Button size="sm" variant="danger" onClick={() => requestDelete(selection)} disabled={!e.editable}>Delete</Button>
                        </div>
                        <label className="vts-f">
                          <span>Move to layer</span>
                          <select className="vts-select" disabled={!e.editable} onChange={(ev) => ev.target.value && commit({ ...world, objects: world.objects.map((o) => (selection.includes(o.id) ? { ...o, layer: ev.target.value } : o)) }, 'Move to layer')} value="">
                            <option value="">Choose…</option>
                            {world.layers.map((l) => <option key={l.id} value={l.id}>{l.name}</option>)}
                          </select>
                        </label>
                      </div>
                    ) : (
                      <p className="small muted">Select an object (click, Shift-click or drag a marquee), or pick a tool and draw.</p>
                    ))}
                  {side === 'layers' && <LayersPanel world={world} active={activeLayer} onActive={setActiveLayer} onChange={commit} />}
                  {side === 'world' && <WorldSettings world={world} onChange={commit} />}
                  {side === 'simulation' && <SimulationPanel world={world} />}
                </div>
              </Pane>
            </div>
          )}
        </div>
      )}
      <ImportDialog open={importOpen} onOpenChange={setImportOpen} world={world} onImport={commit} />
      <ConfirmDelete
        open={deleting !== null}
        onOpenChange={(o) => !o && setDeleting(null)}
        title={deleting && deleting.length === 1 ? `Delete ${deleting[0]}?` : `Delete ${deleting?.length ?? 0} objects?`}
        dependencies={(deleting ?? []).flatMap((id) => usedByWorldObject(e.doc, id))}
        onConfirm={() => deleting && doDelete(deleting)}
      />
      {e.editable && world.layers.length === 0 && (
        <Button size="sm" icon={<Plus size={13} />} onClick={() => commit({ ...world, layers: [{ id: 'layout', name: 'Layout', role: 'shared', visible: true, locked: false }] }, 'Add layer')}>Add a layer</Button>
      )}
    </EdPage>
  );
}
