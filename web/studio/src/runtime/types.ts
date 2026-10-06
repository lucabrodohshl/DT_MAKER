/**
 * Types of the twin-runtime API as proxied by Studio under /api/v1/twins/{twinId}/...
 * (authoritative contract: docs/runtime-api.md, owned by the runtime).
 *
 * Everything here is kernel output. The UI renders it; it never recomputes
 * enabledness, admissibility, propositions or transitions.
 */

/** Logical model time: exact integer ticks plus the decimal rendering in model units. */
export interface LogicalTime {
  ticks: number;
  text: string;
}

export interface Configuration {
  location: string;
  clocks: Record<string, LogicalTime>;
  time: LogicalTime;
}

export interface EnabledTransition {
  member: number;
  transition: string;
  label: string;
  source: string;
  target: string;
  guard: string;
  resets?: string[];
  /** I_D of the label (domain meaning), as reported by the runtime. */
  interpretation?: string;
  window: { earliest: LogicalTime; latest: LogicalTime | null };
  enabled_now: boolean;
}

export interface Proposition {
  id: string;
  location?: string;
  interpretation: string;
}

/** Conformance computed by the runtime only from the kernel's verdicts. */
export interface Conformance {
  status: 'conformant' | 'violated' | string;
  observations: number;
  observations_rejected: number;
  decisions: number;
  decisions_rejected: number;
  alarms: number;
  first_violation_seq: number | null;
  first_violation: string;
  definition: string;
}

export interface LastTransition {
  seq: number;
  transition: string;
  label: string;
  from: string;
  to: string;
  source: string;
  at: LogicalTime;
}

export interface RuntimeState {
  session: string;
  package_hash?: string;
  model?: { id: string; version: string; ir_sha256: string };
  last_transition?: LastTransition | null;
  conformance?: Conformance;
  time: LogicalTime;
  configurations: Configuration[];
  locations?: string[];
  deterministic: boolean;
  propositions: Proposition[];
  enabled: EnabledTransition[];
  deadline: LogicalTime | null;
  ledger: { records: number; head: string };
  failed: boolean;
  closed: boolean;
}

export interface IrConstraint {
  clock: string;
  op: '<' | '<=' | '>' | '>=' | '==';
  bound: number;
}

/** Twin IR (format twin-ir/1) — identical whether served by the runtime or read from a package. */
export interface TwinIr {
  format: string;
  model: { id: string; version: string; source_sha256: string; source_template: string };
  time: { ticks_per_unit: number };
  clocks: string[];
  channels: string[];
  initial: string;
  locations: { id: string; invariant: IrConstraint[] }[];
  transitions: {
    id: string;
    source: string;
    target: string;
    action: { channel?: string; kind: 'send' | 'receive' | 'tau' | string };
    guard: IrConstraint[];
    resets: string[];
  }[];
  propositions: { id: string; location: string; interpretation: string }[];
  event_interpretations: { label: string; formula: string }[];
}

export interface RuntimePackage {
  package_hash: string;
  ir_sha256: string;
  source_sha256: string;
  directory: string;
  manifest: {
    model: { id: string; version: string };
    kernel_compat: string;
    created_at: string;
    verification: { aligned: boolean; lint_clean: boolean; translation_validated: boolean; event_deterministic: boolean };
  };
  checks: { name: string; passed: boolean }[];
  alignment: {
    aligned: boolean;
    lint_clean: boolean;
    label_equivalence: { pt: string; dt: string[] | string }[];
    syntactic_baseline: { aligned: boolean };
  };
}

export interface PredictionStep {
  transition: string;
  label: string;
  window: { earliest: LogicalTime; latest: LogicalTime | null };
  state: Configuration;
}

export interface PredictionResult {
  from?: RuntimeState;
  limits?: Record<string, unknown>;
  note?: string;
  exploration: {
    root: Configuration;
    nodes: number;
    truncated: boolean;
    trajectories: PredictionStep[][];
  }[];
}

export interface LedgerRecord {
  seq: number;
  kind?: string;
  hash: string;
  body: {
    schema: string;
    seq: number;
    session: string;
    kind: string;
    topic?: string;
    at?: number;
    time_before?: number;
    time_after?: number;
    prev_hash: string;
    package: { hash: string; ir_sha256: string; model_id: string; model_version: string };
    kernel_version: string;
    wall_time?: string;
    data?: Record<string, unknown>;
    /** Step records: the input the kernel processed. */
    input?: { at: number; kind: string; name: string; source: string; payload?: Record<string, unknown> };
    input_digest?: string;
    /** Step records: what the kernel decided (branches taken, delay) or why it refused. */
    outcome?: {
      delay?: number;
      branches?: { transition: string; label: string; source: string; target: string; guard: unknown[]; resets: string[]; from_member: number; to_member: number }[];
      error?: { code: string; message: string; context?: { key: string; value: string }[] };
      accepted?: boolean;
    };
    propositions?: string[];
    state_after?: { location: string; clocks: Record<string, number>; time: number }[];
    state_before?: { location: string; clocks: Record<string, number>; time: number }[];
    time_base?: number;
    [key: string]: unknown;
  };
}

export interface LedgerPage {
  session?: string;
  ledger?: string;
  total_records?: number;
  matched?: number;
  records: LedgerRecord[];
}

export interface LedgerVerification {
  valid: boolean;
  records: number;
  session: string;
  package_hash: string;
  running_package?: boolean;
  head_seq: number;
  head_hash: string;
  has_end_record: boolean;
  issues: { line: number; code: string; message: string }[];
  replay?: { identical: boolean; steps: number; mismatches: unknown[] } & Record<string, unknown>;
}

export interface Execution {
  session: string;
  ledger: string;
  telemetry?: string | null;
  package_hash: string;
  model_id?: string;
  model_version?: string;
  records: number;
  ended: boolean;
  started: string;
  ended_at?: string | null;
  last_time_ticks?: number;
  last_location?: string;
  current: boolean;
  replayable: boolean;
}

/** A configuration as recorded in the ledger: clocks and time in integer ticks. */
export interface RecordedConfiguration {
  location: string;
  clocks: Record<string, number>;
  time: number;
}

export interface RecordedBranch {
  from_member?: number;
  transition: string;
  label: string;
  source: string;
  target: string;
  guard: { atom: string; value?: unknown; bound?: unknown; holds: boolean }[];
  resets: string[];
  to_member?: number;
}

/**
 * One recomputed ledger record of POST /runtime/replay (docs/runtime-api.md). `fields` is the
 * kind-specific record body; which members are present depends on `kind`.
 */
export interface ReplayFrameRecord {
  seq: number;
  kind: 'genesis' | 'step' | 'delay' | 'reject' | 'alarm' | 'context' | 'end' | (string & {});
  hash: string;
  fields: {
    input?: { source?: string; kind?: string; name?: string; at?: number; payload?: unknown };
    input_digest?: string;
    time_base?: number;
    time_before?: number;
    time_after?: number;
    state_before?: RecordedConfiguration[];
    state_after?: RecordedConfiguration[];
    outcome?: { delay?: number; branches?: RecordedBranch[] };
    propositions?: string[];
    error?: { code: string; message: string; context?: Record<string, string> };
    alarm?: string;
    detail?: string;
    reason?: string;
    topic?: string;
    at?: number;
    data?: unknown;
    [key: string]: unknown;
  };
}

export interface ReplayResult {
  chain: LedgerVerification | { valid: boolean; [key: string]: unknown };
  identical: boolean;
  steps: number;
  delays: number;
  rejections: number;
  alarms: number;
  contexts: number;
  mismatches: unknown[];
  final_state: unknown;
  frames?: ReplayFrameRecord[];
  package: { package_hash?: string; ir_sha256?: string; model_id?: string; model_version?: string; running?: boolean; [key: string]: unknown };
  telemetry?: Record<string, unknown>[];
}

export interface SimulateResult {
  admissible: boolean;
  steps: Record<string, unknown>[];
  refusal: { code: string; message: string; context: { key: string; value: string }[] } | null;
  note: string;
}

export interface PlanCandidate {
  label: string;
  profile: string;
  found: boolean;
  failure?: string;
  duplicate_of?: string | null;
  plan?: {
    id: number | string;
    waypoints_mm: [number, number][] | { x: number; y: number }[];
    start_mm?: unknown;
    cost_mm: number;
    objective_mm: number;
    length_mm: number;
    unknown_mm: number;
    energy_mwh: number;
    expanded: number;
  } | null;
  geometric?: { ok: boolean; issues: string[] };
  behavioural?: { checked: boolean; ok: boolean; detail: string; schedule?: unknown[] };
  selected: boolean;
}

export interface PlanningEpisode {
  id: number | string;
  at: number | LogicalTime;
  goal: string;
  reason: string;
  decision: string;
  selected: string | null;
  candidates: PlanCandidate[];
}

export interface Decision {
  accepted: boolean;
  at: number;
  label: string;
  ledger_seq?: number;
  reason?: string;
}

/**
 * GET /simulation/state. Co-simulation hosts (drone) report pacing; monitor hosts (pump) report
 * the physical feed instead, so the mode-specific members are optional.
 */
export interface SimulationState {
  status: 'running' | 'paused' | 'finished' | 'failed' | 'monitoring' | (string & {});
  mode?: 'cosimulation' | 'monitor';
  session: string;
  now: LogicalTime;
  ledger: string;
  /** Co-simulation only. */
  mission_started?: boolean;
  speed_permille?: number;
  ticks?: number;
  error?: string;
  /** Monitor only. */
  pt_events?: number;
  telemetry_samples?: number;
  last_telemetry?: LogicalTime;
}

/** A runtime SSE event as received through the Studio proxy. */
export interface RuntimeStreamEvent {
  id: number;
  topic: string;
  data: unknown;
}

// ------------------------------------------------------------------ what-if (POST /runtime/what-if)
/** One atom's contribution to a delay window (kernel::explain_window). */
export interface WindowFactor {
  origin: 'source_invariant' | 'guard' | 'target_invariant';
  atom: string;
  value_now: LogicalTime;
  bound: LogicalTime;
  depends_on_delay: boolean;
  min_delay: LogicalTime | null;
  max_delay: LogicalTime | null;
  never: boolean;
}

/** An admissible delay interval, relative to the state and as absolute logical time. */
export interface DelayInterval {
  earliest: LogicalTime;
  latest: LogicalTime | null;
  earliest_at: LogicalTime;
  latest_at: LogicalTime | null;
}

export interface EventAlternative {
  member: number;
  location: string;
  transition: string;
  source: string;
  target: string;
  guard: string;
  resets: string[];
  window: DelayInterval | null;
  enabled_now: boolean;
  factors: WindowFactor[];
}

export interface EventAvailability {
  label: string;
  status: 'now' | 'later' | 'blocked';
  intervals: DelayInterval[];
  alternatives: EventAlternative[];
}

export interface RecordedConfiguration2 {
  location: string;
  clocks: Record<string, LogicalTime>;
  time: LogicalTime;
}

export interface WhatIfStateSet {
  time: LogicalTime;
  configurations: RecordedConfiguration2[];
  deterministic: boolean;
}

export interface WhatIfFinal {
  state: WhatIfStateSet;
  propositions: { id: string; interpretation: string }[];
  max_delay: LogicalTime | null;
  max_delay_at: LogicalTime | null;
  invariants: { location: string; invariant: string }[];
  availability: EventAvailability[];
  unavailable: { label: string; from_locations: string[] }[];
}

export interface WhatIfStep {
  index: number;
  kind: 'delay' | 'event';
  status: 'ok' | 'invalid' | 'not_evaluated';
  requested?: { label?: string; transition?: string | null; delay: LogicalTime; at: LogicalTime };
  branches?: { from: number; to: number; transition: string; label: string; source: string; target: string }[];
  after?: WhatIfStateSet;
  propositions?: { id: string; interpretation: string }[];
  error?: { code: string; message: string };
  explanation?: {
    requested_delay?: LogicalTime;
    alternatives?: EventAlternative[];
    reasons?: { transition: string | null; target: string | null; reason: string }[];
    max_delay?: LogicalTime | null;
    max_delay_at?: LogicalTime | null;
    invariants?: { location: string; invariant: string }[];
  };
  note?: string;
}

export interface WhatIfResult {
  start: { kind: string; state: WhatIfStateSet };
  steps: WhatIfStep[];
  first_invalid: number | null;
  final: WhatIfFinal;
  note: string;
}

/** A scenario step as sent to the runtime (times are exact decimal strings). */
export type ScenarioStep =
  | { kind: 'delay'; delay: string }
  | { kind: 'event'; label?: string; transition?: string; delay?: string; at?: string };

export type ScenarioStart =
  | { kind: 'current' }
  | { kind: 'initial' }
  | { kind: 'configurations'; configurations: { location: string; clocks: Record<string, string>; time: string }[] };
