/**
 * Status presentation: every status is rendered as icon + text label + colour.
 *
 * These components only *present* states computed by the backend. The tables
 * below map a backend state to how it looks; they never decide a state.
 */
import {
  AlertOctagon,
  AlertTriangle,
  Ban,
  CheckCircle2,
  CircleDashed,
  CircleHelp,
  CircleSlash,
  Clock,
  Loader2,
  MinusCircle,
  PlugZap,
  ShieldAlert,
  ShieldCheck,
  XCircle,
  type LucideIcon,
} from 'lucide-react';
import clsx from 'clsx';
import type {
  Freshness,
  ImpactClassification,
  Lifecycle,
  Outcome,
  Tone,
  TrustState,
  Truth,
  ConditionStatus,
  RefinementVerdict,
} from '@/api/types';

export interface StatusBadgeProps {
  tone: Tone;
  icon?: LucideIcon;
  label: string;
  title?: string;
  size?: 'md' | 'lg';
  square?: boolean;
  spin?: boolean;
  className?: string;
}

/** Generic status pill: tone + icon + label (the label is always visible). */
export function StatusBadge({ tone, icon: Icon, label, title, size = 'md', square, spin, className }: StatusBadgeProps) {
  return (
    <span
      className={clsx('vts-badge', `vts-badge--${tone}`, size === 'lg' && 'vts-badge--lg', square && 'vts-badge--square', className)}
      title={title}
    >
      {Icon && <Icon size={size === 'lg' ? 15 : 13} aria-hidden="true" className={spin ? 'vts-spin' : undefined} />}
      {label}
    </span>
  );
}

interface Presentation {
  tone: Tone;
  icon: LucideIcon;
  label: string;
  spin?: boolean;
}

const TRUST: Record<TrustState, Presentation> = {
  pass: { tone: 'ok', icon: CheckCircle2, label: 'Pass' },
  fail: { tone: 'critical', icon: XCircle, label: 'Fail' },
  unknown: { tone: 'warning', icon: CircleHelp, label: 'Unknown' },
  not_checked: { tone: 'neutral', icon: CircleDashed, label: 'Not checked' },
  stale: { tone: 'warning', icon: Clock, label: 'Stale' },
  invalidated: { tone: 'critical', icon: Ban, label: 'Invalidated' },
  check_running: { tone: 'info', icon: Loader2, label: 'Check running', spin: true },
  unavailable: { tone: 'neutral', icon: PlugZap, label: 'Unavailable' },
  error: { tone: 'critical', icon: AlertOctagon, label: 'Check failed' },
  blocked: { tone: 'neutral', icon: CircleSlash, label: 'Blocked' },
  not_applicable: { tone: 'neutral', icon: MinusCircle, label: 'Not applicable' },
};

export function trustPresentation(state: TrustState): Presentation {
  return TRUST[state] ?? TRUST.unknown;
}

/** Evidence-backed trust state. Unknown/invalid input renders as "Unknown", never as "Pass". */
export function TrustBadge({ state, label, title, size }: { state: TrustState; label?: string; title?: string; size?: 'md' | 'lg' }) {
  const p = trustPresentation(state);
  return (
    <StatusBadge
      tone={p.tone}
      icon={p.icon}
      spin={p.spin}
      label={label ? `${label}: ${p.label}` : p.label}
      title={title}
      size={size}
    />
  );
}

export function OutcomeBadge({ outcome, verdict }: { outcome: Outcome; verdict?: string }) {
  const map: Record<Outcome, TrustState> = { pass: 'pass', fail: 'fail', unknown: 'unknown', error: 'error' };
  const p = trustPresentation(map[outcome]);
  return <StatusBadge tone={p.tone} icon={p.icon} label={verdict ? humanize(verdict) : p.label} />;
}

const LIFECYCLE: Record<Lifecycle, Presentation> = {
  draft: { tone: 'info', icon: CircleDashed, label: 'Draft' },
  validating: { tone: 'info', icon: Loader2, label: 'Validating', spin: true },
  verified: { tone: 'formal', icon: ShieldCheck, label: 'Verified' },
  published: { tone: 'ok', icon: CheckCircle2, label: 'Published' },
  superseded: { tone: 'neutral', icon: Clock, label: 'Superseded' },
  rejected: { tone: 'neutral', icon: Ban, label: 'Rejected' },
};

export function LifecycleBadge({ state }: { state: Lifecycle }) {
  const p = LIFECYCLE[state] ?? LIFECYCLE.draft;
  return <StatusBadge tone={p.tone} icon={p.icon} label={p.label} spin={p.spin} square />;
}

const FRESHNESS: Record<Freshness, Presentation> = {
  fresh: { tone: 'ok', icon: CheckCircle2, label: 'Fresh' },
  stale: { tone: 'warning', icon: Clock, label: 'Stale' },
  missing: { tone: 'neutral', icon: CircleDashed, label: 'No data' },
  invalid: { tone: 'critical', icon: AlertTriangle, label: 'Invalid' },
};

export function FreshnessBadge({ freshness, title }: { freshness: Freshness; title?: string }) {
  const p = FRESHNESS[freshness] ?? FRESHNESS.missing;
  return <StatusBadge tone={p.tone} icon={p.icon} label={p.label} title={title} />;
}

const TRUTH: Record<Truth, Presentation> = {
  true: { tone: 'info', icon: CheckCircle2, label: 'True' },
  false: { tone: 'neutral', icon: MinusCircle, label: 'False' },
  unknown: { tone: 'warning', icon: CircleHelp, label: 'Unknown' },
  inconsistent_observation: { tone: 'critical', icon: ShieldAlert, label: 'Inconsistent data' },
};

/** Truth of an interpretation formula under current observations (3-valued + inconsistent). */
export function TruthBadge({ truth }: { truth: Truth }) {
  const p = TRUTH[truth] ?? TRUTH.unknown;
  return <StatusBadge tone={p.tone} icon={p.icon} label={p.label} />;
}

const CONDITION: Record<ConditionStatus, Presentation> = {
  holds: { tone: 'ok', icon: CheckCircle2, label: 'Holds' },
  violated: { tone: 'critical', icon: XCircle, label: 'Violated' },
  unknown: { tone: 'warning', icon: CircleHelp, label: 'Undecided' },
  not_evaluated: { tone: 'neutral', icon: MinusCircle, label: 'Not evaluated' },
};

export function ConditionBadge({ status }: { status: ConditionStatus }) {
  const p = CONDITION[status] ?? CONDITION.unknown;
  return <StatusBadge tone={p.tone} icon={p.icon} label={p.label} />;
}

const REFINEMENT: Record<RefinementVerdict, Presentation> = {
  valid_refinement: { tone: 'ok', icon: ShieldCheck, label: 'Valid refinement' },
  not_a_refinement: { tone: 'critical', icon: XCircle, label: 'Not a refinement' },
  unknown: { tone: 'warning', icon: CircleHelp, label: 'Inconclusive' },
  check_failed: { tone: 'critical', icon: AlertOctagon, label: 'Check failed' },
};

export function RefinementBadge({ verdict, size }: { verdict: RefinementVerdict; size?: 'md' | 'lg' }) {
  const p = REFINEMENT[verdict] ?? REFINEMENT.unknown;
  return <StatusBadge tone={p.tone} icon={p.icon} label={p.label} size={size} />;
}

const IMPACT: Record<ImpactClassification, Presentation> = {
  changed: { tone: 'info', icon: CircleDashed, label: 'Changed' },
  definitely_stale: { tone: 'critical', icon: Clock, label: 'Definitely stale' },
  requires_verification: { tone: 'warning', icon: ShieldAlert, label: 'Requires verification' },
  preserved: { tone: 'formal', icon: ShieldCheck, label: 'Preserved (Thm. 3)' },
  potentially_affected: { tone: 'neutral', icon: AlertTriangle, label: 'Potentially affected' },
  unaffected: { tone: 'ok', icon: CheckCircle2, label: 'Unaffected' },
};

export function ImpactBadge({ classification }: { classification: ImpactClassification }) {
  const p = IMPACT[classification] ?? IMPACT.potentially_affected;
  return <StatusBadge tone={p.tone} icon={p.icon} label={p.label} />;
}

/** Presentation tone for an authored state tone hint (twin presentation metadata). */
export function toneOf(value: string | undefined | null): Tone {
  return value === 'ok' || value === 'warning' || value === 'critical' || value === 'info' || value === 'formal'
    ? value
    : 'neutral';
}

export const TONE_ICON: Record<Tone, LucideIcon> = {
  ok: CheckCircle2,
  warning: AlertTriangle,
  critical: AlertOctagon,
  info: CircleHelp,
  neutral: MinusCircle,
  formal: ShieldCheck,
};

/** "not_a_refinement" -> "Not a refinement". */
export function humanize(s: string): string {
  const t = s.replace(/[_-]+/g, ' ').trim();
  return t.charAt(0).toUpperCase() + t.slice(1);
}
