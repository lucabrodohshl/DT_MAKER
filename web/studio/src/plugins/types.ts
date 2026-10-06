/**
 * Domain visualisation plugin interface.
 *
 * The generic product works without any plugin. A plugin adds a domain-specific
 * view (a map, a floor plan, a P&ID, a workcell, ...) to an asset's pages. It
 * receives everything as *read-only inputs* and must not decide any behavioural
 * question itself: modes, admissibility, enabled transitions and plan acceptance
 * all come from the runtime (kernel) through `runtime` / `state`.
 *
 * Lifecycle hooks requested by the product specification map onto this API as:
 *   onAssetLoaded   → props.asset / props.twin change
 *   onSemanticState → props.state changes (kernel state, already ordered and resynced)
 *   onTelemetry / onTransition / onPrediction → props.subscribe(['telemetry' | 'decision' | ...], cb)
 *   onReplayFrame   → props.mode === 'replay' and props.replay changes
 */
import type { ComponentType } from 'react';
import type { AssetDetail, TwinDetail, TwinSummary } from '@/api/types';
import type { RuntimeApi } from '@/runtime/client';
import type { LedgerRecord, RuntimeState, RuntimeStreamEvent } from '@/runtime/types';

/** One frame of a replay, produced by the runtime re-executing a recorded execution. */
export interface ReplayFrame {
  /** Session (execution) being replayed. */
  session: string;
  /** Index of the frame within the replay (0-based). */
  index: number;
  /** Logical time of the frame (ticks). */
  ticks: number;
  /** The ledger record this frame corresponds to. */
  record: LedgerRecord | null;
  /** Frame payload as returned by POST /runtime/replay (state after, decision, context...). */
  frame: Record<string, unknown>;
}

export interface DomainPluginProps {
  /** Twin definition: presentation metadata, deployment, bound artefacts, trust summary. */
  twin: TwinDetail;
  /** The asset the twin is bound to (if any). */
  asset: AssetDetail | null;
  /** 'live' (current execution) or 'replay' (a recorded execution; never mixed). */
  mode: 'live' | 'replay';
  /** Latest kernel state (live mode), refreshed on every runtime event and after gaps. */
  state: RuntimeState | null;
  /** Typed GET/POST against this twin's runtime/world/planner/observer via the Studio proxy. */
  runtime: RuntimeApi;
  /**
   * Subscribe to runtime stream topics (e.g. 'telemetry', 'decision', 'command', 'map',
   * 'plan', 'state'). Returns an unsubscribe function. Events are delivered in order;
   * `onGap` fires when ordering was lost (refetch what you display).
   */
  subscribe: (topics: string[], onEvent: (e: RuntimeStreamEvent) => void, onGap?: (reason: string) => void) => () => void;
  /** Current replay frame (replay mode only). */
  replay: ReplayFrame | null;
  /** Whether the runtime is reachable (false: show only what the plugin can render honestly). */
  runtimeConnected: boolean;
}

export interface DomainPlugin {
  /** Stable id, e.g. "drone". */
  id: string;
  /** Tab title shown on the asset page, e.g. "Mission map". */
  title: string;
  /** Short description for the plugin list. */
  description: string;
  /** Whether this plugin applies to a twin (typically `twin.presentation.plugin === id`). */
  matches: (twin: TwinSummary) => boolean;
  /** The view. Must work without the runtime (render an explanatory empty state). */
  Component: ComponentType<DomainPluginProps>;
}
