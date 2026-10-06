/**
 * A presentation model of a timed automaton for the diagram viewer, built from either
 *  - the Twin IR the kernel executes (DT view: identifiers match the runtime exactly), or
 *  - the canonical twin-ta/1 model returned by the engine's importer (PT view).
 * Only structure and texts are carried; the viewer never evaluates guards or invariants.
 */
import type { TwinIr } from '@/runtime/types';

export interface TaLocation {
  id: string;
  initial: boolean;
  invariant: string;
}

export interface TaEdge {
  id: string;
  source: string;
  target: string;
  /** Event label as written in the model ("start_cmd!", "pump_stopped?"), or "τ" for internal steps. */
  label: string;
  guard: string;
  resets: string[];
}

export interface TaGraph {
  name: string;
  clocks: string[];
  locations: TaLocation[];
  edges: TaEdge[];
}

/** Positions from the model file (UPPAAL coordinates), by location id. */
export type TaLayout = Record<string, { x: number; y: number }>;

interface Atom { clock: string; op: string; bound: number | string; minus?: string }
const atomText = (a: Atom) => `${a.clock}${a.minus ? ` - ${a.minus}` : ''} ${a.op} ${a.bound}`;
export const conjunctionText = (c: Atom[]) => (c.length ? c.map(atomText).join(' && ') : '');

export function fromIr(ir: TwinIr): TaGraph {
  return {
    name: ir.model.id,
    clocks: ir.clocks,
    locations: ir.locations.map((l) => ({ id: l.id, initial: l.id === ir.initial, invariant: conjunctionText(l.invariant as Atom[]) })),
    edges: ir.transitions.map((t) => ({
      id: t.id,
      source: t.source,
      target: t.target,
      label: t.action.kind === 'tau' || !t.action.channel ? 'τ' : `${t.action.channel}${t.action.kind === 'receive' ? '?' : '!'}`,
      guard: conjunctionText(t.guard as Atom[]),
      resets: t.resets,
    })),
  };
}

/** Canonical model (twin-ta/1) as returned by POST /authoring/import. */
export interface CanonicalModel {
  format: string;
  name: string;
  clocks: { name: string }[];
  locations: { name: string; initial: boolean; invariant: Atom[] }[];
  edges: { id: string; source: string; target: string; sync: { channel: string; direction: '!' | '?' } | null; guard: Atom[]; resets: string[] }[];
}

export interface ImportResult {
  format: string;
  model?: CanonicalModel;
  layout?: { locations?: Record<string, { x: number; y: number }> };
  diagnostics: { code: string; message: string; line?: number; column?: number; element?: string; hint?: string; severity?: string }[];
  provenance?: { preserved?: boolean };
}

export function fromCanonical(m: CanonicalModel): TaGraph {
  return {
    name: m.name,
    clocks: m.clocks.map((c) => c.name),
    locations: m.locations.map((l) => ({ id: l.name, initial: l.initial, invariant: conjunctionText(l.invariant) })),
    edges: m.edges.map((e) => ({
      id: e.id,
      source: e.source,
      target: e.target,
      label: e.sync ? `${e.sync.channel}${e.sync.direction}` : 'τ',
      guard: conjunctionText(e.guard),
      resets: e.resets,
    })),
  };
}

export function layoutFrom(r: ImportResult | undefined): TaLayout | undefined {
  const l = r?.layout?.locations;
  if (!l || Object.keys(l).length === 0) return undefined;
  return Object.fromEntries(Object.entries(l).map(([k, v]) => [k, { x: v.x, y: v.y }]));
}
