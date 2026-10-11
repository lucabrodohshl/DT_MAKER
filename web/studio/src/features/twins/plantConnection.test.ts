/**
 * Plant connection form helpers: what the New instance dialog and the connection page send.
 */
import { describe, expect, it } from 'vitest';
import type { PlantConnectionView } from '@/api/types';
import { defaultDraft, draftFromConnection, namesFromBindings, requestFromDraft } from './PlantConnection';

const rows = {
  telemetry: [
    { id: 'bearing_temp', label: 'Bearing temperature' },
    { id: 'shaft_speed', label: 'Shaft speed' },
  ],
  events: [{ id: 'start_command', label: 'Start command' }],
};

describe('plant connection drafts', () => {
  it('default to both endpoints on automatic ports and the twin’s own names', () => {
    const body = requestFromDraft(defaultDraft(undefined, namesFromBindings(rows, [])));
    expect(body.rest).toEqual({ enabled: true, port: null });
    expect(body.mqtt).toEqual({ enabled: true, port: null });
    expect((body.broker as { enabled: boolean }).enabled).toBe(false);
    expect(body.names).toEqual({
      telemetry: { bearing_temp: { field: '', topic: '' }, shaft_speed: { field: '', topic: '' } },
      events: { start_command: [] },
    });
  });

  it('carry plant tags and event codes from the Blueprint bindings', () => {
    const names = namesFromBindings(rows, [
      { target: { kind: 'telemetry', id: 'bearing_temp' }, select: { field: 'TT101_degC' } },
      { target: { kind: 'telemetry', id: 'shaft_speed' }, select: { topic: 'plant/+/ST101' } },
      { target: { kind: 'event', id: 'start_command' }, select: { field: 'event', value: 'START' } },
      { target: { kind: 'event', id: 'start_command' }, select: { field: 'event', value: 'RUN' } },
    ]);
    expect(names.telemetry.bearing_temp).toEqual({ field: 'TT101_degC', topic: '' });
    expect(names.telemetry.shaft_speed).toEqual({ field: '', topic: 'plant/+/ST101' });
    expect(names.events.start_command).toBe('START, RUN');
    const body = requestFromDraft({ ...defaultDraft(undefined, names), restPort: '19050', mqtt: false });
    expect(body.rest).toEqual({ enabled: true, port: 19050 });
    expect((body.names as { events: Record<string, string[]> }).events.start_command).toEqual(['START', 'RUN']);
  });

  it('round-trip an existing connection without sending the stored broker password', () => {
    const view = {
      instance: 'p', configured: true, token: 't', hosts: ['10.0.0.2'],
      rest: { enabled: false, port: 19000 }, mqtt: { enabled: true, port: 19001 },
      broker: { enabled: true, host: 'gw', port: 1883, topics: ['a/#', 'b/+'], qos: 2, username: 'u', passwordSet: true },
      timestamp: { field: 'ts', unit: 'iso8601' }, eventField: 'code', clock: { syncSeconds: 5, graceSeconds: 0.5 },
      contract: { telemetry: [{ id: 'bearing_temp', label: 'B', type: 'real', unit: 'degC', field: 'TT101', topic: '' }], events: [{ id: 'start_command', label: 'S', pt: 'start_cmd!', codes: ['START'], signals: [] }] },
      examples: { payload: {} }, runtime: 'running', status: null,
    } satisfies PlantConnectionView;
    const body = requestFromDraft(draftFromConnection(view));
    expect(body.broker).toEqual({ enabled: true, host: 'gw', port: 1883, topics: ['a/#', 'b/+'], qos: 2, username: 'u' });
    expect(body.timestamp).toEqual({ field: 'ts', unit: 'iso8601' });
    expect(body.eventField).toBe('code');
    expect(body.clock).toEqual({ syncSeconds: 5 });
    expect(body.names).toEqual({ telemetry: { bearing_temp: { field: 'TT101', topic: '' } }, events: { start_command: ['START'] } });
  });
});
