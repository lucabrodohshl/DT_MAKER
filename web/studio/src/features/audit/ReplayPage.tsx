/**
 * Replay of a recorded execution.
 *
 * Frames are produced by POST /runtime/replay: the runtime loads the package
 * RECORDED in that ledger (never "whatever is running"), verifies the chain and
 * re-executes every input with a fresh kernel. This page only steps through those
 * frames; it never re-interprets the execution with today's ontology. The
 * historical artefact versions are resolved from the recorded package hash.
 */
import { useQuery } from '@tanstack/react-query';
import { AlertTriangle, ChevronLeft, ChevronRight, Pause, Play, SkipBack } from 'lucide-react';
import { useEffect, useMemo, useRef, useState } from 'react';
import { Link, useParams } from 'react-router-dom';
import { useTwin } from '@/api/queries';
import type { TwinDetail } from '@/api/types';
import { runtimeApi } from '@/runtime/client';
import { ticksToText } from '@/runtime/time';
import { frameLabel, frameTicks, frameTransition, propositionsAt, stateAt } from '@/runtime/replay';
import type { LedgerRecord, RecordedConfiguration, ReplayFrameRecord, ReplayResult, TwinIr } from '@/runtime/types';
import { pluginsFor } from '@/plugins/registry';
import { BehaviorGraph } from '@/features/behavior/BehaviorGraph';
import { usePackageIr } from '@/features/behavior/useBehavior';
import { useRuntimeSubscribe } from '@/features/assets/AssetPluginTab';
import { Button, Callout, ErrorBlock, HashChip, KeyValue, ModeBanner, Panel, PageHeader, Segmented, Skeleton, StatusBadge } from '@/design';
import { Crumbs, PackageLink, RefLink, LearnMore } from '@/features/common/links';
import { KIND_LABEL, kindTone, useStudioPackageForHash } from './ledger';

const SPEEDS = ['0.25', '0.5', '1', '2', '5', '10'] as const;
type Speed = (typeof SPEEDS)[number];

function ReplayGraph({ ir, frame, configurations, twin }: { ir: TwinIr; frame: ReplayFrameRecord | undefined; configurations: RecordedConfiguration[] | null; twin: TwinDetail }) {
  const state = useMemo(() => {
    const st = configurations;
    if (!st) return null;
    return {
      session: '',
      time: { ticks: st[0]?.time ?? 0, text: ticksToText(st[0]?.time ?? 0, ir.time.ticks_per_unit || 1) },
      configurations: st.map((s) => ({
        location: s.location,
        clocks: Object.fromEntries(Object.entries(s.clocks).map(([k, v]) => [k, { ticks: v, text: ticksToText(v, ir.time.ticks_per_unit || 1) }])),
        time: { ticks: s.time, text: ticksToText(s.time, ir.time.ticks_per_unit || 1) },
      })),
      deterministic: st.length === 1,
      propositions: [],
      enabled: [],
      deadline: null,
      ledger: { records: 0, head: '' },
      failed: false,
      closed: false,
    };
  }, [configurations, ir]);
  const taken = frameTransition(frame);
  return (
    <BehaviorGraph
      ir={ir}
      state={state}
      presentation={twin.presentation}
      recent={taken ? [taken.transition] : []}
      selection={null}
      onSelect={() => undefined}
      height={380}
    />
  );
}

export default function ReplayPage() {
  const { twinId = '', session = '' } = useParams();
  const twin = useTwin(twinId);
  const replay = useQuery({
    queryKey: ['runtime', twinId, 'replay', session],
    queryFn: () => runtimeApi(twinId).post<ReplayResult>('/runtime/replay', { session, frames: true, telemetry_max: 2000 }),
    staleTime: Infinity,
    retry: false,
  });
  const frames = useMemo(() => replay.data?.frames ?? [], [replay.data]);
  const [index, setIndex] = useState(0);
  const [playing, setPlaying] = useState(false);
  const [speed, setSpeed] = useState<Speed>('1');
  const clock = useRef(0);
  const packageHash = replay.data?.package?.package_hash;
  const studioPkg = useStudioPackageForHash(twinId, packageHash);
  const ir = usePackageIr(studioPkg?.id);
  const tpu = ir.data?.time.ticks_per_unit ?? twin.data?.ticksPerUnit ?? 1000;
  const frame = frames[index];
  const configurations = useMemo(() => stateAt(frames, index), [frames, index]);
  const propositions = useMemo(() => propositionsAt(frames, index), [frames, index]);
  const subscribe = useRuntimeSubscribe(null, false);

  // Playback: advance logical replay time at `speed` × real time; show the latest frame not after it.
  useEffect(() => {
    if (!playing || frames.length === 0) return;
    clock.current = frameTicks(frames[index]);
    let last = performance.now();
    const id = setInterval(() => {
      const nowMs = performance.now();
      clock.current += ((nowMs - last) / 1000) * Number(speed) * tpu;
      last = nowMs;
      setIndex((i) => {
        let j = i;
        while (j + 1 < frames.length && frameTicks(frames[j + 1]) <= clock.current) j++;
        if (j === i && i + 1 < frames.length && frameTicks(frames[i + 1]) === frameTicks(frames[i])) j = i + 1;
        if (j >= frames.length - 1) setPlaying(false);
        return j;
      });
    }, 100);
    return () => clearInterval(id);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [playing, speed, frames, tpu]);

  const firstDeviation = frames.findIndex((f) => f.kind === 'reject' || f.kind === 'alarm');
  const telemetry = useMemo(() => {
    const t = replay.data?.telemetry ?? [];
    if (!frame) return null;
    let best: Record<string, unknown> | null = null;
    for (const s of t) {
      if (typeof s.ledger_seq === 'number' && s.ledger_seq <= frame.seq) best = s;
    }
    return best;
  }, [replay.data, frame]);
  const plugin = twin.data ? pluginsFor(twin.data)[0] : undefined;
  const binding = (role: string) => studioPkg?.bindings.find((b) => b.role === role);

  return (
    <div className="vts-page">
      <PageHeader
        eyebrow={<Crumbs items={[{ label: 'Audit', to: '/audit' }, { label: session, to: `/twins/${encodeURIComponent(twinId)}/audit/executions/${encodeURIComponent(session)}` }, { label: 'Replay' }]} />}
        title="Replay"
        meta={<>{twin.data && <span>{twin.data.name}</span>}<LearnMore page="audit-and-replay.html#replay" /></>}
      />
      {replay.isPending ? (
        <Panel title="Re-executing the recorded inputs…"><Skeleton lines={6} /></Panel>
      ) : replay.isError ? (
        <Panel title="Replay unavailable"><ErrorBlock error={replay.error} onRetry={() => void replay.refetch()} /></Panel>
      ) : (
        <div className="stack">
          <ModeBanner mode="replay">
            Recorded execution {session}, re-executed by the kernel with the package recorded in its ledger
            {packageHash && <> (<HashChip value={packageHash} label="package hash" />)</>}. Nothing shown here is live.
          </ModeBanner>
          <div className="row-wrap">
            {replay.data.identical ? (
              <StatusBadge tone="ok" label="Replay identical to the recorded execution" size="lg" />
            ) : (
              <StatusBadge tone="critical" icon={AlertTriangle} label={`Replay differs: ${replay.data.mismatches.length} mismatch(es)`} size="lg" />
            )}
            <StatusBadge tone={(replay.data.chain as { valid?: boolean }).valid ? 'ok' : 'critical'} label={(replay.data.chain as { valid?: boolean }).valid ? 'Ledger chain valid' : 'Ledger chain INVALID'} />
            <span className="small muted">
              {replay.data.steps} transitions · {replay.data.delays} delays · {replay.data.rejections} refusals · {replay.data.alarms} alarms · {replay.data.contexts} context records
            </span>
          </div>
          <Panel title="Playback" subtitle="Replay time is logical model time">
            <div className="stack">
              <div className="row-wrap">
                <Button iconOnly icon={<SkipBack size={14} />} onClick={() => { setIndex(0); setPlaying(false); }}>Back to start</Button>
                <Button iconOnly icon={<ChevronLeft size={14} />} onClick={() => setIndex((i) => Math.max(0, i - 1))}>Step backward</Button>
                <Button variant="primary" icon={playing ? <Pause size={14} /> : <Play size={14} />} onClick={() => setPlaying((p) => !p)} disabled={frames.length === 0}>
                  {playing ? 'Pause' : 'Play'}
                </Button>
                <Button iconOnly icon={<ChevronRight size={14} />} onClick={() => setIndex((i) => Math.min(frames.length - 1, i + 1))}>Step forward</Button>
                <Segmented label="Replay speed" value={speed} onChange={setSpeed} options={SPEEDS.map((s) => ({ id: s, label: `${s}×` }))} />
                <Button size="sm" disabled={firstDeviation < 0} onClick={() => { setIndex(firstDeviation); setPlaying(false); }}>
                  Jump to first deviation
                </Button>
                <label className="row small">
                  Jump to event
                  <select className="vts-select" value="" onChange={(e) => { setIndex(Number(e.target.value)); setPlaying(false); }}>
                    <option value="">—</option>
                    {frames.map((f, i) => (f.kind === 'step' || f.kind === 'reject' ? (
                      <option key={f.seq} value={i}>#{f.seq} {frameLabel(f)}</option>
                    ) : null))}
                  </select>
                </label>
              </div>
              <input
                type="range"
                min={0}
                max={Math.max(0, frames.length - 1)}
                value={index}
                onChange={(e) => { setIndex(Number(e.target.value)); setPlaying(false); }}
                aria-label="Replay position"
                aria-valuetext={`Record ${frame?.seq ?? 0}, logical time ${ticksToText(frameTicks(frame), tpu)}`}
                style={{ width: '100%' }}
              />
              <div className="row-between small">
                <span className="strong" style={{ fontSize: 'var(--text-lg)' }}>
                  Replay time t = {ticksToText(frameTicks(frame), tpu)} {twin.data?.presentation.timeUnit ?? ''}
                </span>
                <span className="muted">Record {index + 1} of {frames.length} (#{frame?.seq})</span>
              </div>
            </div>
          </Panel>
          <div className="grid-main-side">
            <div className="stack">
              {plugin && twin.data && (
                <Panel title={`${plugin.title} (replay)`}>
                  <plugin.Component
                    twin={twin.data}
                    asset={null}
                    mode="replay"
                    state={null}
                    runtime={runtimeApi(twinId)}
                    subscribe={subscribe}
                    replay={frame ? { session, index, ticks: frameTicks(frame), record: null, frame: { ...frame, telemetry } } : null}
                    runtimeConnected={false}
                  />
                </Panel>
              )}
              <Panel title="Behavioural state at this point">
                {ir.data && twin.data ? <ReplayGraph ir={ir.data} frame={frame} configurations={configurations} twin={twin.data} /> : <p className="small muted">The recorded package's model is not registered in Studio; the graph cannot be drawn.</p>}
              </Panel>
            </div>
            <div className="stack">
              <Panel title="Record">
                {frame ? (
                  <KeyValue
                    compact
                    items={[
                      ['Kind', <StatusBadge key="k" tone={kindTone(frame.kind)} label={KIND_LABEL[frame.kind] ?? frame.kind} />],
                      ['Sequence', String(frame.seq)],
                      ['State', <span key="s" className="mono small">{configurations?.map((s) => s.location).join(', ') ?? '—'}</span>],
                      ['Transition', <span key="t" className="mono small">{frameTransition(frame) ? `${frameTransition(frame)!.label} (${frameTransition(frame)!.source} → ${frameTransition(frame)!.target})` : '—'}</span>],
                      ['Propositions', <span key="p" className="mono small">{propositions?.join(', ') ?? '—'}</span>],
                      ['Record hash', <HashChip key="h" value={frame.hash} />],
                    ]}
                  />
                ) : (
                  <p className="small muted">No frames.</p>
                )}
                {frame && (
                  <Link className="small" to={`/twins/${encodeURIComponent(twinId)}/audit/provenance?session=${encodeURIComponent(session)}&seq=${frame.seq}`}>
                    Trace decision provenance
                  </Link>
                )}
              </Panel>
              <Panel title="Telemetry at this point" subtitle="Recorded observation data (not evidence)">
                {telemetry ? (
                  <pre className="vts-code" style={{ maxHeight: 220 }}>{JSON.stringify(telemetry, null, 1)}</pre>
                ) : (
                  <p className="small muted">No recorded telemetry before this record.</p>
                )}
              </Panel>
              <Panel title="Historical artefacts" subtitle="Exactly those recorded for this execution">
                {studioPkg ? (
                  <KeyValue
                    compact
                    items={[
                      ['Package', <PackageLink key="p" id={studioPkg.id} />],
                      ['DT model', binding('dt_model') ? <RefLink key="m" refId={binding('dt_model')!.ref} /> : '—'],
                      ['Ontology', binding('ontology') ? <RefLink key="o" refId={binding('ontology')!.ref} /> : '—'],
                      ['Interpretation', binding('dt_interpretation') ? <RefLink key="i" refId={binding('dt_interpretation')!.ref} /> : '—'],
                      ['IR', <HashChip key="ir" value={studioPkg.irSha256} />],
                    ]}
                  />
                ) : (
                  <Callout tone="neutral">The recorded package is not registered in Studio. The runtime still replays it with its recorded package.</Callout>
                )}
              </Panel>
            </div>
          </div>
        </div>
      )}
    </div>
  );
}

export type { LedgerRecord };
