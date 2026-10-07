/**
 * Presentation of Blueprint states. Every state is computed by the backend (section
 * validation, evidence for the exact pinned inputs); these tables only map a state to
 * icon + label + tone, with precise terms: VALID (structurally valid), VERIFIED (formal
 * evidence passes), PUBLISHED / DRAFT (lifecycle), DEPLOYED and CONFORMANT (instances).
 */
import {
  AlertOctagon,
  AlertTriangle,
  CheckCircle2,
  CircleDashed,
  CircleSlash,
  CloudOff,
  Loader2,
  Lock,
  MinusCircle,
  PencilLine,
  RefreshCcwDot,
  ShieldCheck,
  XCircle,
  type LucideIcon,
} from 'lucide-react';
import type { BlueprintVersionState, GateState, SectionState, Tone } from '@/api/types';
import { StatusBadge } from '@/design';
import type { SaveState } from './editor';

interface P {
  tone: Tone;
  icon: LucideIcon;
  label: string;
  spin?: boolean;
}

const SECTION: Record<SectionState, P> = {
  complete: { tone: 'ok', icon: CheckCircle2, label: 'Valid' },
  warnings: { tone: 'warning', icon: AlertTriangle, label: 'Warnings' },
  errors: { tone: 'critical', icon: AlertOctagon, label: 'Errors' },
  empty: { tone: 'neutral', icon: CircleDashed, label: 'Not started' },
};

export function sectionPresentation(s: SectionState | undefined): P {
  return (s && SECTION[s]) || SECTION.empty;
}

export function SectionStateBadge({ state, label }: { state: SectionState; label?: string }) {
  const p = sectionPresentation(state);
  return <StatusBadge tone={p.tone} icon={p.icon} label={label ?? p.label} />;
}

/** Small coloured dot used in the sidebar (with an accessible label). */
export function SectionDot({ state }: { state: SectionState | undefined }) {
  const p = sectionPresentation(state);
  const Icon = p.icon;
  const colour = p.tone === 'ok' ? 'var(--ok)' : p.tone === 'warning' ? 'var(--warn)' : p.tone === 'critical' ? 'var(--crit)' : 'var(--text-subtle)';
  return (
    <span className="vts-bp-dot" title={p.label} style={{ color: colour }}>
      <Icon size={13} aria-hidden="true" />
      <span className="sr-only">{p.label}</span>
    </span>
  );
}

const GATE: Record<GateState, P> = {
  pass: { tone: 'ok', icon: CheckCircle2, label: 'Pass' },
  fail: { tone: 'critical', icon: XCircle, label: 'Fail' },
  not_run: { tone: 'neutral', icon: CircleDashed, label: 'Not run' },
  blocked: { tone: 'neutral', icon: CircleSlash, label: 'Blocked' },
  error: { tone: 'critical', icon: AlertOctagon, label: 'Check error' },
  not_applicable: { tone: 'neutral', icon: MinusCircle, label: 'Not applicable' },
};

export function gatePresentation(s: GateState): P {
  return GATE[s] ?? GATE.not_run;
}

export function GateBadge({ state, label }: { state: GateState; label?: string }) {
  const p = gatePresentation(state);
  return <StatusBadge tone={p.tone} icon={p.icon} label={label ?? p.label} />;
}

export function VersionStateBadge({ state, size }: { state: BlueprintVersionState; size?: 'md' | 'lg' }) {
  if (state === 'published') return <StatusBadge tone="ok" icon={Lock} label="PUBLISHED" size={size} square />;
  if (state === 'deprecated') return <StatusBadge tone="neutral" icon={MinusCircle} label="DEPRECATED" size={size} square />;
  return <StatusBadge tone="info" icon={PencilLine} label="DRAFT" size={size} square />;
}

export function ReleaseVerdictBadge({ verdict, size }: { verdict: 'ready' | 'blocked' | undefined; size?: 'md' | 'lg' }) {
  if (verdict === 'ready') return <StatusBadge tone="formal" icon={ShieldCheck} label="READY TO RELEASE" size={size} />;
  if (verdict === 'blocked') return <StatusBadge tone="warning" icon={CircleSlash} label="RELEASE BLOCKED" size={size} />;
  return <StatusBadge tone="neutral" icon={CircleDashed} label="Release state…" size={size} />;
}

const SAVE: Record<SaveState, P> = {
  saved: { tone: 'ok', icon: CheckCircle2, label: 'All changes saved' },
  dirty: { tone: 'info', icon: PencilLine, label: 'Unsaved changes' },
  saving: { tone: 'info', icon: Loader2, label: 'Saving…', spin: true },
  error: { tone: 'critical', icon: CloudOff, label: 'Save failed' },
  conflict: { tone: 'critical', icon: RefreshCcwDot, label: 'Changed elsewhere' },
  readonly: { tone: 'neutral', icon: Lock, label: 'Read-only (published)' },
};

export function SaveIndicator({ state, title }: { state: SaveState; title?: string }) {
  const p = SAVE[state];
  return (
    <span aria-live="polite">
      <StatusBadge tone={p.tone} icon={p.icon} label={p.label} spin={p.spin} title={title} />
    </span>
  );
}
