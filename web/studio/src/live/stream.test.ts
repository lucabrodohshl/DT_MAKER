import { describe, expect, it, vi } from 'vitest';
import { LiveStream } from './stream';

/** Minimal controllable EventSource. */
class FakeES {
  static last: FakeES | null = null;
  onopen: (() => void) | null = null;
  onerror: (() => void) | null = null;
  onmessage: ((e: MessageEvent<string>) => void) | null = null;
  listeners = new Map<string, ((e: MessageEvent<string>) => void)[]>();
  closed = false;
  constructor(public url: string) {
    FakeES.last = this;
  }
  addEventListener(type: string, fn: (e: MessageEvent<string>) => void) {
    this.listeners.set(type, [...(this.listeners.get(type) ?? []), fn]);
  }
  close() {
    this.closed = true;
  }
  emit(type: string, data: unknown) {
    const e = { data: JSON.stringify(data) } as MessageEvent<string>;
    (this.listeners.get(type) ?? []).forEach((f) => f(e));
  }
}

function setup() {
  const onEvent = vi.fn();
  const onResync = vi.fn();
  const s = new LiveStream('/api/v1/stream', { onEvent, onResync }, FakeES as unknown as new (u: string) => EventSource);
  s.start();
  const es = FakeES.last!;
  es.onopen?.();
  es.emit('hello', { epoch: 'A', head: 0 });
  return { s, es, onEvent, onResync };
}

describe('LiveStream', () => {
  it('applies contiguous events and drops duplicates', () => {
    const { es, onEvent, onResync, s } = setup();
    es.emit('artifact', { seq: 1, topic: 'artifact', at: 't', data: {} });
    es.emit('artifact', { seq: 2, topic: 'artifact', at: 't', data: {} });
    es.emit('artifact', { seq: 2, topic: 'artifact', at: 't', data: {} });
    expect(onEvent).toHaveBeenCalledTimes(2);
    expect(onResync).not.toHaveBeenCalled();
    expect(s.getSnapshot().lastSeq).toBe(2);
    expect(s.getSnapshot().connection).toBe('open');
  });

  it('detects a sequence gap and asks for a resync', () => {
    const { es, onResync, s } = setup();
    es.emit('artifact', { seq: 1, topic: 'artifact', at: 't', data: {} });
    es.emit('artifact', { seq: 5, topic: 'artifact', at: 't', data: {} });
    expect(onResync).toHaveBeenCalledWith(expect.stringContaining('expected event 2'));
    expect(s.getSnapshot().gaps).toBe(1);
  });

  it('treats a server-reported resync and a new epoch (restart) as gaps', () => {
    const { es, onResync } = setup();
    es.emit('resync', { epoch: 'A' });
    expect(onResync).toHaveBeenCalledTimes(1);
    es.emit('hello', { epoch: 'B', head: 0 });
    expect(onResync).toHaveBeenCalledTimes(2);
  });

  it('pause counts events without applying them; resume refetches', () => {
    const { es, s, onEvent, onResync } = setup();
    s.pause();
    es.emit('evidence', { seq: 1, topic: 'evidence', at: 't', data: {} });
    es.emit('evidence', { seq: 2, topic: 'evidence', at: 't', data: {} });
    expect(onEvent).not.toHaveBeenCalled();
    expect(s.getSnapshot().pendingWhilePaused).toBe(2);
    s.resume();
    expect(onResync).toHaveBeenCalledWith('resumed');
    expect(s.getSnapshot().paused).toBe(false);
  });

  it('reconnects after an error with the last sequence as resume point', () => {
    vi.useFakeTimers();
    const { es, s } = setup();
    es.emit('artifact', { seq: 7, topic: 'artifact', at: 't', data: {} });
    es.onerror?.();
    expect(s.getSnapshot().connection).toBe('reconnecting');
    vi.advanceTimersByTime(1000);
    expect(FakeES.last!.url).toContain('lastEventId=7');
    vi.useRealTimers();
    s.stop();
  });
});
