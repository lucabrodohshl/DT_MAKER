/**
 * Live stream client (Server-Sent Events from /api/v1/stream).
 *
 * Responsibilities:
 * - connection state with exponential-backoff reconnects;
 * - ordering: every event carries a strictly increasing `seq` (SSE id). A jump in
 *   `seq`, a server `resync` event, or a new server `epoch` (restart) is a **gap**:
 *   the consumer is told to refetch authoritative state instead of trusting deltas;
 * - duplicates (seq <= last seen) are dropped, so at-least-once delivery is safe;
 * - pause/resume: while paused, events are counted but not applied, and the UI
 *   shows that it is not displaying the latest data.
 *
 * Live events are notifications only. The truth is always re-read from the REST API.
 */

export type ConnectionState = 'connecting' | 'open' | 'reconnecting' | 'closed';

export interface LiveEvent {
  seq: number;
  topic: string;
  at: string;
  data: Record<string, unknown>;
}

export interface LiveSnapshot {
  connection: ConnectionState;
  lastSeq: number;
  epoch: string | null;
  paused: boolean;
  pendingWhilePaused: number;
  lastEventAt: string | null;
  gaps: number;
}

export interface LiveHandlers {
  /** A contiguous event to apply (invalidate affected queries). */
  onEvent: (event: LiveEvent) => void;
  /** Ordering was lost (gap, resync, restart, resume): refetch everything shown. */
  onResync: (reason: string) => void;
}

type EventSourceCtor = new (url: string) => EventSource;

export class LiveStream {
  private source: EventSource | null = null;
  private snapshot: LiveSnapshot = {
    connection: 'closed',
    lastSeq: 0,
    epoch: null,
    paused: false,
    pendingWhilePaused: 0,
    lastEventAt: null,
    gaps: 0,
  };
  private listeners = new Set<() => void>();
  private retryMs = 1000;
  private retryTimer: ReturnType<typeof setTimeout> | null = null;
  private stopped = true;

  constructor(
    private readonly url: string,
    private readonly handlers: LiveHandlers,
    private readonly ES: EventSourceCtor = EventSource,
  ) {}

  start(): void {
    this.stopped = false;
    this.connect('connecting');
  }

  stop(): void {
    this.stopped = true;
    if (this.retryTimer) clearTimeout(this.retryTimer);
    this.source?.close();
    this.source = null;
    this.update({ connection: 'closed' });
  }

  pause(): void {
    this.update({ paused: true, pendingWhilePaused: 0 });
  }

  /** Resume and jump to now: everything shown is refetched. */
  resume(): void {
    this.update({ paused: false, pendingWhilePaused: 0 });
    this.handlers.onResync('resumed');
  }

  getSnapshot = (): LiveSnapshot => this.snapshot;

  subscribe = (listener: () => void): (() => void) => {
    this.listeners.add(listener);
    return () => this.listeners.delete(listener);
  };

  private update(patch: Partial<LiveSnapshot>): void {
    this.snapshot = { ...this.snapshot, ...patch };
    this.listeners.forEach((l) => l());
  }

  private connect(state: ConnectionState): void {
    if (this.stopped) return;
    this.update({ connection: state });
    const resume = this.snapshot.lastSeq > 0 ? `?lastEventId=${this.snapshot.lastSeq}` : '';
    const es = new this.ES(`${this.url}${resume}`);
    this.source = es;
    es.onopen = () => {
      this.retryMs = 1000;
      this.update({ connection: 'open' });
    };
    es.onerror = () => {
      es.close();
      if (this.source === es) this.source = null;
      if (this.stopped) return;
      this.update({ connection: 'reconnecting' });
      this.retryTimer = setTimeout(() => this.connect('reconnecting'), this.retryMs);
      this.retryMs = Math.min(this.retryMs * 2, 30_000);
    };
    es.addEventListener('hello', (e) => this.onHello(e as MessageEvent<string>));
    es.addEventListener('resync', () => this.gap('server reported missed events'));
    es.onmessage = (e) => this.onData(e);
    // Named topics are delivered as typed events; route them all through onData.
    for (const topic of ['artifact', 'evidence', 'telemetry', 'deployment', 'change', 'package', 'audit', 'test']) {
      es.addEventListener(topic, (e) => this.onData(e as MessageEvent<string>));
    }
  }

  private onHello(e: MessageEvent<string>): void {
    let epoch: string | null;
    try {
      epoch = (JSON.parse(e.data) as { epoch?: string }).epoch ?? null;
    } catch {
      epoch = null;
    }
    if (this.snapshot.epoch !== null && epoch !== this.snapshot.epoch) {
      // The server restarted: sequence numbers restarted too.
      this.update({ epoch, lastSeq: 0 });
      this.gap('server restarted');
      return;
    }
    this.update({ epoch });
  }

  private gap(reason: string): void {
    this.update({ gaps: this.snapshot.gaps + 1 });
    if (!this.snapshot.paused) this.handlers.onResync(reason);
  }

  private onData(e: MessageEvent<string>): void {
    let event: LiveEvent;
    try {
      event = JSON.parse(e.data) as LiveEvent;
    } catch {
      return;
    }
    if (typeof event.seq !== 'number') return;
    const last = this.snapshot.lastSeq;
    if (event.seq <= last) return; // duplicate or replayed
    if (last > 0 && event.seq !== last + 1) this.gap(`expected event ${last + 1}, received ${event.seq}`);
    this.update({ lastSeq: event.seq, lastEventAt: event.at });
    if (this.snapshot.paused) {
      this.update({ pendingWhilePaused: this.snapshot.pendingWhilePaused + 1 });
      return;
    }
    this.handlers.onEvent(event);
  }
}
