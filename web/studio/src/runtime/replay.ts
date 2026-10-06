/**
 * Read-only accessors over recomputed replay frames (shape: docs/runtime-api.md,
 * POST /runtime/replay). They only pick recorded fields; nothing is derived semantically.
 */
import type { RecordedBranch, RecordedConfiguration, ReplayFrameRecord } from './types';

/** Logical time after the record, in ticks. */
export function frameTicks(f: ReplayFrameRecord | undefined): number {
  if (!f) return 0;
  if (typeof f.fields.time_after === 'number') return f.fields.time_after;
  const s = f.fields.state_after?.[0];
  return s && typeof s.time === 'number' ? s.time : 0;
}

/** The transition the kernel took in a step record (the first recorded branch). */
export function frameTransition(f: ReplayFrameRecord | undefined): RecordedBranch | null {
  return f?.fields.outcome?.branches?.[0] ?? null;
}

/** Human label of the record: the input name for steps, otherwise the record's own descriptor. */
export function frameLabel(f: ReplayFrameRecord): string {
  const x = f.fields;
  if (x.input?.name) return x.input.name;
  if (f.kind === 'context' && x.topic) return `context: ${x.topic}`;
  if (f.kind === 'alarm' && x.alarm) return `alarm: ${x.alarm}`;
  if (f.kind === 'end' && x.reason) return `end: ${x.reason}`;
  return f.kind;
}

/**
 * The recorded configurations in effect at frame `index`. Context records carry no state, so
 * the state is that of the nearest earlier record that has one.
 */
export function stateAt(frames: ReplayFrameRecord[], index: number): RecordedConfiguration[] | null {
  for (let i = Math.min(index, frames.length - 1); i >= 0; i--) {
    const s = frames[i]?.fields.state_after;
    if (s && s.length) return s;
  }
  return null;
}

/** Propositions recorded at frame `index` (nearest earlier record that lists them). */
export function propositionsAt(frames: ReplayFrameRecord[], index: number): string[] | null {
  for (let i = Math.min(index, frames.length - 1); i >= 0; i--) {
    const p = frames[i]?.fields.propositions;
    if (p) return p;
  }
  return null;
}
