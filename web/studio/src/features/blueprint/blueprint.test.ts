/**
 * Pure logic of the Blueprint editors: reference-preserving refactors, timed-automaton edits,
 * world geometry, and the Scenario Builder's mapping of kernel results to steps. Timing itself
 * is the kernel's (backend); these tests check that the UI only reads it.
 */
import { describe, expect, it } from 'vitest';
import type { BlueprintDocument, ScenarioStep, TaModel, TimingResult } from '@/api/types';
import { renameMonitor, renameRequirement, renameTelemetry } from './refactor';
import { addEdge, completeLayout, deleteLocations, emptyLayout, renameClock, renameLocation, updateEdge } from './ta/taEdit';
import { refusalFixes, reasonsOf, runByStep, stepText, timingByStep } from './test/scenario';
import { alignGuides, bboxOf, layerVisibleInView, snap } from './world/geometry';

function doc(): BlueprintDocument {
  return {
    format: 'twin-blueprint/1',
    identity: { name: 'Chamber', domain: 'lab', modelId: 'chamber', timeUnit: 's', ticksPerUnit: 1000, runtimeMode: 'monitor' },
    structure: { root: 'chamber', assetTypes: [], assets: [{ id: 'chamber', name: 'Chamber', type: 'T', scope: 'instance' }], relationships: [] },
    world: { format: 'twin-world/1', mode: 'spatial', unit: 'mm', bounds: { x: 0, y: 0, w: 1000, h: 1000 }, layers: [], objects: [] },
    data: { properties: [], telemetry: [{ id: 'temp', label: 'Temperature', type: 'real', asset: 'chamber' }], events: [], commands: [] },
    connectivity: { sources: [], bindings: [{ id: 'b1', target: { kind: 'telemetry', id: 'temp' }, source: 'sim', select: { field: 'temp' } }] },
    presentation: { displayName: 'Chamber', primaryView: 'status', keyTelemetry: ['temp'], importantAssets: [], importantPropositions: [], importantMonitors: ['hot'], importantPredictions: [], states: {}, events: {}, charts: [{ title: 'T', telemetry: ['temp'] }] },
    behavior: {},
    assurance: {
      requirements: [{ id: 'REQ-1', title: 'Never hot', category: 'safety', severity: 'critical', monitors: ['hot'] }],
      monitors: [
        { id: 'hot', kind: 'property', name: 'Never hot', severity: 'critical', property: 'A[] !HOT', requirement: 'REQ-1' },
        { id: 'fresh', kind: 'data_quality', name: 'Fresh', severity: 'warning', field: 'temp', check: 'stale', maxAgeSeconds: 5 },
      ],
      alerts: [{ id: 'AL-1', monitor: 'hot', on: 'violated', severity: 'critical', message: 'Hot' }],
    },
    simulation: { kind: 'none' },
    scenarios: [{ id: 's', name: 'S', start: { kind: 'initial' }, steps: [{ id: 's1', kind: 'expect', at: '1', expect: { monitor: { id: 'hot', status: 'satisfied' } } }] }],
  };
}

describe('refactors keep every reference', () => {
  it('renames a monitor in requirements, alerts, presentation and scenarios', () => {
    const d = doc();
    const c = renameMonitor(d, 'hot', 'overheat');
    expect(c.assurance!.monitors.map((m) => m.id)).toContain('overheat');
    expect(c.assurance!.requirements[0]!.monitors).toEqual(['overheat']);
    expect(c.assurance!.alerts[0]!.monitor).toBe('overheat');
    expect(c.presentation!.importantMonitors).toEqual(['overheat']);
  });
  it('renames a requirement in the monitors that trace to it', () => {
    const c = renameRequirement(doc(), 'REQ-1', 'REQ-S1');
    expect(c.assurance!.requirements[0]!.id).toBe('REQ-S1');
    expect(c.assurance!.monitors[0]!.requirement).toBe('REQ-S1');
  });
  it('renames a signal in bindings, monitors and charts', () => {
    const c = renameTelemetry(doc(), 'temp', 'air_temp');
    expect(c.data!.telemetry[0]!.id).toBe('air_temp');
    expect(c.connectivity!.bindings[0]!.target.id).toBe('air_temp');
    expect(c.assurance!.monitors.find((m) => m.id === 'fresh')!.field).toBe('air_temp');
    expect(c.presentation!.keyTelemetry).toEqual(['air_temp']);
    expect(c.presentation!.charts[0]!.telemetry).toEqual(['air_temp']);
  });
  it('is a no-op when the name does not change', () => {
    expect(renameMonitor(doc(), 'hot', 'hot')).toEqual({});
  });
});

describe('timed-automaton edits', () => {
  const model: TaModel = {
    format: 'twin-ta/1',
    name: 'M',
    note: '',
    clocks: [{ name: 't', note: '' }],
    constants: [],
    channels: [{ name: 'go', note: '' }],
    locations: [
      { name: 'IDLE', initial: true, invariant: [], note: '' },
      { name: 'RUN', initial: false, invariant: [{ clock: 't', op: '<=', bound: '5' } as never], note: '' },
    ],
    edges: [{ id: 'e1', source: 'IDLE', target: 'RUN', sync: { channel: 'go', direction: '!' }, guard: [], resets: ['t'], note: '' }],
  };
  it('renaming a location updates edges and layout', () => {
    const l = { ...emptyLayout(), locations: { IDLE: { x: 0, y: 0 }, RUN: { x: 100, y: 0 } } };
    const r = renameLocation(model, l, 'RUN', 'RUNNING');
    expect(r.m.edges[0]!.target).toBe('RUNNING');
    expect(r.l.locations.RUNNING).toEqual({ x: 100, y: 0 });
    expect(r.l.locations.RUN).toBeUndefined();
  });
  it('deleting the initial location moves the initial marker and drops its edges', () => {
    const r = deleteLocations(model, emptyLayout(), ['IDLE']);
    expect(r.m.locations).toHaveLength(1);
    expect(r.m.locations[0]!.initial).toBe(true);
    expect(r.m.edges).toHaveLength(0);
  });
  it('declares new channels and prunes unused ones', () => {
    const a = addEdge(model, 'RUN', 'IDLE', { channel: 'stop', direction: '!' });
    expect(a.m.channels.map((c) => c.name).sort()).toEqual(['go', 'stop']);
    const b = updateEdge(a.m, 'e1', { sync: null });
    expect(b.channels.map((c) => c.name)).toEqual(['stop']);
  });
  it('renaming a clock updates invariants, guards and resets', () => {
    const r = renameClock(model, 't', 'x');
    expect(r.locations[1]!.invariant[0]!.clock).toBe('x');
    expect(r.edges[0]!.resets).toEqual(['x']);
  });
  it('lays out every location, keeping stored positions', () => {
    const l = completeLayout(model, { ...emptyLayout(), locations: { IDLE: { x: 10.4, y: 20.6 } } });
    expect(l.locations.IDLE).toEqual({ x: 10, y: 21 });
    expect(l.locations.RUN).toBeDefined();
  });
});

describe('world geometry', () => {
  it('snaps to the grid only when snapping is on', () => {
    expect(snap(1240, 500, true)).toBe(1000);
    expect(snap(1240.6, 500, false)).toBe(1241);
  });
  it('aligns a moving box to the nearest edge within tolerance', () => {
    const r = alignGuides({ x: 103, y: 0, w: 50, h: 50 }, [{ x: 100, y: 200, w: 40, h: 40 }], 5);
    expect(r.dx).toBe(-3);
    expect(r.guides).toContainEqual({ axis: 'x', at: 100 });
  });
  it('boxes points, lines and rectangles', () => {
    expect(bboxOf({ id: 'a', layer: 'l', kind: 'rect', semanticType: 'wall', name: '', geometry: { x: 1, y: 2, w: 3, h: 4 }, properties: {}, tags: [] })).toEqual({ x: 1, y: 2, w: 3, h: 4 });
    const line = bboxOf({ id: 'b', layer: 'l', kind: 'polyline', semanticType: 'wall', name: '', geometry: { points: [[0, 0], [10, 5]], width: 2 }, properties: {}, tags: [] });
    expect(line!.w).toBeGreaterThanOrEqual(10);
  });
  it('separates ground truth from twin knowledge', () => {
    expect(layerVisibleInView('ground-truth', 'knowledge')).toBe(false);
    expect(layerVisibleInView('knowledge', 'truth')).toBe(false);
    expect(layerVisibleInView('shared', 'truth')).toBe(true);
  });
});

describe('scenario builder reads kernel results', () => {
  const steps: ScenarioStep[] = [
    { id: 's1', kind: 'event', label: 'start!', at: '5' },
    { id: 's2', kind: 'world', at: '9', change: { action: 'set_property', objectId: 'door', property: 'open', value: true }, observation: { event: 'door_opened', at: '9' } },
    { id: 's3', kind: 'expect', at: '10', expect: { window: { label: 'stop!', status: 'later', earliest: '30' } } },
  ];
  it('describes steps in words', () => {
    expect(stepText(steps[0]!)).toBe('start!');
    expect(stepText(steps[1]!)).toContain('observed as door_opened');
    expect(stepText(steps[2]!)).toBe('stop! is available later [+30, ∞]');
    expect(stepText({ id: 'x', kind: 'event', label: 'reset!', at: '1', expectRefused: true })).toContain('must be refused');
  });
  it('maps formal steps back to their scenario steps (observations fold into the world step)', () => {
    const timing = { steps: [{ index: 0, status: 'ok' }, { index: 1, status: 'invalid' }, { index: 2, status: 'not_evaluated' }], origins: ['s1', 's2/observed', 's3'] } as unknown as TimingResult;
    const m = timingByStep(timing);
    expect(m.get('s2')!.status).toBe('invalid');
    expect(m.get('s3')!.status).toBe('not_evaluated');
    const runs = runByStep([{ index: 0, id: 's2', kind: 'world', generated: false, status: 'ok' }, { index: 1, id: 's2/observed', kind: 'event', generated: true, status: 'invalid' }]);
    expect(runs.get('s2')).toHaveLength(2);
  });
  it('offers only the legal times the kernel reported for a refused event', () => {
    const explanation = {
      alternatives: [{ window: { earliest_at: { text: '130', ticks: 130000 }, latest_at: { text: '400', ticks: 400000 } } }],
      reasons: [{ transition: 'A.reset!.B', target: 'B', reason: 'too early: the earliest permitted time is t = 130 (+30)' }],
    };
    expect(refusalFixes(explanation, 110000)).toEqual([{ kind: 'earliest', at: '130' }]);
    expect(refusalFixes(explanation, 500000)).toEqual([{ kind: 'latest', at: '400' }]);
    expect(refusalFixes({ alternatives: [{ window: null }] }, 110000)).toEqual([]);
    expect(reasonsOf(explanation)[0]).toContain('too early');
  });
});
