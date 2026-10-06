/**
 * Connects the live stream to the query cache.
 *
 * Events only *invalidate* affected queries; components then refetch the
 * authoritative state. Invalidation never resets component state (selection,
 * scroll, focus, editor drafts), because those live in components, not in the cache.
 * Telemetry bursts are coalesced (at most one invalidation per channel per 2 s).
 */
import { useQueryClient, type QueryClient } from '@tanstack/react-query';
import { createContext, useContext, useEffect, useMemo, useSyncExternalStore, type ReactNode } from 'react';
import { API_BASE } from '@/api/client';
import { LiveStream, type LiveEvent, type LiveSnapshot } from './stream';

interface LiveContextValue {
  stream: LiveStream | null;
}

const LiveContext = createContext<LiveContextValue>({ stream: null });

/** Topic → query-key prefixes to invalidate. */
const TOPIC_KEYS: Record<string, string[][]> = {
  artifact: [['artifacts'], ['impact'], ['changes'], ['overview']],
  evidence: [['evidence'], ['changes'], ['twins'], ['impact'], ['artifacts'], ['overview']],
  change: [['changes'], ['overview']],
  package: [['packages'], ['changes'], ['twins'], ['overview']],
  deployment: [['deployments'], ['twins'], ['packages'], ['assets'], ['overview']],
  audit: [['audit']],
};

export function applyEvent(qc: QueryClient, event: LiveEvent, throttle: Map<string, number>, now = Date.now()): void {
  if (event.topic === 'telemetry') {
    const channel = String(event.data.channelId ?? '');
    const last = throttle.get(channel) ?? 0;
    if (now - last < 2000) return;
    throttle.set(channel, now);
    void qc.invalidateQueries({ queryKey: ['telemetry'] });
    return;
  }
  for (const key of TOPIC_KEYS[event.topic] ?? []) void qc.invalidateQueries({ queryKey: key });
  void qc.invalidateQueries({ queryKey: ['audit'] });
}

export function LiveProvider({ children, enabled = true }: { children: ReactNode; enabled?: boolean }) {
  const qc = useQueryClient();
  const stream = useMemo(() => {
    if (!enabled || typeof EventSource === 'undefined') return null;
    const throttle = new Map<string, number>();
    return new LiveStream(`${API_BASE}/stream`, {
      onEvent: (e) => applyEvent(qc, e, throttle),
      onResync: () => void qc.invalidateQueries(),
    });
  }, [qc, enabled]);

  useEffect(() => {
    stream?.start();
    return () => stream?.stop();
  }, [stream]);

  return <LiveContext.Provider value={{ stream }}>{children}</LiveContext.Provider>;
}

const OFFLINE: LiveSnapshot = {
  connection: 'closed',
  lastSeq: 0,
  epoch: null,
  paused: false,
  pendingWhilePaused: 0,
  lastEventAt: null,
  gaps: 0,
};

/** Current live-stream status (connection, paused, pending events). */
export function useLive(): LiveSnapshot & { pause: () => void; resume: () => void } {
  const { stream } = useContext(LiveContext);
  const snap = useSyncExternalStore(
    stream?.subscribe ?? (() => () => undefined),
    stream?.getSnapshot ?? (() => OFFLINE),
    () => OFFLINE,
  );
  return {
    ...snap,
    pause: () => stream?.pause(),
    resume: () => stream?.resume(),
  };
}
