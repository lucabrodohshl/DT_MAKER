/**
 * Access to a twin's runtime through the Studio proxy (/api/v1/twins/{id}/...).
 *
 * `RuntimeStreamClient` subscribes to the runtime's SSE stream with the same
 * ordering discipline as the Studio stream: ids are monotone; a jump, a backwards
 * id (runtime restart) or a reconnect is a gap, after which consumers refetch the
 * authoritative state (GET /runtime/state).
 */
import { useQuery } from '@tanstack/react-query';
import { useEffect, useRef, useState } from 'react';
import { API_BASE, ApiError, api } from '@/api/client';
import type {
  Execution,
  LedgerPage,
  PredictionResult,
  RuntimePackage,
  RuntimeState,
  RuntimeStreamEvent,
  SimulationState,
  TwinIr,
} from './types';

export const runtimePath = (twinId: string, path: string) => `/twins/${encodeURIComponent(twinId)}${path}`;

/** Typed helpers bound to one twin. */
export function runtimeApi(twinId: string) {
  return {
    get: <T>(path: string, query?: Record<string, string | number | boolean | undefined>) =>
      api.get<T>(runtimePath(twinId, path), query),
    post: <T>(path: string, body?: unknown) => api.post<T>(runtimePath(twinId, path), body ?? {}),
  };
}
export type RuntimeApi = ReturnType<typeof runtimeApi>;

const retryUnlessNotConnected = (count: number, error: Error) =>
  !(error instanceof ApiError && (error.isRuntimeNotConnected || error.status === 404)) && count < 2;

export const runtimeKeys = {
  state: (twin: string) => ['runtime', twin, 'state'] as const,
  model: (twin: string) => ['runtime', twin, 'model'] as const,
  pkg: (twin: string) => ['runtime', twin, 'package'] as const,
  sim: (twin: string) => ['runtime', twin, 'simulation'] as const,
  executions: (twin: string) => ['runtime', twin, 'executions'] as const,
  ledger: (twin: string, session: string, since: number, kind: string) => ['runtime', twin, 'ledger', session, since, kind] as const,
};

export const useRuntimeState = (twinId: string | undefined | null) =>
  useQuery({
    queryKey: runtimeKeys.state(twinId ?? ''),
    queryFn: () => runtimeApi(twinId!).get<RuntimeState>('/runtime/state'),
    enabled: !!twinId,
    retry: retryUnlessNotConnected,
    staleTime: 0,
  });

export const useRuntimeModel = (twinId: string | undefined | null) =>
  useQuery({
    queryKey: runtimeKeys.model(twinId ?? ''),
    queryFn: () => runtimeApi(twinId!).get<TwinIr>('/runtime/model'),
    enabled: !!twinId,
    retry: retryUnlessNotConnected,
    staleTime: 5 * 60_000,
  });

export const useRuntimePackage = (twinId: string | undefined | null) =>
  useQuery({
    queryKey: runtimeKeys.pkg(twinId ?? ''),
    queryFn: () => runtimeApi(twinId!).get<RuntimePackage>('/runtime/package'),
    enabled: !!twinId,
    retry: retryUnlessNotConnected,
    staleTime: 0,
  });

export const useSimulationState = (twinId: string | undefined | null) =>
  useQuery({
    queryKey: runtimeKeys.sim(twinId ?? ''),
    queryFn: () => runtimeApi(twinId!).get<SimulationState>('/simulation/state'),
    enabled: !!twinId,
    retry: retryUnlessNotConnected,
    staleTime: 0,
  });

export const useExecutions = (twinId: string | undefined | null) =>
  useQuery({
    queryKey: runtimeKeys.executions(twinId ?? ''),
    queryFn: () => runtimeApi(twinId!).get<{ executions: Execution[] }>('/runtime/executions').then((r) => r.executions),
    enabled: !!twinId,
    retry: retryUnlessNotConnected,
    staleTime: 0,
  });

/** Ledger records: `tail` (latest N matching) when since === 0, otherwise forward paging from `since`. */
export const useLedger = (twinId: string | undefined | null, session: string, since: number, kind: string, limit = 200) =>
  useQuery({
    queryKey: [...runtimeKeys.ledger(twinId ?? '', session, since, kind), limit],
    queryFn: () =>
      runtimeApi(twinId!).get<LedgerPage>('/runtime/ledger', {
        session: session || undefined,
        ...(since > 0 ? { since, limit } : { tail: limit }),
        kind: kind || undefined,
      }),
    enabled: !!twinId,
    retry: retryUnlessNotConnected,
    staleTime: 0,
  });

export const predict = (twinId: string, body: { depth: number; horizon_ticks?: number; max_nodes?: number }) =>
  runtimeApi(twinId).post<PredictionResult>('/runtime/predict', body);

export type StreamStatus = 'idle' | 'connecting' | 'open' | 'reconnecting' | 'unavailable';

/**
 * Subscribe to a twin's runtime stream (through the Studio proxy).
 * `onEvent` receives contiguous events; `onGap` is called when ordering was lost.
 */
export class RuntimeStreamClient {
  private es: EventSource | null = null;
  private lastId = 0;
  private retryMs = 1000;
  private timer: ReturnType<typeof setTimeout> | null = null;
  private stopped = false;

  constructor(
    private readonly twinId: string,
    private readonly topics: string[],
    private readonly onEvent: (e: RuntimeStreamEvent) => void,
    private readonly onGap: (reason: string) => void,
    private readonly onStatus: (s: StreamStatus) => void,
    private readonly ES: new (url: string) => EventSource = EventSource,
  ) {}

  start(): void {
    this.stopped = false;
    this.open('connecting');
  }

  stop(): void {
    this.stopped = true;
    if (this.timer) clearTimeout(this.timer);
    this.es?.close();
    this.es = null;
  }

  private open(status: StreamStatus): void {
    if (this.stopped) return;
    this.onStatus(status);
    const es = new this.ES(`${API_BASE}${runtimePath(this.twinId, '/runtime/stream')}`);
    this.es = es;
    es.onopen = () => {
      this.retryMs = 1000;
      this.onStatus('open');
      // A (re)connection may have missed events: let consumers resynchronise.
      this.onGap('connected');
    };
    es.onerror = () => {
      es.close();
      if (this.stopped) return;
      this.onStatus('reconnecting');
      this.timer = setTimeout(() => this.open('reconnecting'), this.retryMs);
      this.retryMs = Math.min(this.retryMs * 2, 30_000);
    };
    const handle = (topic: string) => (raw: Event) => {
      const e = raw as MessageEvent<string>;
      const id = Number(e.lastEventId);
      if (Number.isFinite(id) && id > 0) {
        if (id <= this.lastId) {
          if (id < this.lastId - 1) {
            this.lastId = id;
            this.onGap('runtime restarted');
          }
          return;
        }
        if (this.lastId > 0 && id !== this.lastId + 1) this.onGap(`missed runtime events ${this.lastId + 1}–${id - 1}`);
        this.lastId = id;
      }
      let data: unknown;
      try {
        data = JSON.parse(e.data);
      } catch {
        data = e.data;
      }
      if (this.topics.includes(topic)) this.onEvent({ id, topic, data });
    };
    // The runtime numbers events in ONE sequence across all topics, so ordering is tracked on
    // every event it sends; only the subscribed topics are delivered. Listening to the
    // subscribed topics alone would see "missing" ids for every unsubscribed event and
    // report false gaps (each forcing a full resynchronisation).
    for (const t of new Set([...RUNTIME_STREAM_TOPICS, ...this.topics])) es.addEventListener(t, handle(t));
    es.addEventListener('upstream_error', () => this.onStatus('unavailable'));
  }
}

/** Every event name of GET /runtime/stream (docs/runtime-api.md, api/runtime.openapi.yaml StreamEvent). */
export const RUNTIME_STREAM_TOPICS = [
  'state', 'ledger', 'telemetry', 'pt_event', 'observation', 'decision', 'planning', 'plan',
  'map', 'map_reset', 'command', 'facility', 'sim', 'alarm', 'mission',
] as const;

/** React hook around RuntimeStreamClient. Handlers may change without reconnecting. */
export function useRuntimeStream(
  twinId: string | undefined | null,
  topics: string[],
  onEvent: (e: RuntimeStreamEvent) => void,
  onGap: (reason: string) => void,
  enabled = true,
): StreamStatus {
  const [status, setStatus] = useState<StreamStatus>('idle');
  const handlers = useRef({ onEvent, onGap });
  useEffect(() => {
    handlers.current = { onEvent, onGap };
  });
  const topicKey = topics.join(',');
  useEffect(() => {
    if (!twinId || !enabled || typeof EventSource === 'undefined') return;
    const client = new RuntimeStreamClient(
      twinId,
      topicKey.split(','),
      (e) => handlers.current.onEvent(e),
      (r) => handlers.current.onGap(r),
      setStatus,
    );
    client.start();
    return () => client.stop();
  }, [twinId, topicKey, enabled]);
  return status;
}

export type { SimulationState, RuntimeState, TwinIr };
