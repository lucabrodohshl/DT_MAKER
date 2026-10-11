/**
 * Plant connection of an instance: how the real system sends its data to the twin.
 *
 * An instance can accept plant messages on its own REST endpoint and MQTT endpoint, and can
 * subscribe to the plant's existing MQTT broker. Its twin-ingest process (started with the
 * deployment) maps each message to the data contract and forwards it to the twin's runtime,
 * whose kernel accepts or rejects every event. This page shows everything a plant integrator
 * needs (addresses, ports, access token, topics, the message contract, ready-to-run commands)
 * and the live status of the connection. Everything shown comes from the backend
 * (GET /instances/{id}/connection); nothing here interprets the data.
 *
 * ConnectionForm and the draft helpers are shared with the New instance dialog.
 */
import { Cable, Check, Copy, Eye, EyeOff, KeyRound, Pencil, Plug, RefreshCw } from 'lucide-react';
import { useState, type ReactNode } from 'react';
import { blueprintApi, useBlueprintMutation, useConnection } from '@/api/blueprints';
import type { IngestStatus, PlantConnectionView } from '@/api/types';
import { Button, Callout, Dialog, EmptyState, ErrorBlock, KeyValue, PageHeader, Panel, QueryState, StatusBadge, TimeStamp } from '@/design';
import { useTwinScope } from '@/app/twinScope';

// ------------------------------------------------------------------ settings form (shared)

/** Editable plant connection settings (empty port = chosen automatically). */
export interface ConnectionDraft {
  rest: boolean;
  restPort: string;
  mqtt: boolean;
  mqttPort: string;
  broker: boolean;
  brokerHost: string;
  brokerPort: string;
  brokerTopics: string;
  brokerUser: string;
  /** New password ("" keeps the stored one). */
  brokerPassword: string;
  brokerQos: string;
  timestampField: string;
  timestampUnit: 'ms' | 's' | 'iso8601';
  eventField: string;
  syncSeconds: string;
  /** The plant's own names: per signal a field (or JSON path) or an MQTT topic; per event its codes. */
  names: PlantNames;
}

/** Signals and events whose plant names can be set (from the data contract). */
export interface ContractRows {
  telemetry: { id: string; label: string }[];
  events: { id: string; label: string }[];
}

/** Plant names per signal ({field, topic}) and per event (comma-separated codes). */
export interface PlantNames {
  telemetry: Record<string, { field: string; topic: string }>;
  events: Record<string, string>;
}

/** Plant names as the Blueprint's bindings define them (empty = the twin's own name). */
export function namesFromBindings(rows: ContractRows, bindings: { target: { kind: string; id: string }; select: { field?: string; path?: string; topic?: string; value?: string } }[], eventField = 'event'): PlantNames {
  const names: PlantNames = { telemetry: {}, events: {} };
  for (const t of rows.telemetry) {
    const b = bindings.find((x) => x.target.kind === 'telemetry' && x.target.id === t.id);
    const field = b?.select.field ?? b?.select.path ?? '';
    names.telemetry[t.id] = { field: field === t.id ? '' : field, topic: b?.select.topic ?? '' };
  }
  for (const e of rows.events) {
    names.events[e.id] = bindings
      .filter((x) => x.target.kind === 'event' && x.target.id === e.id && x.select.field === eventField && x.select.value)
      .map((x) => x.select.value!)
      .join(', ');
  }
  return names;
}

/** Defaults of a new instance: both endpoints on automatic ports, arrival time. */
export function defaultDraft(broker?: { host?: string; port?: number; topic?: string }, names: PlantNames = { telemetry: {}, events: {} }): ConnectionDraft {
  return {
    rest: true,
    restPort: '',
    mqtt: true,
    mqttPort: '',
    broker: false,
    brokerHost: broker?.host ?? '',
    brokerPort: broker?.port ? String(broker.port) : '1883',
    brokerTopics: broker?.topic ?? '',
    brokerUser: '',
    brokerPassword: '',
    brokerQos: '1',
    timestampField: '',
    timestampUnit: 'ms',
    eventField: 'event',
    syncSeconds: '0',
    names,
  };
}

/** The draft of an existing connection. */
export function draftFromConnection(c: PlantConnectionView): ConnectionDraft {
  return {
    rest: c.rest.enabled,
    restPort: c.rest.port ? String(c.rest.port) : '',
    mqtt: c.mqtt.enabled,
    mqttPort: c.mqtt.port ? String(c.mqtt.port) : '',
    broker: c.broker.enabled,
    brokerHost: c.broker.host ?? '',
    brokerPort: String(c.broker.port ?? 1883),
    brokerTopics: (c.broker.topics ?? []).join(', '),
    brokerUser: c.broker.username ?? '',
    brokerPassword: '',
    brokerQos: String(c.broker.qos ?? 1),
    timestampField: c.timestamp.field,
    timestampUnit: c.timestamp.unit,
    eventField: c.eventField,
    syncSeconds: String(c.clock.syncSeconds ?? 0),
    names: {
      telemetry: Object.fromEntries(c.contract.telemetry.map((t) => [t.id, { field: t.field === t.id ? '' : t.field, topic: t.topic }])),
      events: Object.fromEntries(c.contract.events.map((e) => [e.id, e.codes.join(', ')])),
    },
  };
}

/** Rows of the plant-name table of an existing connection. */
export function rowsOf(c: PlantConnectionView): ContractRows {
  return { telemetry: c.contract.telemetry.map((t) => ({ id: t.id, label: t.label })), events: c.contract.events.map((e) => ({ id: e.id, label: e.label })) };
}

const portOrAuto = (s: string): number | null => (s.trim() === '' ? null : Number(s.trim()));

/** The request body of a draft (POST /instances "connection", PUT /instances/{id}/connection). */
export function requestFromDraft(d: ConnectionDraft): Record<string, unknown> {
  return {
    rest: { enabled: d.rest, port: portOrAuto(d.restPort) },
    mqtt: { enabled: d.mqtt, port: portOrAuto(d.mqttPort) },
    broker: {
      enabled: d.broker,
      host: d.brokerHost.trim(),
      port: Number(d.brokerPort) || 1883,
      topics: d.brokerTopics.split(',').map((t) => t.trim()).filter(Boolean),
      qos: Number(d.brokerQos),
      username: d.brokerUser.trim(),
      ...(d.brokerPassword ? { password: d.brokerPassword } : {}),
    },
    timestamp: { field: d.timestampField.trim(), unit: d.timestampUnit },
    eventField: d.eventField.trim() || 'event',
    clock: { syncSeconds: Number(d.syncSeconds) || 0 },
    names: {
      telemetry: Object.fromEntries(Object.entries(d.names.telemetry).map(([id, n]) => [id, { field: n.field.trim(), topic: n.topic.trim() }])),
      events: Object.fromEntries(Object.entries(d.names.events).map(([id, codes]) => [id, codes.split(',').map((x) => x.trim()).filter(Boolean)])),
    },
  };
}

/** The plant's own names for the contract's signals and events. */
function PlantNamesEditor({ rows, value, eventField, onChange }: { rows: ContractRows; value: PlantNames; eventField: string; onChange: (n: PlantNames) => void }) {
  const tel = (id: string) => value.telemetry[id] ?? { field: '', topic: '' };
  return (
    <details>
      <summary className="small" style={{ cursor: 'pointer' }}><strong>Plant names</strong> (optional) — the tag names and event codes your plant uses</summary>
      <div className="stack-sm" style={{ marginTop: 8 }}>
        <p className="xsmall subtle" style={{ margin: 0 }}>Empty: the twin&apos;s own name. The twin&apos;s own names are always accepted as well.</p>
        <table className="vts-table">
          <thead><tr><th scope="col">Signal</th><th scope="col">Plant field (or JSON path $.a.b)</th><th scope="col">or MQTT topic</th></tr></thead>
          <tbody>
            {rows.telemetry.map((t) => (
              <tr key={t.id}>
                <td className="small">{t.label} <span className="mono xsmall subtle">{t.id}</span></td>
                <td><input className="vts-input mono" value={tel(t.id).field} placeholder={t.id} aria-label={`Plant field of ${t.id}`} onChange={(x) => onChange({ ...value, telemetry: { ...value.telemetry, [t.id]: { ...tel(t.id), field: x.target.value } } })} /></td>
                <td><input className="vts-input mono" value={tel(t.id).topic} placeholder="plant/line4/pump/TT101" aria-label={`MQTT topic of ${t.id}`} onChange={(x) => onChange({ ...value, telemetry: { ...value.telemetry, [t.id]: { ...tel(t.id), topic: x.target.value } } })} /></td>
              </tr>
            ))}
          </tbody>
        </table>
        <table className="vts-table">
          <thead><tr><th scope="col">Event</th><th scope="col">Plant codes in the field <span className="mono">{eventField || 'event'}</span> (comma-separated)</th></tr></thead>
          <tbody>
            {rows.events.map((e) => (
              <tr key={e.id}>
                <td className="small">{e.label} <span className="mono xsmall subtle">{e.id}</span></td>
                <td><input className="vts-input mono" value={value.events[e.id] ?? ''} placeholder="START" aria-label={`Plant codes of ${e.id}`} onChange={(x) => onChange({ ...value, events: { ...value.events, [e.id]: x.target.value } })} /></td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>
    </details>
  );
}

/** Inputs of a plant connection (with the plant-name table when @p rows is given). */
export function ConnectionForm({ value, onChange, passwordSet, rows }: { value: ConnectionDraft; onChange: (d: ConnectionDraft) => void; passwordSet?: boolean; rows?: ContractRows }) {
  const set = (patch: Partial<ConnectionDraft>) => onChange({ ...value, ...patch });
  const port = (label: string, v: string, k: 'restPort' | 'mqttPort' | 'brokerPort', auto = true) => (
    <label className="vts-f">
      <span>{label}</span>
      <input className="vts-input mono" inputMode="numeric" value={v} placeholder={auto ? 'automatic' : '1883'} onChange={(x) => set({ [k]: x.target.value.replace(/[^0-9]/g, '') })} />
    </label>
  );
  return (
    <div className="stack">
      <div className="vts-fgrid">
        <div className="vts-f">
          <label className="vts-check small">
            <input type="checkbox" checked={value.rest} onChange={(x) => set({ rest: x.target.checked })} /> <strong>REST endpoint</strong>
          </label>
          <span className="vts-f__hint">The plant sends HTTP POST requests to the twin.</span>
          {value.rest && port('REST port', value.restPort, 'restPort')}
        </div>
        <div className="vts-f">
          <label className="vts-check small">
            <input type="checkbox" checked={value.mqtt} onChange={(x) => set({ mqtt: x.target.checked })} /> <strong>MQTT endpoint</strong>
          </label>
          <span className="vts-f__hint">The plant publishes to the twin&apos;s own MQTT endpoint.</span>
          {value.mqtt && port('MQTT port', value.mqttPort, 'mqttPort')}
        </div>
      </div>
      <div className="vts-f">
        <label className="vts-check small">
          <input type="checkbox" checked={value.broker} onChange={(x) => set({ broker: x.target.checked })} /> <strong>Subscribe to the plant&apos;s MQTT broker</strong>
        </label>
        <span className="vts-f__hint">The twin connects to a broker you already run.</span>
      </div>
      {value.broker && (
        <div className="vts-fgrid">
          <label className="vts-f">
            <span>Broker host</span>
            <input className="vts-input mono" value={value.brokerHost} placeholder="broker.plant.local" onChange={(x) => set({ brokerHost: x.target.value })} />
          </label>
          {port('Broker port', value.brokerPort, 'brokerPort', false)}
          <label className="vts-f">
            <span>Topics</span>
            <input className="vts-input mono" value={value.brokerTopics} placeholder="plant/line4/pump/#" onChange={(x) => set({ brokerTopics: x.target.value })} />
            <span className="vts-f__hint">Comma-separated; + and # wildcards allowed.</span>
          </label>
          <label className="vts-f">
            <span>QoS</span>
            <select className="vts-select" value={value.brokerQos} onChange={(x) => set({ brokerQos: x.target.value })}>
              <option value="0">0 — at most once</option>
              <option value="1">1 — at least once</option>
              <option value="2">2 — exactly once</option>
            </select>
          </label>
          <label className="vts-f">
            <span>User name (optional)</span>
            <input className="vts-input" value={value.brokerUser} autoComplete="off" onChange={(x) => set({ brokerUser: x.target.value })} />
          </label>
          <label className="vts-f">
            <span>Password (optional)</span>
            <input className="vts-input" type="password" autoComplete="new-password" value={value.brokerPassword} placeholder={passwordSet ? 'stored — type to replace' : ''} onChange={(x) => set({ brokerPassword: x.target.value })} />
            <span className="vts-f__hint">Kept by Studio on this machine, outside its database.</span>
          </label>
        </div>
      )}
      <div className="vts-fgrid">
        <label className="vts-f">
          <span>Timestamp field (optional)</span>
          <input className="vts-input mono" value={value.timestampField} placeholder="ts" onChange={(x) => set({ timestampField: x.target.value })} />
          <span className="vts-f__hint">Empty: the arrival time is used.</span>
        </label>
        <label className="vts-f">
          <span>Timestamp unit</span>
          <select className="vts-select" value={value.timestampUnit} onChange={(x) => set({ timestampUnit: x.target.value as ConnectionDraft['timestampUnit'] })}>
            <option value="ms">epoch milliseconds</option>
            <option value="s">epoch seconds</option>
            <option value="iso8601">ISO 8601 text</option>
          </select>
        </label>
        <label className="vts-f">
          <span>Event field</span>
          <input className="vts-input mono" value={value.eventField} onChange={(x) => set({ eventField: x.target.value })} />
          <span className="vts-f__hint">The field that carries event codes.</span>
        </label>
        <label className="vts-f">
          <span>Advance the twin&apos;s clock every</span>
          <input className="vts-input mono" inputMode="decimal" value={value.syncSeconds} onChange={(x) => set({ syncSeconds: x.target.value.replace(/[^0-9.]/g, '') })} />
          <span className="vts-f__hint">Seconds without data before the twin&apos;s time follows the plant (0: only at deadlines). Each advance is a ledger record.</span>
        </label>
      </div>
      {rows && <PlantNamesEditor rows={rows} value={value.names} eventField={value.eventField} onChange={(names) => set({ names })} />}
    </div>
  );
}

// ------------------------------------------------------------------ page

function CopyButton({ text, label = 'Copy' }: { text: string; label?: string }) {
  const [done, setDone] = useState(false);
  return (
    <Button
      size="sm"
      variant="ghost"
      iconOnly
      icon={done ? <Check size={13} /> : <Copy size={13} />}
      onClick={() => {
        void navigator.clipboard?.writeText(text).then(() => {
          setDone(true);
          window.setTimeout(() => setDone(false), 1500);
        });
      }}
    >
      {label}
    </Button>
  );
}

function Copyable({ value, secret }: { value: string; secret?: boolean }) {
  const [shown, setShown] = useState(!secret);
  return (
    <span className="row" style={{ gap: 4, flexWrap: 'nowrap' }}>
      <code className="vts-formula">{shown ? value : '•'.repeat(24)}</code>
      {secret && (
        <Button size="sm" variant="ghost" iconOnly icon={shown ? <EyeOff size={13} /> : <Eye size={13} />} onClick={() => setShown(!shown)}>
          {shown ? 'Hide' : 'Show'}
        </Button>
      )}
      <CopyButton text={value} />
    </span>
  );
}

function Command({ title, text }: { title: string; text: string }) {
  return (
    <div className="stack-sm">
      <div className="row" style={{ justifyContent: 'space-between' }}>
        <span className="small"><strong>{title}</strong></span>
        <CopyButton text={text} label={`Copy: ${title}`} />
      </div>
      <pre className="vts-code" style={{ whiteSpace: 'pre-wrap', overflowWrap: 'anywhere' }}>{text}</pre>
    </div>
  );
}

/** Overall state of the connection, from what the backend reports. */
function connectionState(c: PlantConnectionView): { tone: 'ok' | 'info' | 'neutral' | 'warning' | 'critical'; label: string } {
  if (!c.configured) return { tone: 'neutral', label: 'NOT CONFIGURED' };
  if (c.runtime !== 'running') return { tone: 'neutral', label: 'NOT DEPLOYED' };
  if (!c.status) return { tone: 'critical', label: 'CONNECTOR NOT RUNNING' };
  if (c.status.missedDeadline) return { tone: 'warning', label: 'DEADLINE MISSED' };
  if (c.status.lastMessage) return { tone: 'ok', label: 'RECEIVING DATA' };
  return { tone: 'info', label: 'WAITING FOR DATA' };
}

function StatusPanel({ s }: { s: IngestStatus }) {
  const k = s.counters;
  return (
    <Panel title="Live status" subtitle={`Connector running since ${new Date(s.startedAt).toLocaleString()}`}>
      <div className="stack">
        <KeyValue
          compact
          items={[
            ['Messages received', String(k.messages)],
            ['Telemetry samples forwarded', String(k.telemetrySamples)],
            ['Events accepted / rejected by the twin', `${k.eventsAccepted} / ${k.eventsRejected}`],
            ['Not delivered (runtime unreachable)', String(k.notDelivered)],
            ['Unknown event codes', String(k.unmappedCodes)],
            ['Values of the wrong type', String(k.problems)],
            ['Last message', s.lastMessage ? <span key="lm"><TimeStamp value={s.lastMessage.at} relative /> · {s.lastMessage.transport} · <span className="mono">{s.lastMessage.client}</span>{s.lastMessage.topic ? <> · <span className="mono">{s.lastMessage.topic}</span></> : null}</span> : 'none yet'],
            ['Last event', s.lastEvent ? <span key="le"><span className="mono">{s.lastEvent.pt}</span> at t = {s.lastEvent.at.text} — {s.lastEvent.accepted ? <StatusBadge tone="ok" label={`ACCEPTED ${s.lastEvent.from ?? ''} → ${s.lastEvent.to ?? ''}`} /> : <StatusBadge tone="critical" label="REJECTED" />}</span> : 'none yet'],
            ['Twin time', s.runtime.time ? `t = ${s.runtime.time.text}${s.runtime.deadline ? ` · deadline t = ${s.runtime.deadline.text}` : ''}` : '—'],
            ['Timestamps', s.clock.timestamps],
          ]}
        />
        {s.mqtt && (
          <div className="small">
            <strong>MQTT clients connected:</strong> {s.mqtt.clients.length === 0 ? 'none' : s.mqtt.clients.map((c) => `${c.clientId} (${c.address}, ${c.messages} messages)`).join(', ')}
            {s.mqtt.refused > 0 && <span className="subtle"> · {s.mqtt.refused} connection(s) refused (wrong token)</span>}
          </div>
        )}
        {s.rest && s.rest.unauthorized > 0 && <div className="small subtle">REST: {s.rest.unauthorized} request(s) refused (missing or wrong token).</div>}
        {s.broker && (
          <div className="small">
            <strong>Broker {s.broker.broker}:</strong> {s.broker.state}
            {s.broker.lastError && s.broker.state !== 'connected' && <span style={{ color: 'var(--crit)' }}> — {s.broker.lastError}</span>}
          </div>
        )}
        {s.missedDeadline && (
          <Callout tone="warning" title={`Missed deadline at t = ${s.missedDeadline.deadline?.text ?? '?'}`}>
            The plant did not act within the time the verified model allows. The twin recorded the violation (t = {s.missedDeadline.at.text}); its time no longer advances by itself until the plant sends an event the model accepts. {s.missedDeadline.message}
          </Callout>
        )}
        {s.recent.length > 0 && (
          <div>
            <div className="small"><strong>Recent notices</strong></div>
            <ul className="small" style={{ margin: '4px 0 0', paddingLeft: 18 }}>
              {s.recent.slice(0, 8).map((r, i) => (
                <li key={`${r.at}-${i}`}>
                  <span className="subtle"><TimeStamp value={r.at} relative /></span> <StatusBadge tone={r.kind === 'rejected' || r.kind === 'error' || r.kind === 'deadline' ? 'critical' : r.kind === 'info' ? 'neutral' : 'warning'} label={r.kind} /> {r.text}
                </li>
              ))}
            </ul>
          </div>
        )}
      </div>
    </Panel>
  );
}

function ContractPanel({ c }: { c: PlantConnectionView }) {
  return (
    <Panel title="Message contract" subtitle="What the twin understands. Its own names always work; plant names come from the bindings.">
      <div className="stack">
        <table className="vts-table">
          <caption className="small" style={{ textAlign: 'left' }}>Telemetry: one field per signal (or one MQTT topic per signal)</caption>
          <thead><tr><th scope="col">Signal</th><th scope="col">Plant field</th><th scope="col">Type</th><th scope="col">Unit</th></tr></thead>
          <tbody>
            {c.contract.telemetry.map((t) => (
              <tr key={t.id}>
                <td className="small">{t.label} <span className="mono xsmall subtle">{t.id}</span></td>
                <td className="mono small">{t.topic ? `topic ${t.topic}` : t.field || t.id}{t.field && t.field !== t.id && !t.topic ? <span className="subtle"> (or {t.id})</span> : null}</td>
                <td className="small">{t.type}</td>
                <td className="small">{t.unit || '—'}</td>
              </tr>
            ))}
          </tbody>
        </table>
        <table className="vts-table">
          <caption className="small" style={{ textAlign: 'left' }}>Events: a value of the field <span className="mono">{c.eventField}</span> (several: a JSON array)</caption>
          <thead><tr><th scope="col">Event</th><th scope="col">Codes accepted</th><th scope="col">PT label</th></tr></thead>
          <tbody>
            {c.contract.events.map((e) => (
              <tr key={e.id}>
                <td className="small">{e.label}</td>
                <td className="mono small">{[...e.codes, e.id, e.pt].join(' · ')}{e.signals.length > 0 && <span className="subtle"> · or {e.signals.map((s) => s.topic ? `topic ${s.topic}` : `field ${s.field ?? s.path} = true`).join(', ')}</span>}</td>
                <td className="mono small">{e.pt}</td>
              </tr>
            ))}
          </tbody>
        </table>
        <p className="xsmall subtle" style={{ margin: 0 }}>
          Time: {c.timestamp.field ? <>the field <span className="mono">{c.timestamp.field}</span> ({c.timestamp.unit === 'iso8601' ? 'ISO 8601 text' : `epoch ${c.timestamp.unit === 'ms' ? 'milliseconds' : 'seconds'}`}); messages keep their spacing and logical time never goes back.</> : 'the arrival time of each message.'} A code that matches no event is reported and never sent. The twin&apos;s kernel accepts or rejects every event; rejections are recorded in the ledger.
        </p>
      </div>
    </Panel>
  );
}

function EditDialog({ c, open, onOpenChange }: { c: PlantConnectionView; open: boolean; onOpenChange: (o: boolean) => void }) {
  const [draft, setDraft] = useState<ConnectionDraft>(() => draftFromConnection(c));
  const save = useBlueprintMutation((body: Record<string, unknown>) => blueprintApi.updateConnection(c.instance, body));
  return (
    <Dialog
      open={open}
      onOpenChange={(o) => {
        if (o) setDraft(draftFromConnection(c));
        save.reset();
        onOpenChange(o);
      }}
      wide
      title="Plant connection"
      description="Choose how the plant sends data to this twin. A running twin applies the change at once; its runtime keeps running."
      footer={
        <>
          <Button onClick={() => onOpenChange(false)}>Cancel</Button>
          <Button variant="primary" loading={save.isPending} onClick={() => save.mutate(requestFromDraft(draft), { onSuccess: () => onOpenChange(false) })}>
            Save
          </Button>
        </>
      }
    >
      <div className="stack">
        <ConnectionForm value={draft} onChange={setDraft} passwordSet={c.broker.passwordSet} rows={rowsOf(c)} />
        {save.isError && <ErrorBlock error={save.error} compact />}
      </div>
    </Dialog>
  );
}

function Endpoints({ c, header }: { c: PlantConnectionView; header?: ReactNode }) {
  const token = c.token ?? '';
  return (
    <div className="stack">
      {header}
      <div className="grid-2">
        {c.rest.enabled && c.rest.url && (
          <Panel title="REST endpoint" subtitle="HTTP POST of a JSON object, a JSON array, or JSON lines">
            <KeyValue
              compact
              items={[
                ['URL', <Copyable key="u" value={c.rest.url} />],
                ['Header', <Copyable key="h" value={`Authorization: Bearer ${token}`} secret />],
                ['Other addresses', <span key="o" className="mono xsmall">{(c.rest.urls ?? []).slice(1).join('  ·  ') || '—'}</span>],
              ]}
            />
          </Panel>
        )}
        {c.mqtt.enabled && c.mqtt.host && (
          <Panel title="MQTT endpoint" subtitle="MQTT 3.1.1, QoS 0, 1 or 2, publish to any topic">
            <KeyValue
              compact
              items={[
                ['Host', <Copyable key="h" value={c.mqtt.host} />],
                ['Port', <Copyable key="p" value={String(c.mqtt.port)} />],
                ['User name', <Copyable key="u" value={c.mqtt.username ?? c.instance} />],
                ['Password', <Copyable key="pw" value={token} secret />],
                ['Topic', <Copyable key="t" value={c.mqtt.topic ?? ''} />],
                ['Other hosts', <span key="o" className="mono xsmall">{c.hosts.slice(1).join('  ·  ')}</span>],
              ]}
            />
          </Panel>
        )}
        {c.broker.enabled && (
          <Panel title="Broker subscription" subtitle="The twin connects to the plant's broker">
            <KeyValue
              compact
              items={[
                ['Broker', <span key="b" className="mono">{c.broker.host}:{c.broker.port}</span>],
                ['Topics', <span key="t" className="mono">{(c.broker.topics ?? []).join(', ')}</span>],
                ['QoS', String(c.broker.qos ?? 1)],
                ['Client id', <span key="c" className="mono">{c.broker.clientId}</span>],
                ['Credentials', c.broker.username ? `${c.broker.username}${c.broker.passwordSet ? ' (password stored)' : ''}` : 'none'],
              ]}
            />
          </Panel>
        )}
        <Panel title="Access token" subtitle="REST bearer token and MQTT password">
          <div className="stack-sm">
            <Copyable value={token} secret />
            <p className="xsmall subtle" style={{ margin: 0 }}>Anyone with the token can send data to this twin. The endpoints listen on every network interface of this machine; there is no TLS in this version, so keep them on a trusted plant network.</p>
          </div>
        </Panel>
      </div>
    </div>
  );
}

/** Operate → Plant connection. */
export function TwinConnectionPage() {
  const scope = useTwinScope();
  const twinId = scope?.twin.id;
  const q = useConnection(twinId);
  const [editing, setEditing] = useState(false);
  const rotate = useBlueprintMutation(() => blueprintApi.rotateConnectionToken(twinId!));
  return (
    <div className="vts-page stack">
      <QueryState query={q}>
        {(c) => {
          const st = connectionState(c);
          return (
            <>
              <PageHeader
                title="Plant connection"
                meta={<span>How {scope?.twin.name ?? c.instance} receives the real system&apos;s data</span>}
                actions={
                  <span className="row">
                    <StatusBadge tone={st.tone} icon={Plug} label={st.label} />
                    <Button icon={<Pencil size={14} />} onClick={() => setEditing(true)}>{c.configured ? 'Edit connection' : 'Set up'}</Button>
                    {c.configured && (
                      <Button icon={<KeyRound size={14} />} loading={rotate.isPending} onClick={() => rotate.mutate(undefined)}>
                        New token
                      </Button>
                    )}
                  </span>
                }
              />
              {rotate.isError && <ErrorBlock error={rotate.error} compact />}
              {!c.configured ? (
                <EmptyState icon={<Cable size={28} />} title="No plant connection" action={<Button variant="primary" onClick={() => setEditing(true)}>Set up a plant connection</Button>}>
                  This twin receives no data from the plant yet. Choose a REST endpoint, an MQTT endpoint or a subscription to the plant&apos;s broker: Studio starts it with the twin and shows here everything needed to connect.
                </EmptyState>
              ) : (
                <>
                  {c.runtime !== 'running' && (
                    <Callout tone="info" title="Not deployed">
                      The connection starts with the twin. Deploy the instance (Studio → its Blueprint → Release → Instances) and the endpoints below open automatically.
                    </Callout>
                  )}
                  {c.runtime === 'running' && !c.status && (
                    <Callout tone="critical" title="The connector is not running">
                      The twin runs, but its plant connection did not answer. Studio restarts it automatically; see the instance&apos;s logs if this persists.
                    </Callout>
                  )}
                  <Endpoints c={c} />
                  <Panel title="Try it" subtitle="Ready-to-run commands with this twin's address and token">
                    <div className="stack">
                      {c.examples.curl && <Command title="Send one message (REST)" text={c.examples.curl} />}
                      {c.examples.mosquittoPub && <Command title="Publish one message (MQTT, mosquitto_pub)" text={c.examples.mosquittoPub} />}
                      {c.examples.curlBatch && <Command title="Replay a recorded log, one JSON object per line (REST)" text={c.examples.curlBatch} />}
                    </div>
                  </Panel>
                  {c.status && <StatusPanel s={c.status} />}
                  <ContractPanel c={c} />
                </>
              )}
              {editing && <EditDialog c={c} open={editing} onOpenChange={setEditing} />}
            </>
          );
        }}
      </QueryState>
      <p className="xsmall subtle" style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
        <RefreshCw size={12} aria-hidden="true" /> Refreshed every 3 seconds. Logical time only: no real-time guarantee is implied.
      </p>
    </div>
  );
}
