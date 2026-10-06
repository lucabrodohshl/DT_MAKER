/**
 * The 2D world canvas (SVG): renders layers and objects in world units and implements the
 * drawing tools — select (click, shift-click, marquee; move with grid snapping and
 * alignment guides; corner and vertex handles), pan (tool, middle button or Space+drag),
 * zoom at the cursor, and creation of points, labels, waypoints, nodes, lines, polylines,
 * rectangles, polygons/regions and connectors. Every change is committed as one edit of the
 * world section (one undo step); transient drags are previewed locally.
 */
import { useCallback, useEffect, useMemo, useRef, useState, type PointerEvent as RPointerEvent, type ReactNode } from 'react';
import type { WorldDocument, WorldGeometry, WorldLayer, WorldObject, WorldObjectKind } from '@/api/types';
import {
  alignGuides,
  bboxOf,
  centerOf,
  hits,
  isAreaKind,
  isLinkKind,
  isPointKind,
  isRectLike,
  layerVisibleInView,
  moveGeometry,
  pointsOf,
  resizeBox,
  round,
  snap,
  unionBox,
  type Box,
  type Guide,
} from './geometry';

export interface ToolSpec {
  id: string;
  label: string;
  kind: WorldObjectKind | 'select' | 'pan';
  semanticType: string;
  layerRole?: string;
  defaults?: { width?: number; properties?: Record<string, string | number | boolean>; w?: number; h?: number };
  /** Asset placement: bind the new object to this asset. */
  asset?: string;
  assetName?: string;
}

export interface CanvasApi {
  fit: () => void;
  zoomBy: (f: number) => void;
}

interface Props {
  world: WorldDocument;
  view: 'all' | 'truth' | 'knowledge';
  tool: ToolSpec;
  activeLayer: string;
  selection: string[];
  onSelect: (ids: string[]) => void;
  onCommit: (next: WorldDocument, label: string) => void;
  onCreated: (id: string) => void;
  onDeleteRequest: (ids: string[]) => void;
  onDuplicate: (ids: string[]) => void;
  findings: Map<string, 'error' | 'warning'>;
  editable: boolean;
  apiRef?: (api: CanvasApi) => void;
  /** Extra overlay in world coordinates (e.g. raster cells). */
  overlay?: ReactNode;
  ariaLabel: string;
}

type Drag =
  | { kind: 'pan'; sx: number; sy: number; ox: number; oy: number }
  | { kind: 'move'; wx: number; wy: number; dx: number; dy: number; guides: Guide[] }
  | { kind: 'marquee'; x0: number; y0: number; x1: number; y1: number; additive: boolean }
  | { kind: 'resize'; id: string; handle: 'nw' | 'ne' | 'sw' | 'se'; box: Box }
  | { kind: 'vertex'; id: string; index: number; points: [number, number][] }
  | { kind: 'create-box'; x0: number; y0: number; x1: number; y1: number }
  | { kind: 'create-line'; x0: number; y0: number; x1: number; y1: number };

const STYLE: Record<string, { fill: string; stroke: string; dash?: string; text?: string }> = {
  floor: { fill: 'var(--surface)', stroke: 'var(--border-strong)' },
  free: { fill: 'var(--surface)', stroke: 'var(--border-strong)' },
  wall: { fill: '#5b6573', stroke: '#3d4652' },
  obstacle: { fill: '#d28b33', stroke: '#8a5300' },
  hazard: { fill: 'rgba(176,38,27,0.18)', stroke: 'var(--crit)', dash: '5 3' },
  unknown: { fill: 'rgba(102,113,126,0.25)', stroke: 'var(--text-subtle)', dash: '3 3' },
  room: { fill: 'rgba(29,79,145,0.04)', stroke: 'var(--accent)', dash: '7 4' },
  zone: { fill: 'rgba(29,79,145,0.05)', stroke: 'var(--accent)', dash: '7 4' },
  region: { fill: 'rgba(29,79,145,0.06)', stroke: 'var(--accent)', dash: '4 3' },
  chamber: { fill: 'rgba(29,79,145,0.08)', stroke: 'var(--accent)' },
  heater: { fill: 'rgba(195,90,0,0.28)', stroke: '#c35a00' },
  tank: { fill: 'var(--surface)', stroke: 'var(--accent)' },
  pump: { fill: 'var(--accent-soft)', stroke: 'var(--accent)' },
  valve: { fill: 'var(--surface)', stroke: 'var(--text-muted)' },
  pipe: { fill: 'none', stroke: 'var(--accent)' },
  cable: { fill: 'none', stroke: '#c35a00' },
};
const POINT_COLOUR: Record<string, string> = {
  start: 'var(--ok)', target: 'var(--crit)', waypoint: 'var(--accent)', sensor: 'var(--formal)', asset: 'var(--accent)', marker: 'var(--text-muted)', robot: 'var(--accent)',
};

function styleOf(o: WorldObject, layer: WorldLayer | undefined) {
  let s = STYLE[o.semanticType] ?? (isAreaKind(o.kind) ? STYLE.zone! : { fill: 'var(--surface)', stroke: 'var(--border-strong)' });
  if (o.semanticType === 'door') {
    const open = o.properties.state === 'open';
    s = { fill: open ? 'rgba(26,113,70,0.35)' : 'rgba(160,90,44,0.75)', stroke: open ? 'var(--ok)' : '#7a3e14', dash: open ? '4 3' : undefined };
  }
  const opacity = layer?.role === 'event' ? 0.55 : layer?.role === 'annotation' ? 0.9 : 1;
  const dash = layer?.role === 'knowledge' ? '8 4' : layer?.role === 'event' ? '2 3' : s.dash;
  const stroke = layer?.role === 'knowledge' ? 'var(--formal)' : layer?.role === 'event' ? 'var(--warn)' : s.stroke;
  return { ...s, stroke, dash, opacity };
}

export function WorldCanvas({ world, view, tool, activeLayer, selection, onSelect, onCommit, onCreated, onDeleteRequest, onDuplicate, findings, editable, apiRef, overlay, ariaLabel }: Props) {
  const host = useRef<HTMLDivElement>(null);
  const [size, setSize] = useState({ w: 800, h: 500 });
  const [cam, setCam] = useState<{ x: number; y: number; s: number } | null>(null);
  const [drag, setDrag] = useState<Drag | null>(null);
  const [draft, setDraft] = useState<[number, number][]>([]); // polyline / polygon points in progress
  const [linkFrom, setLinkFrom] = useState<string | null>(null);
  const [hover, setHover] = useState<[number, number] | null>(null);
  const [space, setSpace] = useState(false);
  const grid = world.grid?.size ?? (world.unit === 'mm' ? 500 : 20);
  const snapOn = world.grid?.snap ?? true;

  const layers = useMemo(() => new Map(world.layers.map((l) => [l.id, l])), [world.layers]);
  const byId = useMemo(() => new Map(world.objects.map((o) => [o.id, o])), [world.objects]);
  const visible = useCallback(
    (o: WorldObject) => {
      const l = layers.get(o.layer);
      return !!l && l.visible && layerVisibleInView(l.role, view);
    },
    [layers, view],
  );
  const editableObj = useCallback((o: WorldObject) => editable && !layers.get(o.layer)?.locked, [editable, layers]);

  useEffect(() => {
    const el = host.current;
    if (!el) return;
    const ro = new ResizeObserver(() => setSize({ w: el.clientWidth, h: el.clientHeight }));
    ro.observe(el);
    setSize({ w: el.clientWidth, h: el.clientHeight });
    return () => ro.disconnect();
  }, []);

  const fit = useCallback(() => {
    const content = unionBox([world.bounds, ...world.objects.filter(visible).map((o) => bboxOf(o, byId))]) ?? world.bounds;
    const pad = 32;
    const s = Math.min((size.w - pad * 2) / Math.max(1, content.w), (size.h - pad * 2) / Math.max(1, content.h));
    setCam({ s, x: content.x - (size.w / s - content.w) / 2, y: content.y - (size.h / s - content.h) / 2 });
  }, [world.bounds, world.objects, visible, byId, size]);

  const camera = cam ?? (() => {
    const b = world.bounds;
    const s = Math.min((size.w - 64) / Math.max(1, b.w), (size.h - 64) / Math.max(1, b.h)) || 1;
    return { s, x: b.x - (size.w / s - b.w) / 2, y: b.y - (size.h / s - b.h) / 2 };
  })();

  const zoomAt = useCallback(
    (f: number, sx: number, sy: number) => {
      const c = cam ?? camera;
      const s = Math.min(Math.max(c.s * f, 1e-4), 400);
      const wx = c.x + sx / c.s;
      const wy = c.y + sy / c.s;
      setCam({ s, x: wx - sx / s, y: wy - sy / s });
    },
    [cam, camera],
  );

  useEffect(() => {
    apiRef?.({ fit, zoomBy: (f) => zoomAt(f, size.w / 2, size.h / 2) });
  }, [apiRef, fit, zoomAt, size]);

  const toWorld = (ev: { clientX: number; clientY: number }): [number, number] => {
    const r = host.current!.getBoundingClientRect();
    return [camera.x + (ev.clientX - r.left) / camera.s, camera.y + (ev.clientY - r.top) / camera.s];
  };
  const tol = 6 / camera.s;

  const topHit = (wx: number, wy: number, predicate: (o: WorldObject) => boolean = () => true): WorldObject | null => {
    for (let i = world.objects.length - 1; i >= 0; i--) {
      const o = world.objects[i]!;
      if (!visible(o) || !predicate(o)) continue;
      if (layers.get(o.layer)?.role === 'background' && tool.kind !== 'select') continue;
      if (hits(o, wx, wy, tol, byId)) return o;
    }
    return null;
  };

  const layerForNew = (): string => {
    if (tool.layerRole) {
      const l = world.layers.find((x) => x.role === tool.layerRole && !x.locked);
      if (l) return l.id;
    }
    const active = layers.get(activeLayer);
    if (active && !active.locked && active.role !== 'background') return active.id;
    return world.layers.find((l) => !l.locked && l.role !== 'background')?.id ?? world.layers[0]?.id ?? '';
  };

  const newObject = (kind: WorldObjectKind, geometry: WorldGeometry): WorldObject => {
    const base = (tool.asset ? tool.asset : tool.semanticType || kind).replace(/[^A-Za-z0-9_-]+/g, '-');
    const taken = new Set(world.objects.map((o) => o.id));
    let id = base;
    for (let i = 2; taken.has(id); i++) id = `${base}-${i}`;
    const count = world.objects.filter((o) => o.semanticType === tool.semanticType).length + 1;
    return {
      id,
      layer: layerForNew(),
      kind,
      semanticType: tool.semanticType,
      name: tool.assetName ?? (kind === 'label' ? 'Label' : `${tool.label} ${count}`),
      geometry,
      properties: { ...(tool.defaults?.properties ?? {}), ...(kind === 'label' ? { text: 'Label' } : {}) },
      asset: tool.asset ?? null,
      tags: [],
    };
  };

  const commitNew = (o: WorldObject) => {
    if (!o.layer) return;
    onCommit({ ...world, objects: [...world.objects, o] }, `Add ${o.semanticType || o.kind}`);
    onCreated(o.id);
  };

  // ------------------------------------------------------------------ pointer handling
  const onPointerDown = (ev: RPointerEvent<SVGSVGElement>) => {
    host.current?.querySelector<SVGSVGElement>('svg')?.focus();
    const [wx, wy] = toWorld(ev);
    if (ev.button === 1 || tool.kind === 'pan' || space) {
      setDrag({ kind: 'pan', sx: ev.clientX, sy: ev.clientY, ox: camera.x, oy: camera.y });
      (ev.target as Element).setPointerCapture?.(ev.pointerId);
      return;
    }
    if (ev.button !== 0) return;
    const sx = snap(wx, grid, snapOn);
    const sy = snap(wy, grid, snapOn);
    if (tool.kind === 'select') {
      // Handles of the single selected object first.
      if (selection.length === 1 && editable) {
        const o = byId.get(selection[0]!);
        if (o && editableObj(o)) {
          if (isRectLike(o) || (o.kind === 'node' && o.geometry.w)) {
            const b = bboxOf(o)!;
            const corners: ['nw' | 'ne' | 'sw' | 'se', number, number][] = [['nw', b.x, b.y], ['ne', b.x + b.w, b.y], ['sw', b.x, b.y + b.h], ['se', b.x + b.w, b.y + b.h]];
            for (const [h, cx, cy] of corners) {
              if (Math.abs(wx - cx) <= tol * 1.4 && Math.abs(wy - cy) <= tol * 1.4) {
                setDrag({ kind: 'resize', id: o.id, handle: h, box: b });
                (ev.target as Element).setPointerCapture?.(ev.pointerId);
                return;
              }
            }
          }
          const ps = pointsOf(o.geometry);
          const vi = ps.findIndex(([px, py]) => Math.hypot(px - wx, py - wy) <= tol * 1.4);
          if (vi >= 0 && !isPointKind(o.kind)) {
            setDrag({ kind: 'vertex', id: o.id, index: vi, points: ps });
            (ev.target as Element).setPointerCapture?.(ev.pointerId);
            return;
          }
        }
      }
      const hit = topHit(wx, wy);
      if (hit) {
        if (ev.shiftKey) {
          onSelect(selection.includes(hit.id) ? selection.filter((s) => s !== hit.id) : [...selection, hit.id]);
          return;
        }
        const sel = selection.includes(hit.id) ? selection : [hit.id];
        if (!selection.includes(hit.id)) onSelect(sel);
        if (editable && sel.every((id) => byId.get(id) && editableObj(byId.get(id)!))) {
          setDrag({ kind: 'move', wx, wy, dx: 0, dy: 0, guides: [] });
          (ev.target as Element).setPointerCapture?.(ev.pointerId);
        }
        return;
      }
      if (!ev.shiftKey) onSelect([]);
      setDrag({ kind: 'marquee', x0: wx, y0: wy, x1: wx, y1: wy, additive: ev.shiftKey });
      (ev.target as Element).setPointerCapture?.(ev.pointerId);
      return;
    }
    if (!editable) return;
    const k = tool.kind;
    if (k === 'point' || k === 'waypoint' || k === 'label') {
      commitNew(newObject(k, { x: sx, y: sy }));
    } else if (k === 'node') {
      const w = tool.defaults?.w ?? (world.unit === 'mm' ? grid * 4 : 140);
      const h = tool.defaults?.h ?? (world.unit === 'mm' ? grid * 2 : 56);
      commitNew(newObject('node', { x: sx, y: sy, w, h }));
    } else if (k === 'rect' || k === 'zone' || k === 'image') {
      setDrag({ kind: 'create-box', x0: sx, y0: sy, x1: sx, y1: sy });
      (ev.target as Element).setPointerCapture?.(ev.pointerId);
    } else if (k === 'line') {
      setDrag({ kind: 'create-line', x0: sx, y0: sy, x1: sx, y1: sy });
      (ev.target as Element).setPointerCapture?.(ev.pointerId);
    } else if (k === 'polyline' || k === 'polygon' || k === 'region') {
      setDraft((d) => [...d, [sx, sy]]);
    } else if (k === 'edge' || k === 'connector') {
      const hit = topHit(wx, wy, (o) => !isLinkKind(o.kind));
      if (!hit) return;
      if (!linkFrom) setLinkFrom(hit.id);
      else if (hit.id !== linkFrom) {
        commitNew({ ...newObject(k, { from: linkFrom, to: hit.id, directed: true }), name: `${byId.get(linkFrom)?.name ?? linkFrom} → ${hit.name || hit.id}` });
        setLinkFrom(null);
      }
    }
  };

  const onPointerMove = (ev: RPointerEvent<SVGSVGElement>) => {
    const [wx, wy] = toWorld(ev);
    setHover([wx, wy]);
    if (!drag) return;
    if (drag.kind === 'pan') {
      setCam({ s: camera.s, x: drag.ox - (ev.clientX - drag.sx) / camera.s, y: drag.oy - (ev.clientY - drag.sy) / camera.s });
    } else if (drag.kind === 'move') {
      let dx = snap(wx - drag.wx, grid, snapOn);
      let dy = snap(wy - drag.wy, grid, snapOn);
      const moving = unionBox(selection.map((id) => byId.get(id)).filter((o): o is WorldObject => !!o).map((o) => bboxOf(o, byId)));
      let guides: Guide[] = [];
      if (moving && !ev.altKey) {
        const others = world.objects.filter((o) => !selection.includes(o.id) && visible(o) && !isLinkKind(o.kind)).map((o) => bboxOf(o, byId)).filter((b): b is Box => !!b);
        const a = alignGuides({ ...moving, x: moving.x + dx, y: moving.y + dy }, others, tol);
        dx += a.dx;
        dy += a.dy;
        guides = a.guides;
      }
      setDrag({ ...drag, dx, dy, guides });
    } else if (drag.kind === 'marquee') {
      setDrag({ ...drag, x1: wx, y1: wy });
    } else if (drag.kind === 'resize' || drag.kind === 'vertex' || drag.kind === 'create-box' || drag.kind === 'create-line') {
      const sx = snap(wx, grid, snapOn);
      const sy = snap(wy, grid, snapOn);
      if (drag.kind === 'resize') setDrag({ ...drag, box: resizeBox(bboxOf(byId.get(drag.id)!)!, drag.handle, sx, sy, Math.max(1, grid / 2)) });
      else if (drag.kind === 'vertex') setDrag({ ...drag, points: drag.points.map((p, i) => (i === drag.index ? [sx, sy] : p)) });
      else setDrag({ ...drag, x1: sx, y1: sy });
    }
  };

  const onPointerUp = () => {
    if (!drag) return;
    const d = drag;
    setDrag(null);
    if (d.kind === 'move' && (d.dx !== 0 || d.dy !== 0)) {
      onCommit({ ...world, objects: world.objects.map((o) => (selection.includes(o.id) ? { ...o, geometry: moveGeometry(o, d.dx, d.dy) } : o)) }, selection.length === 1 ? `Move ${selection[0]}` : `Move ${selection.length} objects`);
    } else if (d.kind === 'marquee') {
      const x0 = Math.min(d.x0, d.x1);
      const y0 = Math.min(d.y0, d.y1);
      const x1 = Math.max(d.x0, d.x1);
      const y1 = Math.max(d.y0, d.y1);
      if (x1 - x0 < tol && y1 - y0 < tol) return;
      const inside = world.objects
        .filter((o) => visible(o) && layers.get(o.layer)?.role !== 'background')
        .filter((o) => {
          const b = bboxOf(o, byId);
          return !!b && b.x >= x0 && b.y >= y0 && b.x + b.w <= x1 && b.y + b.h <= y1;
        })
        .map((o) => o.id);
      onSelect(d.additive ? [...new Set([...selection, ...inside])] : inside);
    } else if (d.kind === 'resize') {
      const o = byId.get(d.id)!;
      const g: WorldGeometry = o.kind === 'node' ? { ...o.geometry, x: round(d.box.x + d.box.w / 2), y: round(d.box.y + d.box.h / 2), w: d.box.w, h: d.box.h } : { ...o.geometry, ...d.box };
      onCommit({ ...world, objects: world.objects.map((x) => (x.id === d.id ? { ...x, geometry: g } : x)) }, `Resize ${d.id}`);
    } else if (d.kind === 'vertex') {
      onCommit({ ...world, objects: world.objects.map((x) => (x.id === d.id ? { ...x, geometry: { ...x.geometry, points: d.points } } : x)) }, `Edit vertex of ${d.id}`);
    } else if (d.kind === 'create-box') {
      let x = Math.min(d.x0, d.x1);
      let y = Math.min(d.y0, d.y1);
      let w = Math.abs(d.x1 - d.x0);
      let h = Math.abs(d.y1 - d.y0);
      if (w < grid / 2 || h < grid / 2) {
        w = tool.defaults?.w ?? grid * 4;
        h = tool.defaults?.h ?? grid * 3;
        x = d.x0;
        y = d.y0;
      }
      commitNew(newObject(tool.kind as WorldObjectKind, { x: round(x), y: round(y), w: round(w), h: round(h) }));
    } else if (d.kind === 'create-line') {
      if (d.x0 === d.x1 && d.y0 === d.y1) return;
      commitNew(newObject('line', { points: [[d.x0, d.y0], [d.x1, d.y1]], width: tool.defaults?.width ?? 0 }));
    }
  };

  const finishDraft = useCallback(() => {
    const k = tool.kind;
    const min = k === 'polyline' ? 2 : 3;
    if (draft.length >= min) {
      const kind = (k === 'region' ? 'region' : k) as WorldObjectKind;
      commitNew(newObject(kind, kind === 'polyline' ? { points: draft, width: tool.defaults?.width ?? 0 } : { points: draft }));
    }
    setDraft([]);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [draft, tool]);

  // Cancel drafts when the tool changes.
  const [toolSeen, setToolSeen] = useState(tool.id);
  if (toolSeen !== tool.id) {
    setToolSeen(tool.id);
    setDraft([]);
    setLinkFrom(null);
  }

  const onKeyDown = (ev: React.KeyboardEvent) => {
    if (ev.key === ' ') {
      setSpace(true);
      ev.preventDefault();
      return;
    }
    if (ev.key === 'Escape') {
      setDraft([]);
      setLinkFrom(null);
      setDrag(null);
      onSelect([]);
      return;
    }
    if (ev.key === 'Enter' && draft.length > 0) {
      ev.preventDefault();
      finishDraft();
      return;
    }
    if (ev.key === 'Backspace' && draft.length > 0) {
      ev.preventDefault();
      setDraft((d) => d.slice(0, -1));
      return;
    }
    if ((ev.key === 'Delete' || ev.key === 'Backspace') && selection.length > 0 && editable) {
      ev.preventDefault();
      onDeleteRequest(selection);
      return;
    }
    const mod = ev.metaKey || ev.ctrlKey;
    if (mod && ev.key.toLowerCase() === 'd' && selection.length > 0 && editable) {
      ev.preventDefault();
      onDuplicate(selection);
      return;
    }
    if (mod && ev.key.toLowerCase() === 'a') {
      ev.preventDefault();
      onSelect(world.objects.filter((o) => visible(o) && editableObj(o)).map((o) => o.id));
      return;
    }
    if (ev.key.startsWith('Arrow') && selection.length > 0 && editable) {
      ev.preventDefault();
      const step = (ev.altKey ? 1 : grid) * (ev.shiftKey ? 10 : 1);
      const dx = ev.key === 'ArrowLeft' ? -step : ev.key === 'ArrowRight' ? step : 0;
      const dy = ev.key === 'ArrowUp' ? -step : ev.key === 'ArrowDown' ? step : 0;
      onCommit({ ...world, objects: world.objects.map((o) => (selection.includes(o.id) && editableObj(o) ? { ...o, geometry: moveGeometry(o, dx, dy) } : o)) }, 'Nudge selection');
      return;
    }
    if (ev.key === 'f' && !mod) fit();
    if ((ev.key === '+' || ev.key === '=') && !mod) zoomAt(1.25, size.w / 2, size.h / 2);
    if (ev.key === '-' && !mod) zoomAt(0.8, size.w / 2, size.h / 2);
  };

  // ------------------------------------------------------------------ rendering
  const s = camera.s;
  const px = (n: number) => n / s; // screen pixels -> world units
  const preview = (o: WorldObject): WorldObject => {
    if (!drag) return o;
    if (drag.kind === 'move' && selection.includes(o.id)) return { ...o, geometry: moveGeometry(o, drag.dx, drag.dy) };
    if (drag.kind === 'resize' && drag.id === o.id) return { ...o, geometry: o.kind === 'node' ? { ...o.geometry, x: drag.box.x + drag.box.w / 2, y: drag.box.y + drag.box.h / 2, w: drag.box.w, h: drag.box.h } : { ...o.geometry, ...drag.box } };
    if (drag.kind === 'vertex' && drag.id === o.id) return { ...o, geometry: { ...o.geometry, points: drag.points } };
    return o;
  };
  const shown = world.objects.filter(visible).map(preview);
  const shownById = new Map(shown.map((o) => [o.id, o]));
  const orderedLayers = world.layers.map((l) => l.id);
  const sorted = [...shown].sort((a, b) => orderedLayers.indexOf(a.layer) - orderedLayers.indexOf(b.layer));

  const renderObject = (o: WorldObject) => {
    const layer = layers.get(o.layer);
    const st = styleOf(o, layer);
    const g = o.geometry;
    const sel = selection.includes(o.id);
    const bad = findings.get(o.id);
    const common = {
      vectorEffect: 'non-scaling-stroke' as const,
      stroke: sel ? 'var(--accent)' : bad === 'error' ? 'var(--crit)' : st.stroke,
      strokeWidth: sel ? 2.5 : bad ? 2 : 1.2,
      strokeDasharray: bad && !sel ? '5 3' : st.dash,
      opacity: st.opacity,
    };
    const label = (cx: number, cy: number, w: number) =>
      o.name && w * s > 60 ? (
        <text x={cx} y={cy} fontSize={px(11)} textAnchor="middle" dominantBaseline="middle" fill="var(--text-muted)" style={{ pointerEvents: 'none' }}>
          {o.name}
        </text>
      ) : null;
    if (o.kind === 'image') {
      const src = String(o.properties.src ?? '');
      return (
        <g key={o.id} opacity={0.75}>
          {src && <image href={src} x={g.x} y={g.y} width={g.w} height={g.h} preserveAspectRatio="none" />}
          <rect x={g.x} y={g.y} width={g.w} height={g.h} fill="none" {...common} strokeDasharray="6 4" />
        </g>
      );
    }
    if (isLinkKind(o.kind)) {
      const a = shownById.get(g.from ?? '') ?? byId.get(g.from ?? '');
      const b = shownById.get(g.to ?? '') ?? byId.get(g.to ?? '');
      const ca = a && centerOf(a);
      const cb = b && centerOf(b);
      if (!ca || !cb) return null;
      const ang = Math.atan2(cb[1] - ca[1], cb[0] - ca[0]);
      const bb = b && bboxOf(b);
      // stop at the target's box edge (approximately)
      const inset = bb ? Math.min(bb.w, bb.h) / 2 : 0;
      const ex = cb[0] - Math.cos(ang) * inset;
      const ey = cb[1] - Math.sin(ang) * inset;
      const ah = px(9);
      return (
        <g key={o.id}>
          <line x1={ca[0]} y1={ca[1]} x2={ex} y2={ey} {...common} stroke={sel ? 'var(--accent)' : 'var(--text-muted)'} strokeWidth={sel ? 2.5 : 1.6} />
          {g.directed !== false && (
            <polygon
              points={`${ex},${ey} ${ex - ah * Math.cos(ang - 0.4)},${ey - ah * Math.sin(ang - 0.4)} ${ex - ah * Math.cos(ang + 0.4)},${ey - ah * Math.sin(ang + 0.4)}`}
              fill={sel ? 'var(--accent)' : 'var(--text-muted)'}
            />
          )}
          {o.name && s > 0 && (
            <text x={(ca[0] + ex) / 2} y={(ca[1] + ey) / 2 - px(6)} fontSize={px(10)} textAnchor="middle" fill="var(--text-subtle)" style={{ pointerEvents: 'none' }}>
              {o.semanticType !== 'connection' ? o.semanticType : ''}
            </text>
          )}
        </g>
      );
    }
    if (isPointKind(o.kind)) {
      if (g.x === undefined || g.y === undefined) return null;
      if (o.kind === 'label') {
        return (
          <text key={o.id} x={g.x} y={g.y} fontSize={px(13)} fill={sel ? 'var(--accent-text)' : 'var(--text)'} fontWeight={600} style={{ cursor: 'move' }}>
            {String(o.properties.text ?? o.name)}
          </text>
        );
      }
      const colour = POINT_COLOUR[o.semanticType] ?? (o.asset ? 'var(--accent)' : 'var(--text-muted)');
      return (
        <g key={o.id}>
          {o.kind === 'waypoint' ? (
            <rect x={g.x - px(5)} y={g.y - px(5)} width={px(10)} height={px(10)} transform={`rotate(45 ${g.x} ${g.y})`} fill={colour} {...common} stroke={sel ? 'var(--accent)' : '#fff'} />
          ) : (
            <circle cx={g.x} cy={g.y} r={px(6)} fill={colour} {...common} stroke={sel ? 'var(--accent)' : '#fff'} strokeWidth={sel ? 3 : 1.5} />
          )}
          {s * 1 > 0 && (
            <text x={g.x + px(9)} y={g.y + px(4)} fontSize={px(11)} fill="var(--text)" style={{ pointerEvents: 'none' }}>
              {o.name}
            </text>
          )}
        </g>
      );
    }
    if (o.kind === 'line' || o.kind === 'polyline') {
      const ps = pointsOf(g);
      const width = g.width ?? 0;
      return (
        <g key={o.id}>
          {width > 0 && <polyline points={ps.map((p) => p.join(',')).join(' ')} fill="none" stroke={st.fill === 'none' ? st.stroke : st.fill} strokeWidth={width} strokeLinecap="square" opacity={st.opacity} />}
          <polyline points={ps.map((p) => p.join(',')).join(' ')} fill="none" {...common} />
        </g>
      );
    }
    if (o.kind === 'node') {
      const b = bboxOf(o);
      if (!b) return null;
      return (
        <g key={o.id}>
          <rect x={b.x} y={b.y} width={Math.max(b.w, px(16))} height={Math.max(b.h, px(16))} rx={px(8)} fill={st.fill} {...common} />
          {label(b.x + b.w / 2, b.y + b.h / 2, b.w || 80)}
        </g>
      );
    }
    const ps = pointsOf(g);
    if (ps.length >= 3) {
      const b = bboxOf(o)!;
      return (
        <g key={o.id}>
          <polygon points={ps.map((p) => p.join(',')).join(' ')} fill={st.fill} {...common} />
          {label(b.x + b.w / 2, b.y + b.h / 2, b.w)}
        </g>
      );
    }
    if (g.w === undefined || g.h === undefined) return null;
    return (
      <g key={o.id}>
        <rect x={g.x} y={g.y} width={g.w} height={g.h} fill={st.fill} {...common} />
        {(isAreaKind(o.kind) || o.semanticType === 'chamber' || o.semanticType === 'room') && label((g.x ?? 0) + g.w / 2, (g.y ?? 0) + px(14), g.w)}
      </g>
    );
  };

  const selectionBoxes = selection.map((id) => shownById.get(id)).filter((o): o is WorldObject => !!o);
  const single = selectionBoxes.length === 1 ? selectionBoxes[0]! : null;
  const gridPx = grid * s;
  const b = world.bounds;
  const cursor = drag?.kind === 'pan' || tool.kind === 'pan' || space ? 'grabbing' : tool.kind === 'select' ? 'default' : 'crosshair';

  return (
    <div ref={host} className="vts-canvas-wrap" style={{ height: '100%' }}>
      <svg
        className="vts-canvas"
        role="application"
        aria-label={ariaLabel}
        aria-roledescription="world canvas"
        tabIndex={0}
        style={{ cursor }}
        onPointerDown={onPointerDown}
        onPointerMove={onPointerMove}
        onPointerUp={onPointerUp}
        onPointerLeave={() => setHover(null)}
        onDoubleClick={() => draft.length > 0 && finishDraft()}
        onWheel={(ev) => {
          const r = host.current!.getBoundingClientRect();
          zoomAt(ev.deltaY < 0 ? 1.15 : 1 / 1.15, ev.clientX - r.left, ev.clientY - r.top);
        }}
        onKeyDown={onKeyDown}
        onKeyUp={(ev) => ev.key === ' ' && setSpace(false)}
      >
        <defs>
          <pattern id="vts-grid" width={grid} height={grid} patternUnits="userSpaceOnUse">
            <path d={`M ${grid} 0 L 0 0 0 ${grid}`} fill="none" stroke="var(--divider)" strokeWidth={px(1)} />
          </pattern>
        </defs>
        <g transform={`scale(${s}) translate(${-camera.x} ${-camera.y})`}>
          <rect x={b.x} y={b.y} width={b.w} height={b.h} fill="var(--surface)" stroke="var(--border-strong)" strokeWidth={px(1)} />
          {gridPx >= 6 && <rect x={b.x} y={b.y} width={b.w} height={b.h} fill="url(#vts-grid)" style={{ pointerEvents: 'none' }} />}
          {overlay}
          {sorted.map(renderObject)}
          {/* selection handles */}
          {single && editable && editableObj(single) && (isRectLike(single) || (single.kind === 'node' && single.geometry.w)) && (() => {
            const bb = bboxOf(single)!;
            return [[bb.x, bb.y], [bb.x + bb.w, bb.y], [bb.x, bb.y + bb.h], [bb.x + bb.w, bb.y + bb.h]].map(([hx, hy], i) => (
              <rect key={i} x={hx! - px(4)} y={hy! - px(4)} width={px(8)} height={px(8)} fill="var(--surface)" stroke="var(--accent)" strokeWidth={px(1.5)} />
            ));
          })()}
          {single && editable && editableObj(single) && !isPointKind(single.kind) &&
            pointsOf(single.geometry).map(([hx, hy], i) => <circle key={`v${i}`} cx={hx} cy={hy} r={px(4.5)} fill="var(--surface)" stroke="var(--accent)" strokeWidth={px(1.5)} />)}
          {selectionBoxes.length > 1 &&
            selectionBoxes.map((o) => {
              const bb = bboxOf(o, shownById);
              return bb ? <rect key={`sb-${o.id}`} x={bb.x - px(3)} y={bb.y - px(3)} width={bb.w + px(6)} height={bb.h + px(6)} fill="none" stroke="var(--accent)" strokeWidth={px(1)} strokeDasharray={`${px(4)} ${px(3)}`} /> : null;
            })}
          {/* alignment guides */}
          {drag?.kind === 'move' &&
            drag.guides.map((gd, i) =>
              gd.axis === 'x' ? (
                <line key={i} x1={gd.at} x2={gd.at} y1={b.y - b.h} y2={b.y + 2 * b.h} stroke="var(--crit)" strokeWidth={px(1)} strokeDasharray={`${px(4)} ${px(3)}`} />
              ) : (
                <line key={i} y1={gd.at} y2={gd.at} x1={b.x - b.w} x2={b.x + 2 * b.w} stroke="var(--crit)" strokeWidth={px(1)} strokeDasharray={`${px(4)} ${px(3)}`} />
              ),
            )}
          {/* marquee and creation previews */}
          {drag?.kind === 'marquee' && (
            <rect x={Math.min(drag.x0, drag.x1)} y={Math.min(drag.y0, drag.y1)} width={Math.abs(drag.x1 - drag.x0)} height={Math.abs(drag.y1 - drag.y0)} fill="rgba(29,79,145,0.08)" stroke="var(--accent)" strokeWidth={px(1)} strokeDasharray={`${px(4)} ${px(3)}`} />
          )}
          {drag?.kind === 'create-box' && (
            <rect x={Math.min(drag.x0, drag.x1)} y={Math.min(drag.y0, drag.y1)} width={Math.abs(drag.x1 - drag.x0)} height={Math.abs(drag.y1 - drag.y0)} fill="rgba(29,79,145,0.12)" stroke="var(--accent)" strokeWidth={px(1.5)} />
          )}
          {drag?.kind === 'create-line' && <line x1={drag.x0} y1={drag.y0} x2={drag.x1} y2={drag.y1} stroke="var(--accent)" strokeWidth={px(2)} />}
          {draft.length > 0 && (
            <polyline
              points={[...draft, ...(hover ? [[snap(hover[0], grid, snapOn), snap(hover[1], grid, snapOn)]] : [])].map((p) => p.join(',')).join(' ')}
              fill={tool.kind === 'polyline' ? 'none' : 'rgba(29,79,145,0.08)'}
              stroke="var(--accent)"
              strokeWidth={px(1.5)}
              strokeDasharray={`${px(5)} ${px(3)}`}
            />
          )}
          {linkFrom && byId.get(linkFrom) && hover && (() => {
            const c = centerOf(byId.get(linkFrom)!);
            return c ? <line x1={c[0]} y1={c[1]} x2={hover[0]} y2={hover[1]} stroke="var(--accent)" strokeWidth={px(1.5)} strokeDasharray={`${px(5)} ${px(3)}`} /> : null;
          })()}
        </g>
      </svg>
      <div className="vts-canvas-hud" aria-live="polite">
        {hover ? <span className="num">x {Math.round(hover[0])} · y {Math.round(hover[1])} {world.unit}</span> : <span>{world.mode}</span>}
        <span>· zoom {s >= 1 ? s.toFixed(1) : s.toPrecision(2)}×</span>
        {draft.length > 0 && <span>· {draft.length} point(s) — double-click or Enter to finish, Backspace removes, Esc cancels</span>}
        {linkFrom && <span>· from {linkFrom}: click the target</span>}
      </div>
    </div>
  );
}
