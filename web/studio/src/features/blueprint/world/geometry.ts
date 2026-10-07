/**
 * Geometry of twin-world/1 objects for the editor: bounding boxes, hit testing, moving,
 * resizing, snapping and alignment guides. Coordinates are integer world units (mm in
 * spatial worlds); everything returned for the document is rounded to integers.
 */
import type { WorldDocument, WorldGeometry, WorldObject, WorldObjectKind } from '@/api/types';

export interface Box {
  x: number;
  y: number;
  w: number;
  h: number;
}

export const POINT_KINDS: WorldObjectKind[] = ['point', 'label', 'waypoint'];
export const LINK_KINDS: WorldObjectKind[] = ['edge', 'connector'];
export const AREA_KINDS: WorldObjectKind[] = ['polygon', 'region', 'zone'];

export const isPointKind = (k: WorldObjectKind) => POINT_KINDS.includes(k);
export const isLinkKind = (k: WorldObjectKind) => LINK_KINDS.includes(k);
export const isAreaKind = (k: WorldObjectKind) => AREA_KINDS.includes(k);
export const isRectLike = (o: WorldObject) => o.kind === 'rect' || o.kind === 'image' || (isAreaKind(o.kind) && !o.geometry.points?.length && o.geometry.w !== undefined);

export const round = (n: number) => Math.round(n);

/** Points of a geometry ([x, y] pairs), whichever encoding it uses. */
export function pointsOf(g: WorldGeometry): [number, number][] {
  return (g.points ?? []).map((p) => (Array.isArray(p) ? [Number(p[0]), Number(p[1])] : [Number((p as { x: number }).x), Number((p as { y: number }).y)])) as [number, number][];
}

/** Bounding box in world units (links: the box between their endpoints). */
export function bboxOf(o: WorldObject, all?: Map<string, WorldObject>): Box | null {
  const g = o.geometry;
  if (isLinkKind(o.kind)) {
    if (!all) return null;
    const a = all.get(g.from ?? '');
    const b = all.get(g.to ?? '');
    if (!a || !b) return null;
    const ca = centerOf(a);
    const cb = centerOf(b);
    if (!ca || !cb) return null;
    return { x: Math.min(ca[0], cb[0]), y: Math.min(ca[1], cb[1]), w: Math.abs(ca[0] - cb[0]), h: Math.abs(ca[1] - cb[1]) };
  }
  if (o.kind === 'node') {
    if (g.x === undefined || g.y === undefined) return null;
    const w = g.w ?? 0;
    const h = g.h ?? 0;
    return { x: g.x - w / 2, y: g.y - h / 2, w, h };
  }
  if (isPointKind(o.kind)) return g.x === undefined || g.y === undefined ? null : { x: g.x, y: g.y, w: 0, h: 0 };
  if (g.w !== undefined && g.h !== undefined && g.x !== undefined && g.y !== undefined) return { x: g.x, y: g.y, w: g.w, h: g.h };
  const ps = pointsOf(g);
  if (ps.length === 0) return null;
  const xs = ps.map((p) => p[0]);
  const ys = ps.map((p) => p[1]);
  const x = Math.min(...xs);
  const y = Math.min(...ys);
  return { x, y, w: Math.max(...xs) - x, h: Math.max(...ys) - y };
}

export function centerOf(o: WorldObject): [number, number] | null {
  const g = o.geometry;
  if (isPointKind(o.kind) || o.kind === 'node') return g.x === undefined || g.y === undefined ? null : [g.x, g.y];
  const b = bboxOf(o);
  return b ? [b.x + b.w / 2, b.y + b.h / 2] : null;
}

export function unionBox(boxes: (Box | null)[]): Box | null {
  const bs = boxes.filter((b): b is Box => !!b);
  if (bs.length === 0) return null;
  const x = Math.min(...bs.map((b) => b.x));
  const y = Math.min(...bs.map((b) => b.y));
  return { x, y, w: Math.max(...bs.map((b) => b.x + b.w)) - x, h: Math.max(...bs.map((b) => b.y + b.h)) - y };
}

function distToSegment(px: number, py: number, ax: number, ay: number, bx: number, by: number): number {
  const dx = bx - ax;
  const dy = by - ay;
  const len = dx * dx + dy * dy;
  const t = len === 0 ? 0 : Math.max(0, Math.min(1, ((px - ax) * dx + (py - ay) * dy) / len));
  return Math.hypot(px - (ax + t * dx), py - (ay + t * dy));
}

function insidePolygon(px: number, py: number, ps: [number, number][]): boolean {
  let inside = false;
  for (let i = 0, j = ps.length - 1; i < ps.length; j = i++) {
    const [xi, yi] = ps[i]!;
    const [xj, yj] = ps[j]!;
    if (yi > py !== yj > py && px < ((xj - xi) * (py - yi)) / (yj - yi) + xi) inside = !inside;
  }
  return inside;
}

/** Does the world point (px, py) hit the object? @p tol is the tolerance in world units. */
export function hits(o: WorldObject, px: number, py: number, tol: number, all: Map<string, WorldObject>): boolean {
  const g = o.geometry;
  if (isLinkKind(o.kind)) {
    const a = all.get(g.from ?? '');
    const b = all.get(g.to ?? '');
    const ca = a && centerOf(a);
    const cb = b && centerOf(b);
    return !!ca && !!cb && distToSegment(px, py, ca[0], ca[1], cb[0], cb[1]) <= tol;
  }
  if (isPointKind(o.kind)) return g.x !== undefined && g.y !== undefined && Math.hypot(px - g.x, py - g.y) <= tol * 1.5;
  if (o.kind === 'line' || o.kind === 'polyline') {
    const ps = pointsOf(g);
    const half = Math.max(tol, (g.width ?? 0) / 2);
    for (let i = 1; i < ps.length; i++) if (distToSegment(px, py, ps[i - 1]![0], ps[i - 1]![1], ps[i]![0], ps[i]![1]) <= half) return true;
    return false;
  }
  const ps = pointsOf(g);
  if (ps.length >= 3) return insidePolygon(px, py, ps);
  const b = bboxOf(o);
  return !!b && px >= b.x - tol && px <= b.x + b.w + tol && py >= b.y - tol && py <= b.y + b.h + tol;
}

/** Geometry moved by (dx, dy) world units. */
export function moveGeometry(o: WorldObject, dx: number, dy: number): WorldGeometry {
  const g = o.geometry;
  if (isLinkKind(o.kind)) return g;
  const out: WorldGeometry = { ...g };
  if (g.x !== undefined) out.x = round(g.x + dx);
  if (g.y !== undefined) out.y = round(g.y + dy);
  if (g.points) out.points = pointsOf(g).map(([x, y]) => [round(x + dx), round(y + dy)]);
  return out;
}

/** Rect-like geometry with one corner handle dragged to (px, py). */
export function resizeBox(b: Box, handle: 'nw' | 'ne' | 'sw' | 'se', px: number, py: number, minSize: number): Box {
  let x1 = b.x;
  let y1 = b.y;
  let x2 = b.x + b.w;
  let y2 = b.y + b.h;
  if (handle.includes('w')) x1 = Math.min(px, x2 - minSize);
  if (handle.includes('e')) x2 = Math.max(px, x1 + minSize);
  if (handle.includes('n')) y1 = Math.min(py, y2 - minSize);
  if (handle.includes('s')) y2 = Math.max(py, y1 + minSize);
  return { x: round(x1), y: round(y1), w: round(x2 - x1), h: round(y2 - y1) };
}

export function snap(v: number, grid: number, on: boolean): number {
  return on && grid > 0 ? Math.round(v / grid) * grid : Math.round(v);
}

export interface Guide {
  axis: 'x' | 'y';
  at: number;
}

/**
 * Alignment guides: snap a moving box's edges/centre to other boxes' edges/centres within
 * @p tol world units. Returns the correction and the guides to draw.
 */
export function alignGuides(moving: Box, others: Box[], tol: number): { dx: number; dy: number; guides: Guide[] } {
  const xs = [moving.x, moving.x + moving.w / 2, moving.x + moving.w];
  const ys = [moving.y, moving.y + moving.h / 2, moving.y + moving.h];
  let best: { dx: number; gx: number } | null = null;
  let bestY: { dy: number; gy: number } | null = null;
  for (const o of others) {
    const ox = [o.x, o.x + o.w / 2, o.x + o.w];
    const oy = [o.y, o.y + o.h / 2, o.y + o.h];
    for (const a of xs) for (const b of ox) if (Math.abs(b - a) <= tol && (!best || Math.abs(b - a) < Math.abs(best.dx))) best = { dx: b - a, gx: b };
    for (const a of ys) for (const b of oy) if (Math.abs(b - a) <= tol && (!bestY || Math.abs(b - a) < Math.abs(bestY.dy))) bestY = { dy: b - a, gy: b };
  }
  const guides: Guide[] = [];
  if (best) guides.push({ axis: 'x', at: best.gx });
  if (bestY) guides.push({ axis: 'y', at: bestY.gy });
  return { dx: best?.dx ?? 0, dy: bestY?.dy ?? 0, guides };
}

/** Layers by role for the "ground truth" and "twin knowledge" views. */
export function layerVisibleInView(role: string, view: 'all' | 'truth' | 'knowledge'): boolean {
  if (view === 'all') return true;
  if (view === 'truth') return role === 'shared' || role === 'ground-truth' || role === 'background';
  return role === 'shared' || role === 'knowledge' || role === 'background';
}

/** A fresh, empty world of the given mode. */
export function emptyWorld(mode: WorldDocument['mode']): WorldDocument {
  return mode === 'spatial'
    ? { format: 'twin-world/1', mode, unit: 'mm', bounds: { x: 0, y: 0, w: 20000, h: 12000 }, grid: { size: 500, snap: true }, layers: [{ id: 'layout', name: 'Layout', role: 'shared', visible: true, locked: false }], objects: [] }
    : { format: 'twin-world/1', mode, unit: 'px', bounds: { x: 0, y: 0, w: 1200, h: 700 }, grid: { size: 20, snap: true }, layers: [{ id: 'diagram', name: mode === 'topology' ? 'Topology' : 'Diagram', role: 'shared', visible: true, locked: false }], objects: [] };
}
