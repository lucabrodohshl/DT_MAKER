/**
 * Data hooks of the drone mission view.
 *
 * Live: everything comes from the twin's runtime (and, for the PHYSICAL WORLD
 * panel only, from the world's observer API) through the Studio proxy. Stream
 * events are applied in order; after a gap or reconnect every view is
 * refetched from the authoritative endpoints.
 *
 * Replay: everything comes from POST /runtime/replay — the kernel re-executes
 * the recorded inputs with the recorded package; the map knowledge, plans and
 * commands are the chained context records of that execution, and positions
 * are the recorded telemetry anchored to ledger positions.
 */
import { useQuery, useQueryClient } from '@tanstack/react-query';
import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import type { DomainPluginProps } from '@/plugins/types';
import type { LedgerPage, LedgerRecord, SimulationState } from '@/runtime/types';
import {
  applyUpdate,
  decodeGrid,
  reconstruct,
  type CellChange,
  type EpisodeJson,
  type Grid,
  type GridJson,
  type MapUpdateJson,
  type MissionJson,
  type PlanJson,
  type Point,
  type ReplayFrameJson,
  type TelemetryJson,
} from './model';

export interface MissionView {
  known: Grid | null;
  recent: Set<number>;
  truth: Grid | null;
  observerAvailable: boolean;
  truthDrone: TelemetryJson | null;
  telemetry: TelemetryJson | null;
  trajectory: Point[];
  mission: MissionJson | null;
  activePlan: PlanJson | null;
  endedPlans: PlanJson[];
  episodes: EpisodeJson[];
  sim: SimulationState | null;
  records: LedgerRecord[];
  facility: { at: number; text: string }[];
  location: string;
  loading: boolean;
  error: unknown;
}

const RECENT_MS = 3500;
const TOPICS = ['telemetry', 'map', 'map_reset', 'plan', 'planning', 'decision', 'ledger', 'sim', 'facility', 'mission'];

function toPoint(t: TelemetryJson): Point {
  return { x: t.x_mm, y: t.y_mm };
}

/** Live mission data for the current execution of the twin. */
export function useLiveMission(props: DomainPluginProps): MissionView {
  const { runtime, subscribe, twin, runtimeConnected } = props;
  const qc = useQueryClient();
  const key = useCallback((...k: string[]) => ['drone', twin.id, ...k], [twin.id]);
  const enabled = runtimeConnected;

  // Authoritative snapshots (refetched after gaps, resets and relevant events).
  const sim = useQuery({ queryKey: key('sim'), queryFn: () => runtime.get<SimulationState>('/simulation/state'), enabled, refetchInterval: 2000 });
  const session = sim.data?.session ?? props.state?.session ?? '';
  const known = useQuery({ queryKey: key('known', session), queryFn: () => runtime.get<{ map: GridJson; seq: number }>('/world/known'), enabled, staleTime: 0 });
  const truth = useQuery({ queryKey: key('truth'), queryFn: () => runtime.get<{ map: GridJson }>('/observer/ground-truth'), enabled, retry: false, staleTime: 0 });
  const running = sim.data?.status === 'running';
  const truthState = useQuery({
    queryKey: key('truth-state'),
    queryFn: () => runtime.get<{ telemetry: TelemetryJson }>('/observer/state'),
    enabled: enabled && truth.isSuccess,
    retry: false,
    refetchInterval: running ? 400 : false,
  });
  const mission = useQuery({ queryKey: key('mission'), queryFn: () => runtime.get<MissionJson>('/mission'), enabled });
  const plans = useQuery({ queryKey: key('plans'), queryFn: () => runtime.get<{ active: PlanJson | null; history: PlanJson[] }>('/planner/history'), enabled });
  const episodes = useQuery({ queryKey: key('episodes'), queryFn: () => runtime.get<{ episodes: EpisodeJson[] }>('/planner/episodes'), enabled });
  const ledgerTail = useQuery({
    queryKey: key('ledger', session),
    queryFn: () => runtime.get<LedgerPage>('/runtime/ledger', { session, tail: 600 }),
    enabled: enabled && !!session,
    staleTime: 0,
  });
  const recorded = useQuery({
    queryKey: key('trajectory', session),
    queryFn: () => runtime.get<{ samples: TelemetryJson[] }>(`/runtime/executions/${encodeURIComponent(session)}/telemetry`, { max: 3000 }),
    enabled: enabled && !!session,
    staleTime: 0,
  });

  // Ordered stream increments on top of the snapshots (state set only in event callbacks).
  const [updates, setUpdates] = useState<MapUpdateJson[]>([]);
  const [recent, setRecent] = useState<{ cells: CellChange[]; active: boolean }>({ cells: [], active: false });
  const [live, setLive] = useState<TelemetryJson[]>([]);
  const [newRecords, setNewRecords] = useState<LedgerRecord[]>([]);
  const [facility, setFacility] = useState<{ at: number; text: string }[]>([]);
  const fetching = useRef(false);
  const lastSeqRef = useRef(-1);

  const tailRecords = useMemo(() => ledgerTail.data?.records ?? [], [ledgerTail.data]);
  const records = useMemo(() => {
    const last = tailRecords[tailRecords.length - 1]?.seq ?? -1;
    return [...tailRecords, ...newRecords.filter((r) => r.seq > last)];
  }, [tailRecords, newRecords]);
  useEffect(() => {
    lastSeqRef.current = records[records.length - 1]?.seq ?? -1;  // read by the incremental fetch
  }, [records]);

  const resync = useCallback(() => {
    setUpdates([]);
    setLive([]);
    setNewRecords([]);
    void qc.invalidateQueries({ queryKey: ['drone', twin.id] });
  }, [qc, twin.id]);

  const fetchNewRecords = useCallback(async () => {
    if (!session || fetching.current) return;
    fetching.current = true;
    try {
      const page = await runtime.get<LedgerPage>('/runtime/ledger', { session, since: lastSeqRef.current + 1, limit: 200 });
      if (page.records.length) setNewRecords((prev) => [...prev, ...page.records]);
    } catch {
      /* surfaced through the queries' error states */
    } finally {
      fetching.current = false;
    }
  }, [runtime, session]);

  useEffect(() => {
    if (!enabled) return;
    let ledgerTimer: ReturnType<typeof setTimeout> | null = null;
    let recentTimer: ReturnType<typeof setTimeout> | null = null;
    const unsubscribe = subscribe(
      TOPICS,
      (e) => {
        const d = e.data as Record<string, unknown>;
        switch (e.topic) {
          case 'telemetry':
            setLive((l) => (l.length > 6000 ? [...l.slice(-6000), d as unknown as TelemetryJson] : [...l, d as unknown as TelemetryJson]));
            break;
          case 'map': {
            const u = d as unknown as MapUpdateJson;
            setUpdates((list) => [...list, u]);
            setRecent({ cells: u.cells, active: true });
            if (recentTimer) clearTimeout(recentTimer);
            recentTimer = setTimeout(() => setRecent((r) => ({ ...r, active: false })), RECENT_MS);
            break;
          }
          case 'map_reset':
          case 'sim':
            if (e.topic === 'map_reset' || d.status === 'reset') {
              setFacility([]);
              resync();
            } else {
              void qc.invalidateQueries({ queryKey: key('sim') });
            }
            break;
          case 'facility':
            setFacility((f) => [...f, { at: Number(d.at ?? 0), text: String(d.text ?? '') }]);
            void qc.invalidateQueries({ queryKey: key('truth') });
            break;
          case 'ledger':
            if (!ledgerTimer) {
              ledgerTimer = setTimeout(() => {
                ledgerTimer = null;
                void fetchNewRecords();
              }, 250);
            }
            break;
          case 'plan':
          case 'decision':
            void qc.invalidateQueries({ queryKey: key('plans') });
            void qc.invalidateQueries({ queryKey: key('mission') });
            break;
          case 'planning':
            void qc.invalidateQueries({ queryKey: key('episodes') });
            break;
          case 'mission':
            void qc.invalidateQueries({ queryKey: key('mission') });
            break;
          default:
            break;
        }
      },
      () => resync(),
    );
    return () => {
      if (ledgerTimer) clearTimeout(ledgerTimer);
      if (recentTimer) clearTimeout(recentTimer);
      unsubscribe();
    };
  }, [enabled, subscribe, fetchNewRecords, resync, qc, key]);

  // Derived views.
  const grid = useMemo(() => {
    let g = decodeGrid(known.data?.map);
    const base = known.data?.seq ?? 0;
    for (const u of updates) if (g && u.seq > base) g = applyUpdate(g, u);
    return g;
  }, [known.data, updates]);
  const recentCells = useMemo(
    () => (grid && recent.active ? new Set(recent.cells.map((c) => c.cell[1] * grid.width + c.cell[0])) : new Set<number>()),
    [grid, recent],
  );
  const samples = useMemo(() => {
    const base = recorded.data?.samples ?? [];
    const lastAt = base[base.length - 1]?.at ?? -1;
    return [...base, ...live.filter((t) => t.at > lastAt)];
  }, [recorded.data, live]);
  const trajectory = useMemo(() => samples.filter((s) => (s.alt_mm ?? 0) > 0).map(toPoint), [samples]);

  const history = plans.data?.history ?? [];
  return {
    known: grid,
    recent: recentCells,
    truth: decodeGrid(truth.data?.map),
    observerAvailable: truth.isSuccess,
    truthDrone: truthState.data?.telemetry ?? null,
    telemetry: samples[samples.length - 1] ?? null,
    trajectory,
    mission: mission.data ?? null,
    activePlan: plans.data?.active ?? null,
    endedPlans: history.filter((p) => p.status === 'invalidated' || p.status === 'superseded'),
    episodes: episodes.data?.episodes ?? [],
    sim: sim.data ?? null,
    records,
    facility,
    location: props.state?.locations?.[0] ?? props.state?.configurations?.[0]?.location ?? '',
    loading: known.isPending || sim.isPending,
    error: known.error ?? sim.error ?? null,
  };
}

/** Replay data: the execution as re-executed by the kernel, up to the current frame. */
export function useReplayMission(props: DomainPluginProps): MissionView {
  const session = props.replay?.session ?? '';
  const seq = Number((props.replay?.frame as { seq?: number } | undefined)?.seq ?? 0);
  const replay = useQuery({
    queryKey: ['drone', props.twin.id, 'replay', session],
    queryFn: () =>
      props.runtime.post<{ frames: ReplayFrameJson[]; telemetry?: TelemetryJson[] }>('/runtime/replay', {
        session,
        frames: true,
        telemetry_max: 4000,
      }),
    enabled: !!session,
    staleTime: Infinity,
  });
  const frames = useMemo(() => replay.data?.frames ?? [], [replay.data]);
  const rec = useMemo(() => reconstruct(frames, seq), [frames, seq]);
  const samples = useMemo(() => (replay.data?.telemetry ?? []).filter((s) => (s.ledger_seq ?? 0) <= seq), [replay.data, seq]);
  const episodes = useMemo(() => (rec.episode ? [rec.episode] : []), [rec.episode]);
  const records = useMemo<LedgerRecord[]>(
    () =>
      frames
        .filter((f) => f.seq <= seq)
        .map((f) => ({ seq: f.seq, kind: f.kind, hash: f.hash, body: { ...(f.fields as object), kind: f.kind, seq: f.seq } as unknown as LedgerRecord['body'] })),
    [frames, seq],
  );
  const telemetry = samples[samples.length - 1] ?? null;
  return {
    known: rec.known,
    recent: rec.recent,
    truth: null,
    observerAvailable: false,
    truthDrone: null,
    telemetry,
    trajectory: samples.filter((s) => (s.alt_mm ?? 0) > 0).map(toPoint),
    mission: rec.home ? { mission: '', home: rec.home, goal: rec.activePlan?.goal ?? '', targets: rec.targets, planning: false, finished: false, planner: 'A*' } : null,
    activePlan: rec.activePlan,
    endedPlans: rec.endedPlans,
    episodes,
    sim: null,
    records,
    facility: [],
    location: rec.location,
    loading: replay.isPending,
    error: replay.error,
  };
}
