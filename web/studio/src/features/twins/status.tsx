/**
 * Twin status shared by the twin library, the workspace header and the overview.
 * Every value shown is a backend conclusion: the kernel's mode and conformance verdict
 * from the runtime, trust states computed by twin-studio from stored evidence.
 */
import { CircleSlash, PlugZap } from 'lucide-react';
import type { TwinDetail, TwinSummary, TrustState } from '@/api/types';
import { useRuntimeState } from '@/runtime/client';
import type { RuntimeState } from '@/runtime/types';
import { StatusBadge, TONE_ICON, toneOf } from '@/design';

export type Operational = 'running' | 'not_connected' | 'unreachable' | 'loading' | 'stopped';

export interface LiveStatus {
  operational: Operational;
  state: RuntimeState | undefined;
  /** Location of the (first) possible configuration, as reported by the kernel. */
  location: string | undefined;
  label: string | undefined;
  tone: ReturnType<typeof toneOf>;
}

/** Live status of a twin from its runtime (polls; pass null to disable). */
export function useLiveStatus(twin: TwinSummary | TwinDetail | null | undefined): LiveStatus {
  const configured = !!twin?.runtimeUrl;
  const q = useRuntimeState(configured ? twin!.id : null);
  const location = q.data?.configurations[0]?.location;
  const p = location ? twin?.presentation.states?.[location] : undefined;
  const operational: Operational = !configured
    ? 'not_connected'
    : q.isError
      ? 'unreachable'
      : !q.data
        ? 'loading'
        : q.data.closed || q.data.failed
          ? 'stopped'
          : 'running';
  return { operational, state: q.data, location, label: p?.label ?? location, tone: toneOf(p?.tone) };
}

export function OperationalBadge({ status, size }: { status: LiveStatus; size?: 'lg' }) {
  switch (status.operational) {
    case 'not_connected':
      return <StatusBadge tone="neutral" icon={PlugZap} label="No runtime" title="No twin-runtime is configured for this twin" size={size} />;
    case 'unreachable':
      return <StatusBadge tone="warning" icon={PlugZap} label="Runtime unreachable" size={size} />;
    case 'loading':
      return <StatusBadge tone="neutral" label="Connecting…" size={size} />;
    case 'stopped':
      return <StatusBadge tone="neutral" icon={CircleSlash} label="Execution ended" title="The runtime's current execution is closed; a new one starts when the twin is restarted" size={size} />;
    default:
      return <StatusBadge tone="ok" label="Live" title="The runtime is executing the twin and judging observations" size={size} />;
  }
}

/** The kernel's current mode (behavioural state), with its presentation label and tone. */
export function ModeBadge({ status, size }: { status: LiveStatus; size?: 'lg' }) {
  if (!status.location) return null;
  const many = (status.state?.configurations.length ?? 0) > 1;
  return (
    <StatusBadge
      tone={status.tone}
      icon={TONE_ICON[status.tone]}
      label={many ? `${status.label} (+${status.state!.configurations.length - 1} possible)` : status.label ?? status.location}
      title={many ? 'Several configurations are consistent with the observations (nondeterminism is kept, never resolved).' : `Location ${status.location}`}
      size={size}
    />
  );
}

export function ConformanceBadge({ status }: { status: LiveStatus }) {
  const c = status.state?.conformance;
  if (!c) return null;
  return c.status === 'conformant'
    ? <StatusBadge tone="ok" label="Conformant" title={c.definition} />
    : <StatusBadge tone="critical" label="Deviation detected" title={c.first_violation} />;
}

/**
 * Worst trust state of a twin over its applicable checks (alignment, refinement, integrity,
 * compatibility, compilation). Checks that do not apply are ignored; "pass" only if every
 * applicable check passed.
 */
export function overallTrust(twin: TwinDetail | undefined): TrustState | undefined {
  if (!twin) return undefined;
  const order: TrustState[] = ['fail', 'invalidated', 'error', 'stale', 'unknown', 'check_running', 'unavailable', 'not_checked', 'blocked', 'pass'];
  const states: TrustState[] = Object.values(twin.trust).map((t) => t.state).filter((s) => s !== 'not_applicable');
  if (states.length === 0) return 'not_applicable';
  return order.find((s) => states.includes(s)) ?? 'unknown';
}

/**
 * Problems the runtime reports for the current execution: observations the model did not admit
 * and monitoring alarms (missed deadlines). Rejected planner proposals are not counted: refusing
 * an inadmissible decision is the kernel doing its job.
 */
export function alertCount(state: RuntimeState | undefined): number {
  const c = state?.conformance;
  if (!c) return 0;
  return (c.observations_rejected ?? 0) + (c.alarms ?? 0);
}
