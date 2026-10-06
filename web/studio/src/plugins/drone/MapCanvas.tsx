/**
 * Canvas rendering of one occupancy grid with mission overlays.
 *
 * Colours come from the design tokens (light and dark themes); every map
 * feature also has a shape or pattern (hatching for unknown space and no-fly
 * zones, dashes for invalidated routes), so colour is never the only carrier.
 */
import { useEffect, useRef, useState } from 'react';
import type { Grid, MissionTargetJson, PlanJson, Point } from './model';
import { cellAt, planPoints } from './model';

export interface CandidatePath {
  label: string;
  points: Point[];
  selected: boolean;
  highlighted: boolean;
}

export interface MapOverlay {
  /** Drone position (mm) and heading (centidegrees), if known. */
  drone?: { x: number; y: number; headingCdeg: number } | null;
  /** Executed trajectory (mm). */
  trajectory?: Point[];
  activePlan?: PlanJson | null;
  endedPlans?: PlanJson[];
  candidates?: CandidatePath[];
  targets?: MissionTargetJson[];
  home?: [number, number] | null;
  /** Cell indices to emphasise (recently discovered / changed). */
  recent?: Set<number>;
  /** Cell indices where this view differs from another (observer-only comparison). */
  differs?: Set<number>;
}

interface Palette {
  free: string;
  unknown: string;
  unknownHatch: string;
  wall: string;
  obstacle: string;
  doorOpen: string;
  doorClosed: string;
  hazard: string;
  grid: string;
  trajectory: string;
  plan: string;
  ended: string;
  invalid: string;
  candidate: string[];
  drone: string;
  target: string;
  inspected: string;
  home: string;
  recent: string;
  differs: string;
  text: string;
}

function readPalette(el: HTMLElement): Palette {
  const css = getComputedStyle(el);
  const v = (name: string, fallback: string) => css.getPropertyValue(name).trim() || fallback;
  return {
    free: v('--surface', '#fff'),
    unknown: v('--bg-sunken', '#eceff3'),
    unknownHatch: v('--border-strong', '#b9c1cc'),
    wall: v('--text', '#16202b'),
    obstacle: v('--series-6', '#5f6b78'),
    doorOpen: v('--ok', '#1a7146'),
    doorClosed: v('--crit', '#b0261b'),
    hazard: v('--crit', '#b0261b'),
    grid: v('--divider', '#e7eaee'),
    trajectory: v('--series-3', '#2f8f6b'),
    plan: v('--accent', '#1d4f91'),
    ended: v('--text-subtle', '#66717e'),
    invalid: v('--crit', '#b0261b'),
    candidate: [v('--series-4', '#7b4fbf'), v('--series-2', '#c35a00'), v('--series-5', '#b03a6e')],
    drone: v('--accent', '#1d4f91'),
    target: v('--warn', '#8a5300'),
    inspected: v('--ok', '#1a7146'),
    home: v('--formal', '#5a3d99'),
    recent: v('--warn', '#8a5300'),
    differs: v('--series-5', '#b03a6e'),
    text: v('--text', '#16202b'),
  };
}

function polyline(ctx: CanvasRenderingContext2D, pts: Point[], toPx: (p: Point) => [number, number]) {
  if (pts.length < 2) return;
  ctx.beginPath();
  pts.forEach((p, i) => {
    const [x, y] = toPx(p);
    if (i === 0) ctx.moveTo(x, y);
    else ctx.lineTo(x, y);
  });
  ctx.stroke();
}

function draw(canvas: HTMLCanvasElement, grid: Grid, o: MapOverlay, cs: number, pal: Palette) {
  const dpr = window.devicePixelRatio || 1;
  const w = grid.width * cs;
  const h = grid.height * cs;
  canvas.width = Math.round(w * dpr);
  canvas.height = Math.round(h * dpr);
  canvas.style.width = `${w}px`;
  canvas.style.height = `${h}px`;
  const ctx = canvas.getContext('2d');
  if (!ctx) return;
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.clearRect(0, 0, w, h);
  const scale = cs / grid.cellSizeMm;
  const toPx = (p: Point): [number, number] => [p.x * scale, p.y * scale];

  // Cells.
  for (let y = 0; y < grid.height; y++) {
    for (let x = 0; x < grid.width; x++) {
      const c = cellAt(grid, x, y);
      const px = x * cs;
      const py = y * cs;
      ctx.fillStyle = pal.free;
      if (c === '#') ctx.fillStyle = pal.wall;
      else if (c === 'o') ctx.fillStyle = pal.obstacle;
      else if (c === '?') ctx.fillStyle = pal.unknown;
      ctx.fillRect(px, py, cs, cs);
      if (c === '?') {
        ctx.strokeStyle = pal.unknownHatch;
        ctx.lineWidth = 1;
        ctx.beginPath();
        ctx.moveTo(px, py + cs);
        ctx.lineTo(px + cs, py);
        ctx.stroke();
      } else if (c === 'D' || c === 'd') {
        ctx.fillStyle = c === 'D' ? pal.doorOpen : pal.doorClosed;
        ctx.globalAlpha = c === 'D' ? 0.35 : 0.9;
        ctx.fillRect(px + 1, py + 1, cs - 2, cs - 2);
        ctx.globalAlpha = 1;
      } else if (c === '!') {
        ctx.fillStyle = pal.hazard;
        ctx.globalAlpha = 0.18;
        ctx.fillRect(px, py, cs, cs);
        ctx.globalAlpha = 0.8;
        ctx.strokeStyle = pal.hazard;
        ctx.lineWidth = 1;
        ctx.beginPath();
        ctx.moveTo(px, py);
        ctx.lineTo(px + cs, py + cs);
        ctx.moveTo(px + cs / 2, py);
        ctx.lineTo(px + cs, py + cs / 2);
        ctx.moveTo(px, py + cs / 2);
        ctx.lineTo(px + cs / 2, py + cs);
        ctx.stroke();
        ctx.globalAlpha = 1;
      }
    }
  }
  // Light grid.
  ctx.strokeStyle = pal.grid;
  ctx.lineWidth = 0.5;
  ctx.globalAlpha = 0.6;
  for (let x = 0; x <= grid.width; x++) {
    ctx.beginPath();
    ctx.moveTo(x * cs, 0);
    ctx.lineTo(x * cs, h);
    ctx.stroke();
  }
  for (let y = 0; y <= grid.height; y++) {
    ctx.beginPath();
    ctx.moveTo(0, y * cs);
    ctx.lineTo(w, y * cs);
    ctx.stroke();
  }
  ctx.globalAlpha = 1;

  // Observer comparison and recent discoveries.
  const outline = (set: Set<number> | undefined, color: string, width: number) => {
    if (!set || set.size === 0) return;
    ctx.strokeStyle = color;
    ctx.lineWidth = width;
    for (const i of set) {
      const x = i % grid.width;
      const y = Math.floor(i / grid.width);
      ctx.strokeRect(x * cs + 1, y * cs + 1, cs - 2, cs - 2);
    }
  };
  outline(o.differs, pal.differs, 1.5);
  outline(o.recent, pal.recent, 2);

  // Routes: ended (dashed), candidates (thin dashed), active (solid), trajectory.
  ctx.lineJoin = 'round';
  ctx.lineCap = 'round';
  for (const p of o.endedPlans ?? []) {
    ctx.setLineDash([cs * 0.6, cs * 0.4]);
    ctx.strokeStyle = p.status === 'invalidated' ? pal.invalid : pal.ended;
    ctx.lineWidth = Math.max(1.5, cs * 0.12);
    ctx.globalAlpha = p.status === 'invalidated' ? 0.9 : 0.5;
    polyline(ctx, planPoints(p), toPx);
  }
  ctx.globalAlpha = 1;
  (o.candidates ?? []).forEach((c, i) => {
    const color = pal.candidate[i % pal.candidate.length] ?? pal.plan;
    ctx.setLineDash([cs * 0.3, cs * 0.3]);
    ctx.strokeStyle = color;
    ctx.lineWidth = c.highlighted ? Math.max(2.5, cs * 0.22) : Math.max(1.2, cs * 0.1);
    ctx.globalAlpha = c.highlighted || c.selected ? 1 : 0.75;
    polyline(ctx, c.points, toPx);
    const last = c.points[c.points.length - 1];
    if (last) {
      const [x, y] = toPx(last);
      ctx.setLineDash([]);
      ctx.fillStyle = color;
      ctx.font = `600 ${Math.max(9, cs * 0.8)}px var(--font-sans, sans-serif)`;
      ctx.fillText(c.label, x + cs * 0.35, y - cs * 0.35);
    }
  });
  ctx.globalAlpha = 1;
  ctx.setLineDash([]);
  if (o.trajectory && o.trajectory.length > 1) {
    ctx.strokeStyle = pal.trajectory;
    ctx.lineWidth = Math.max(1.5, cs * 0.14);
    polyline(ctx, o.trajectory, toPx);
  }
  if (o.activePlan) {
    const pts = planPoints(o.activePlan);
    ctx.strokeStyle = pal.plan;
    ctx.lineWidth = Math.max(2, cs * 0.2);
    polyline(ctx, pts, toPx);
    ctx.fillStyle = pal.plan;
    for (const p of pts.slice(1)) {
      const [x, y] = toPx(p);
      ctx.beginPath();
      ctx.arc(x, y, Math.max(2, cs * 0.18), 0, Math.PI * 2);
      ctx.fill();
    }
  }

  // Home pad and targets.
  const marker = (cell: [number, number], color: string, label: string, filled: boolean) => {
    const x = (cell[0] + 0.5) * cs;
    const y = (cell[1] + 0.5) * cs;
    const r = Math.max(5, cs * 0.7);
    ctx.beginPath();
    ctx.arc(x, y, r, 0, Math.PI * 2);
    ctx.lineWidth = 2;
    ctx.strokeStyle = color;
    ctx.fillStyle = filled ? color : pal.free;
    ctx.fill();
    ctx.stroke();
    ctx.fillStyle = filled ? pal.free : color;
    ctx.font = `700 ${Math.max(8, cs * 0.7)}px var(--font-sans, sans-serif)`;
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    ctx.fillText(label, x, y + 0.5);
    ctx.textAlign = 'start';
    ctx.textBaseline = 'alphabetic';
  };
  if (o.home) marker(o.home, pal.home, 'H', false);
  for (const t of o.targets ?? []) marker(t.cell, t.status === 'inspected' ? pal.inspected : pal.target, t.id, t.status === 'inspected');

  // Drone.
  if (o.drone) {
    const [x, y] = toPx(o.drone);
    const r = Math.max(5, cs * 0.55);
    const a = (o.drone.headingCdeg / 100) * (Math.PI / 180);
    ctx.fillStyle = pal.drone;
    ctx.strokeStyle = pal.free;
    ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.moveTo(x + Math.cos(a) * r * 1.6, y + Math.sin(a) * r * 1.6);
    ctx.lineTo(x + Math.cos(a + 2.5) * r, y + Math.sin(a + 2.5) * r);
    ctx.lineTo(x + Math.cos(a - 2.5) * r, y + Math.sin(a - 2.5) * r);
    ctx.closePath();
    ctx.fill();
    ctx.stroke();
  }
}

/** Responsive canvas map (fits the container width). */
export function MapCanvas({ grid, overlay, label }: { grid: Grid | null; overlay: MapOverlay; label: string }) {
  const wrap = useRef<HTMLDivElement>(null);
  const canvas = useRef<HTMLCanvasElement>(null);
  const [width, setWidth] = useState(0);
  useEffect(() => {
    const el = wrap.current;
    if (!el) return;
    const ro = new ResizeObserver((entries) => setWidth(entries[0]?.contentRect.width ?? 0));
    ro.observe(el);
    return () => ro.disconnect();
  }, []);
  useEffect(() => {
    if (!canvas.current || !wrap.current || !grid || width <= 0) return;
    const cs = Math.max(4, Math.floor(width / grid.width));
    draw(canvas.current, grid, overlay, cs, readPalette(wrap.current));
  }, [grid, overlay, width]);
  return (
    <div ref={wrap} className="drone-map">
      {grid ? (
        <canvas ref={canvas} role="img" aria-label={label} />
      ) : (
        <div className="drone-map__empty small muted">No map available.</div>
      )}
    </div>
  );
}
