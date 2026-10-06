/**
 * Test → Preview / Simulation: run this version for real — the twin runtime on its verified core
 * (the version's package, or a sandbox build of the pinned artefacts that is never recorded) with
 * its simulator — in an isolated STUDIO PREVIEW: no twin record, no deployment, no stored
 * telemetry. Shows the kernel's live state, lets the engineer send plant / twin events or play a
 * scenario into it, and hosts the domain view (e.g. the drone mission map) when the Blueprint
 * names one. Nothing here decides behaviour: every state and refusal is the runtime's.
 */
import { useQueryClient } from '@tanstack/react-query';
import { FlaskConical, MonitorPlay, Play, Power, RotateCcw, Send, Square } from 'lucide-react';
import { useMemo, useState } from 'react';
import { Link, useSearchParams } from 'react-router-dom';
import { blueprintApi, blueprintRoute, bpKeys, usePreview } from '@/api/blueprints';
import { ApiError } from '@/api/client';
import type { PreviewView, TwinDetail } from '@/api/types';
import { Button, Callout, ErrorBlock, HashChip, KeyValue, Segmented, Skeleton, StatusBadge } from '@/design';
import { PluginHost } from '@/features/assets/AssetPluginTab';
import { pluginsFor } from '@/plugins/registry';
import { runtimeApi, runtimeKeys, useRuntimeState, useRuntimeStream } from '@/runtime/client';
import type { RuntimeStreamEvent } from '@/runtime/types';
import { useEditor } from '../editor';
import { useModelFacts } from '../formalFacts';
import { EdPage, Pane } from '../ui';

const STATE_TONE: Record<string, 'ok' | 'info' | 'neutral' | 'critical'> = { running: 'ok', starting: 'info', not_started: 'neutral', stopped: 'neutral', failed: 'critical' };

function Banner({ name, version }: { name: string; version: number }) {
  return (
    <div className="vts-preview-banner" role="status">
      <MonitorPlay size={18} aria-hidden="true" />
      <span>STUDIO PREVIEW</span>
      <span style={{ fontWeight: 400, letterSpacing: 0 }} className="small">
        {name} v{version} in an isolated sandbox — no twin record, no deployment, no stored telemetry. Not an operational twin.
      </span>
    </div>
  );
}

function summarize(e: RuntimeStreamEvent): string {
  const d = (e.data ?? {}) as Record<string, unknown>;
  if (e.topic === 'pt_event') return `plant ${String(d.pt_label ?? '?')} → twin ${String(d.dt_label || '— (not translated)')}${d.note ? ` (${String(d.note)})` : ''}`;
  if (e.topic === 'observation') {
    const input = (d.input ?? {}) as { name?: string };
    const outcome = (d.outcome ?? {}) as { branches?: { source: string; target: string }[]; error?: { message: string } };
    const b = outcome.branches?.[0];
    return `${d.accepted === false ? 'REFUSED' : 'accepted'} ${input.name ?? String(d.label ?? '')}${b ? `: ${b.source} → ${b.target}` : ''}${outcome.error ? ` — ${outcome.error.message}` : ''}`;
  }
  if (e.topic === 'alarm') return `alarm: ${String(d.message ?? d.kind ?? JSON.stringify(d))}`;
  if (e.topic === 'telemetry') {
    const fields = (d.fields ?? d.values ?? d) as Record<string, unknown>;
    return `telemetry ${Object.entries(fields)
      .slice(0, 4)
      .map(([k, v]) => `${k}=${typeof v === 'object' ? JSON.stringify(v) : String(v)}`)
      .join(', ')}`;
  }
  if (e.topic === 'decision') return `decision ${String(d.label ?? d.kind ?? '')}`;
  return JSON.stringify(d).slice(0, 120);
}

function LivePanel({ pid, view, unit }: { pid: string; view: PreviewView; unit: string }) {
  const e = useEditor();
  const qc = useQueryClient();
  const state = useRuntimeState(pid);
  const pt = useModelFacts('pt');
  const dt = useModelFacts('dt');
  const [log, setLog] = useState<RuntimeStreamEvent[]>([]);
  const stream = useRuntimeStream(
    pid,
    ['pt_event', 'observation', 'alarm', 'telemetry', 'decision', 'state'],
    (ev) => {
      if (ev.topic === 'state' || ev.topic === 'observation' || ev.topic === 'decision') void qc.invalidateQueries({ queryKey: runtimeKeys.state(pid) });
      if (ev.topic !== 'state') setLog((l) => [ev, ...l].slice(0, 60));
    },
    () => void qc.invalidateQueries({ queryKey: runtimeKeys.state(pid) }),
  );
  const api = useMemo(() => runtimeApi(pid), [pid]);
  const [level, setLevel] = useState<'pt' | 'dt'>(view.mode === 'monitor' ? 'pt' : 'dt');
  const [label, setLabel] = useState('');
  const [at, setAt] = useState('');
  const [busy, setBusy] = useState(false);
  const [result, setResult] = useState<{ ok: boolean; text: string } | null>(null);
  const [playing, setPlaying] = useState<string | null>(null);
  const s = state.data;
  const now = s?.time.text ?? '0';
  const send = async (lvl: 'pt' | 'dt', l: string, time: string) => {
    const body = { label: l, time };
    const r = lvl === 'pt' ? await api.post<Record<string, unknown>>('/runtime/pt-event', body) : await api.post<Record<string, unknown>>('/runtime/event', body);
    return r;
  };
  const submit = async () => {
    setBusy(true);
    setResult(null);
    try {
      await send(level, label, at || now);
      setResult({ ok: true, text: `${label} accepted at t = ${at || now}` });
    } catch (err) {
      setResult({ ok: false, text: err instanceof ApiError ? err.message : String(err) });
    } finally {
      setBusy(false);
      void qc.invalidateQueries({ queryKey: runtimeKeys.state(pid) });
    }
  };
  const play = async (scenarioId: string) => {
    const sc = e.doc.scenarios.find((x) => x.id === scenarioId);
    if (!sc) return;
    setPlaying(scenarioId);
    setResult(null);
    try {
      for (const st of sc.steps) {
        if (st.kind !== 'event' || st.expectRefused || !st.label || !st.at) continue;
        try {
          await send(st.level === 'pt' ? 'pt' : 'dt', st.label, st.at);
        } catch (err) {
          setResult({ ok: false, text: `Step ${st.id} (${st.label} at t = ${st.at}) refused: ${err instanceof Error ? err.message : String(err)}` });
          return;
        }
      }
      setResult({ ok: true, text: `Scenario “${sc.name}” played: ${sc.steps.filter((x) => x.kind === 'event' && !x.expectRefused).length} event(s) sent.` });
    } finally {
      setPlaying(null);
      void qc.invalidateQueries({ queryKey: runtimeKeys.state(pid) });
    }
  };
  const labels = level === 'pt' ? pt.labels : dt.labels;
  const conformance = s?.conformance;
  return (
    <div className="grid-main-side">
      <div className="stack">
        <Pane title="Twin state (from the preview runtime)" actions={<StatusBadge tone={stream === 'open' ? 'ok' : 'neutral'} label={stream === 'open' ? 'Live stream' : stream} />}>
          {state.isPending ? (
            <Skeleton lines={3} />
          ) : state.isError ? (
            <ErrorBlock error={state.error} compact onRetry={() => void state.refetch()} />
          ) : s ? (
            <div className="stack-sm">
              <div className="row-wrap">
                <span className="vts-twin-card__mode" style={{ fontSize: 22, fontWeight: 700 }}>
                  {[...new Set(s.configurations.map((c) => c.location))].map((l) => e.doc.presentation.states?.[l]?.label || l).join(' | ')}
                </span>
                {!s.deterministic && <StatusBadge tone="warning" label="Several possible states" />}
                {conformance && <StatusBadge tone={conformance.status === 'conformant' ? 'ok' : 'critical'} label={conformance.status === 'conformant' ? 'CONFORMANT' : 'NOT CONFORMANT'} title={conformance.definition} />}
              </div>
              <KeyValue
                compact
                items={[
                  ['Logical time', `t = ${s.time.text} ${unit}`],
                  ['Deadline', s.deadline ? `t ≤ ${s.deadline.text}` : 'none'],
                  ['Observations', conformance ? `${conformance.observations} (${conformance.observations_rejected} refused)` : '—'],
                  ['Ledger', `${s.ledger.records} record(s)`],
                ]}
              />
              {s.enabled.length > 0 && (
                <table className="vts-table">
                  <caption className="small" style={{ textAlign: 'left' }}>
                    Possible next events (kernel)
                  </caption>
                  <thead>
                    <tr>
                      <th scope="col">Event</th>
                      <th scope="col">To</th>
                      <th scope="col">Window</th>
                    </tr>
                  </thead>
                  <tbody>
                    {s.enabled.map((x) => (
                      <tr key={`${x.member}-${x.transition}`}>
                        <td className="mono small">{x.label}</td>
                        <td className="mono small">{x.target}</td>
                        <td className="xsmall num">
                          t ∈ [{x.window.earliest.text}, {x.window.latest ? x.window.latest.text : '∞'})
                        </td>
                      </tr>
                    ))}
                  </tbody>
                </table>
              )}
            </div>
          ) : null}
        </Pane>
        <Pane title="Event log">
          {log.length === 0 ? (
            <p className="small muted">Events, refusals and telemetry of this preview appear here.</p>
          ) : (
            <ol className="vts-list xsmall mono" style={{ maxHeight: 260, overflow: 'auto', listStyle: 'none', padding: 0 }}>
              {log.map((ev) => (
                <li key={`${ev.id}-${ev.topic}`}>
                  <span className="subtle">#{ev.id}</span> <strong>{ev.topic}</strong> {summarize(ev)}
                </li>
              ))}
            </ol>
          )}
        </Pane>
      </div>
      <div className="stack">
        <Pane title={<span className="row"><Send size={14} aria-hidden="true" /> Drive the preview</span>}>
          <div className="stack-sm">
            <Segmented
              value={level}
              onChange={(v) => {
                setLevel(v);
                setLabel('');
              }}
              label="Event level"
              options={[
                ...(view.mode === 'monitor' ? [{ id: 'pt' as const, label: 'Plant event' }] : []),
                { id: 'dt' as const, label: 'Twin event' },
              ]}
            />
            <select className="vts-select" value={label} onChange={(x) => setLabel(x.target.value)} aria-label="Event">
              <option value="">Choose an event…</option>
              {labels.map((l) => (
                <option key={l}>{l}</option>
              ))}
            </select>
            <div className="row">
              <label className="vts-f vts-f--inline">
                <span>at t =</span>
                <input className="vts-input mono" style={{ width: 100 }} value={at} placeholder={now} onChange={(x) => setAt(x.target.value.trim())} />
              </label>
              <Button size="sm" variant="primary" icon={<Send size={13} />} loading={busy} disabled={!label} onClick={() => void submit()}>
                Send
              </Button>
            </div>
            <p className="xsmall subtle" style={{ margin: 0 }}>
              {level === 'pt' ? 'Plant events are translated by the runtime adapter and checked by the kernel; a refused event is a conformance violation of this preview only.' : 'Twin events go straight to the kernel; it refuses events outside their window.'}
            </p>
            {e.doc.scenarios.length > 0 && (
              <div className="stack-sm" style={{ borderTop: '1px solid var(--divider)', paddingTop: 8 }}>
                <span className="vts-label">Play a scenario&apos;s events</span>
                {e.doc.scenarios.map((sc) => (
                  <div key={sc.id} className="row">
                    <span className="small grow truncate">{sc.name}</span>
                    <Button size="sm" icon={<Play size={13} />} loading={playing === sc.id} disabled={!!playing} onClick={() => void play(sc.id)}>
                      Play
                    </Button>
                  </div>
                ))}
                <span className="xsmall subtle">Sends the scenario&apos;s event steps at their logical times; start the preview without its simulator so nothing else drives it.</span>
              </div>
            )}
            {result && (
              <Callout tone={result.ok ? 'ok' : 'critical'} title={result.ok ? 'Accepted' : 'Refused by the runtime'}>
                <span className="small">{result.text}</span>
              </Callout>
            )}
          </div>
        </Pane>
      </div>
    </div>
  );
}

export default function PreviewPage() {
  const e = useEditor();
  const qc = useQueryClient();
  const [params] = useSearchParams();
  const fromScenario = params.get('scenario');
  const [polling, setPolling] = useState(false);
  const q = usePreview(e.id, e.version, polling);
  const [speed, setSpeed] = useState('1');
  const [paused, setPaused] = useState(false);
  const [simulator, setSimulator] = useState(!fromScenario);
  const [busy, setBusy] = useState<'start' | 'stop' | null>(null);
  const [error, setError] = useState<unknown>(null);
  const [startInfo, setStartInfo] = useState<PreviewView | null>(null);
  const identity = e.doc.identity;
  const unit = identity.timeUnit || 's';
  const view = q.data;
  const rt = view?.runtime;
  const running = rt?.state === 'running';
  if (polling !== (rt?.state === 'starting' || rt?.state === 'running')) setPolling(rt?.state === 'starting' || rt?.state === 'running');
  const pid = view?.previewId ?? '';
  const info = startInfo && startInfo.previewId === pid ? startInfo : null;
  const pluginId = e.doc.presentation.plugin || identity.plugin || null;
  const twin: TwinDetail | null = useMemo(
    () =>
      pid
        ? {
            id: pid,
            name: `${identity.name} — Studio preview`,
            assetId: null,
            description: identity.description ?? '',
            modelId: identity.modelId,
            ticksPerUnit: identity.ticksPerUnit,
            runtimeUrl: rt?.runtimeUrl ?? null,
            worldUrl: rt?.worldUrl ?? null,
            presentation: { ...e.doc.presentation, timeUnit: unit, plugin: pluginId ?? undefined },
            createdAt: '',
            deployment: null,
            package: null,
            bindings: [],
            trust: {} as TwinDetail['trust'],
            runtimeConnected: running,
          }
        : null,
    [pid, identity, e.doc.presentation, unit, pluginId, rt?.runtimeUrl, rt?.worldUrl, running],
  );
  const plugin = twin ? pluginsFor(twin)[0] : undefined;
  const start = async () => {
    setBusy('start');
    setError(null);
    try {
      await e.saveNow();
      const r = await blueprintApi.startPreview(e.id, e.version, { speed, paused, simulator });
      setStartInfo(r);
      setPolling(true);
    } catch (err) {
      setError(err);
    } finally {
      setBusy(null);
      void qc.invalidateQueries({ queryKey: bpKeys.preview(e.id, e.version) });
    }
  };
  const stop = async () => {
    setBusy('stop');
    setError(null);
    try {
      await blueprintApi.stopPreview(e.id, e.version);
    } catch (err) {
      setError(err);
    } finally {
      setBusy(null);
      void qc.invalidateQueries({ queryKey: bpKeys.preview(e.id, e.version) });
    }
  };
  const mode = identity.runtimeMode;
  const simKind = (e.doc.simulation?.kind as string | undefined) ?? 'none';
  return (
    <EdPage
      title="Preview / Simulation"
      wide
      description="Run this version on the real runtime in an isolated sandbox before releasing it."
      actions={
        running ? (
          <>
            <Button size="sm" icon={<RotateCcw size={14} />} loading={busy === 'start'} onClick={() => void start()}>
              Restart
            </Button>
            <Button size="sm" variant="danger" icon={<Square size={14} />} loading={busy === 'stop'} onClick={() => void stop()}>
              Stop preview
            </Button>
          </>
        ) : (
          <Button size="sm" variant="primary" icon={<Power size={14} />} loading={busy === 'start' || rt?.state === 'starting'} onClick={() => void start()}>
            Start preview
          </Button>
        )
      }
      guide={
        <>
          The preview runs the exact runtime and simulator a deployment would use, on this version&apos;s verified core (a draft is built in a sandbox and never recorded). It has no twin record, writes no telemetry to Operate and is stopped
          with Studio. Use it to watch the twin follow the simulator, to send events yourself, or to play a scenario.
        </>
      }
    >
      <Banner name={identity.name} version={e.version} />
      {error !== null && <ErrorBlock error={error} />}
      {q.isPending ? (
        <Skeleton lines={3} />
      ) : (
        <Pane
          title="Preview runtime"
          actions={<StatusBadge tone={STATE_TONE[rt?.state ?? 'not_started'] ?? 'neutral'} label={(rt?.state ?? 'not_started').replace('_', ' ').toUpperCase()} spin={rt?.state === 'starting'} />}
        >
          <div className="stack-sm">
            {!running && (
              <div className="row-wrap">
                <label className="vts-f vts-f--inline">
                  <span>Speed</span>
                  <select className="vts-select" value={speed} onChange={(x) => setSpeed(x.target.value)}>
                    {['1', '2', '5', '10'].map((s) => (
                      <option key={s} value={s}>
                        {s}×
                      </option>
                    ))}
                  </select>
                </label>
                {mode === 'cosimulation' && (
                  <label className="vts-check small">
                    <input type="checkbox" checked={paused} onChange={(x) => setPaused(x.target.checked)} /> Start paused
                  </label>
                )}
                {mode === 'monitor' && simKind === 'event-script' && (
                  <label className="vts-check small">
                    <input type="checkbox" checked={simulator} onChange={(x) => setSimulator(x.target.checked)} /> Run the event-script simulator
                  </label>
                )}
                {fromScenario && (
                  <span className="xsmall subtle">
                    Opened from scenario <Link to={blueprintRoute(e.id, e.version, `test/scenarios/${encodeURIComponent(fromScenario)}`)}>{fromScenario}</Link>: start without the simulator, then play it.
                  </span>
                )}
              </div>
            )}
            {rt?.message && rt.state === 'failed' && <Callout tone="critical" title="The preview failed">{rt.message}</Callout>}
            <KeyValue
              compact
              items={[
                ['Preview id', <span key="p" className="mono xsmall">{pid}</span>],
                ['Runtime mode', mode === 'cosimulation' ? 'Co-simulation (the twin drives the simulated plant)' : 'Monitoring (the twin follows plant events)'],
                ['Simulator', simKind === 'mobile-robot' ? 'Mobile-robot world (twin-world)' : simKind === 'event-script' ? 'Event script (twin-pt-feed)' : 'none'],
                ...(info?.core
                  ? ([
                      [
                        'Verified core',
                        <span key="c" className="small">
                          {String(info.core.source ?? (info.core.packageId ? `package ${String(info.core.packageId)}` : 'package'))}
                          {info.core.packageHash ? (
                            <>
                              {' '}
                              <HashChip value={String(info.core.packageHash)} label="package hash" />
                            </>
                          ) : null}
                          {info.core.checksPassed !== undefined ? ` · ${String(info.core.checksPassed)} integrity checks passed` : ''}
                        </span>,
                      ],
                    ] as [string, React.ReactNode][])
                  : []),
                ['Processes', (rt?.processes ?? []).length ? (rt?.processes ?? []).map((p) => `${p.name} (${p.state}${p.port ? `, :${p.port}` : ''})`).join(' · ') : '—'],
              ]}
            />
            <span className="xsmall subtle">{info?.isolation ?? 'No twin record, no deployment, no stored telemetry; ledgers and logs live in the preview’s own directory.'}</span>
          </div>
        </Pane>
      )}
      {running && pid && twin && plugin && (
        <Pane title={<span className="row"><FlaskConical size={14} aria-hidden="true" /> {plugin.title} (preview)</span>} flush>
          <div style={{ padding: 12 }}>
            <PluginHost plugin={plugin} twin={twin} asset={null} runtimeConnected={running} />
          </div>
        </Pane>
      )}
      {running && pid && view && <LivePanel pid={pid} view={{ ...view, mode: view.mode ?? info?.mode ?? mode }} unit={unit} />}
      {!running && rt?.state !== 'starting' && (
        <Callout tone="info" title="Not running">
          Start the preview to run this version. A published version previews its released package; a draft is built in a sandbox first (a few seconds).
        </Callout>
      )}
    </EdPage>
  );
}
