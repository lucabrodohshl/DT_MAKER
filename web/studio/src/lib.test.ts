/** Pure helpers: exact logical time, prediction tree, grouping, CSV export, chart alignment, schemas. */
import { describe, expect, it } from 'vitest';
import { safeTrustState, validated, twinTrustSchema } from '@/api/schemas';
import type { TelemetrySeries } from '@/api/types';
import { group } from '@/features/operations/EventsPage';
import { decimalToTicks, toTree } from '@/features/predict/AssetPredictTab';
import { buildAligned } from '@/features/telemetry/TimeSeriesChart';
import { exportCsv } from '@/features/telemetry/TelemetryExplorer';
import { ticksToText } from '@/runtime/time';
import type { PredictionStep } from '@/runtime/types';

describe('logical time', () => {
  it('renders ticks exactly, without floating point', () => {
    expect(ticksToText(16800, 1000)).toBe('16.8');
    expect(ticksToText(1, 1000)).toBe('0.001');
    expect(ticksToText(3000, 1000)).toBe('3');
    expect(ticksToText(-250, 1000)).toBe('-0.25');
  });
  it('parses exact decimals to ticks and rejects inexact input', () => {
    expect(decimalToTicks('17.5', 1000)).toBe(17500);
    expect(decimalToTicks('60', 1000)).toBe(60000);
    expect(decimalToTicks('0.0001', 1000)).toBeNull();
    expect(decimalToTicks('1e3', 1000)).toBeNull();
    expect(decimalToTicks('-1', 1000)).toBeNull();
  });
});

describe('prediction tree', () => {
  const step = (id: string, loc: string): PredictionStep => ({
    transition: id,
    label: `${id}!`,
    window: { earliest: { ticks: 0, text: '0' }, latest: null },
    state: { location: loc, clocks: {}, time: { ticks: 0, text: '0' } },
  });
  it('merges trajectories sharing a prefix', () => {
    const tree = toTree([[step('a', 'X')], [step('a', 'X'), step('b', 'Y')], [step('c', 'Z')]]);
    expect(tree).toHaveLength(2);
    expect(tree[0]!.children).toHaveLength(1);
    expect(tree[0]!.children[0]!.step.state.location).toBe('Y');
  });
});

describe('event grouping', () => {
  it('collapses consecutive identical events only', () => {
    const g = group(['a', 'a', 'b', 'a'], (x) => x);
    expect(g.map((x) => [x.item, x.count])).toEqual([['a', 2], ['b', 1], ['a', 1]]);
  });
});

const channel = {
  id: 'p.t',
  assetId: 'p',
  name: 't',
  valueType: 'number' as const,
  unit: 'degC',
  ontologySymbol: 't',
  source: 's',
  expectedPeriodMs: 1000,
  presentation: { label: 'Temp' },
};

describe('telemetry', () => {
  it('inserts gaps instead of interpolating across missing data', () => {
    const series: TelemetrySeries = {
      channelId: 'p.t',
      channel,
      from: '',
      to: '',
      totalSamples: 3,
      downsampled: false,
      samples: [0, 1000, 10000].map((t) => ({ observedAt: '', observedMs: t, ingestedAt: '', quality: 'good' as const, value: 1 })),
    };
    const { data } = buildAligned([{ series, color: '--series-1' }]);
    const values = data[1] as (number | null)[];
    expect(values).toContain(null);
    expect(values.filter((v) => v !== null)).toHaveLength(3);
  });
  it('marks quality-flagged samples', () => {
    const series: TelemetrySeries = {
      channelId: 'p.t', channel, from: '', to: '', totalSamples: 2, downsampled: false,
      samples: [
        { observedAt: '', observedMs: 0, ingestedAt: '', quality: 'good', value: 1 },
        { observedAt: '', observedMs: 1000, ingestedAt: '', quality: 'bad', value: 2 },
      ],
    };
    expect(buildAligned([{ series, color: '--series-1' }]).badMarkers).toBe(1);
  });
  it('exports CSV with observation and ingestion times and quality', () => {
    const csv = exportCsv([{ channelId: 'p.t', channel, from: '', to: '', totalSamples: 1, downsampled: false,
      samples: [{ observedAt: '2026-01-01T00:00:00.000Z', observedMs: 0, ingestedAt: '2026-01-01T00:00:00.040Z', quality: 'uncertain', value: 3.5 }] }]);
    expect(csv.split('\n')[0]).toBe('channel,unit,observed_at,ingested_at,value,quality,min,max,count');
    expect(csv).toContain('2026-01-01T00:00:00.040Z');
    expect(csv).toContain('uncertain');
  });
});

describe('trust schemas', () => {
  it('maps anything unexpected to unknown, never to pass', () => {
    expect(safeTrustState('pass')).toBe('pass');
    expect(safeTrustState('PASS')).toBe('unknown');
    expect(safeTrustState(undefined)).toBe('unknown');
    expect(safeTrustState({ state: 'pass' })).toBe('unknown');
  });
  it('rejects a malformed trust payload', () => {
    expect(() => validated(twinTrustSchema, { alignment: { state: 'pass' } }, 'twin trust')).toThrow(/did not match/);
  });
});

describe('graph layout', () => {
  it('lays out graphs with self-loops and dangling edges without throwing', async () => {
    const Dagre = (await import('@dagrejs/dagre')).default;
    const { safeLayout } = await import('@/design/graphLayout');
    const g = new Dagre.graphlib.Graph({ multigraph: true }).setDefaultEdgeLabel(() => ({}));
    g.setGraph({ rankdir: 'LR' });
    g.setNode('A', { width: 170, height: 56 });
    g.setNode('B', { width: 170, height: 56 });
    g.setEdge('A', 'A', {}, 'loop');
    g.setEdge('A', 'B', {}, 'ab');
    g.setEdge('B', 'ghost', {}, 'dangling');
    expect(() => safeLayout(g)).not.toThrow();
    const a = g.node('A') as unknown as { x: number };
    const b = g.node('B') as unknown as { x: number };
    expect(Number.isFinite(a.x) && Number.isFinite(b.x)).toBe(true);
    expect(b.x).toBeGreaterThan(a.x);
  });
});

describe('replay frames (docs/runtime-api.md shapes)', () => {
  it('reads label, transition, time and carries state across context records', async () => {
    const { frameLabel, frameTicks, frameTransition, stateAt, propositionsAt } = await import('@/runtime/replay');
    const frames = [
      { seq: 0, kind: 'genesis', hash: 'h0', fields: { time_after: 0, state_after: [{ location: 'READY', clocks: {}, time: 0 }], propositions: ['at(READY)'] } },
      {
        seq: 1, kind: 'step', hash: 'h1',
        fields: {
          input: { name: 'start!' }, time_after: 1500,
          outcome: { branches: [{ transition: 't0', label: 'start!', source: 'READY', target: 'NAVIGATING', guard: [], resets: ['x'] }] },
          state_after: [{ location: 'NAVIGATING', clocks: { x: 0 }, time: 1500 }], propositions: ['at(NAVIGATING)'],
        },
      },
      { seq: 2, kind: 'context', hash: 'h2', fields: { topic: 'map_update', at: 1600, data: {}, time_after: 1600 } },
    ];
    expect(frameLabel(frames[1]!)).toBe('start!');
    expect(frameLabel(frames[2]!)).toBe('context: map_update');
    expect(frameTransition(frames[1]!)?.target).toBe('NAVIGATING');
    expect(frameTransition(frames[2]!)).toBeNull();
    expect(frameTicks(frames[2]!)).toBe(1600);
    expect(stateAt(frames, 2)?.[0]?.location).toBe('NAVIGATING');
    expect(propositionsAt(frames, 2)).toEqual(['at(NAVIGATING)']);
  });
});
