/**
 * Drone mission view: PHYSICAL WORLD (observer ground truth) next to DIGITAL
 * TWIN KNOWLEDGE (what the twin knows through the environment API), with the
 * twin's plans, the planner's candidates, the kernel's semantic state and the
 * ledger-backed timeline.
 *
 * The two maps are deliberately separate sources: the left map is the
 * simulator's ground truth (observer API, visualisation only — the runtime
 * never reads it); the right map is the twin's belief, the only map the
 * planner plans on. Outlined cells on the left are where the twin's knowledge
 * still differs from reality — the reason replanning happens.
 */
import { History, Pause, Play, RotateCcw, StepForward } from 'lucide-react';
import { useMemo, useState } from 'react';
import { useNavigate } from 'react-router-dom';
import { Button, ModeBanner, Panel, RuntimeUnavailable, Segmented, Skeleton, StatusBadge } from '@/design';
import type { DomainPluginProps } from '@/plugins/types';
import type { SimulationState } from '@/runtime/types';
import { MapCanvas, type CandidatePath, type MapOverlay } from './MapCanvas';
import { cellAt, isPlan, planPoints, unknownCount, type Grid } from './model';
import { CandidatesPanel, OpsPanel, Timeline } from './panels';
import { useLiveMission, useReplayMission, type MissionView } from './useMission';
import './drone.css';

const SPEEDS = ['1', '2', '5', '10'] as const;
type Speed = (typeof SPEEDS)[number];

/** Cells where two maps disagree (observer-side comparison; never fed to the twin). */
function differences(truth: Grid | null, known: Grid | null): Set<number> {
  const out = new Set<number>();
  if (!truth || !known || truth.width !== known.width || truth.height !== known.height) return out;
  for (let y = 0; y < truth.height; y++) {
    for (let x = 0; x < truth.width; x++) {
      const k = cellAt(known, x, y);
      const t = cellAt(truth, x, y);
      if (k !== '?' && k !== t) out.add(y * truth.width + x);
    }
  }
  return out;
}

function Legend() {
  const items: [string, string][] = [
    ['drone-legend__swatch--wall', 'wall'],
    ['drone-legend__swatch--obstacle', 'obstacle'],
    ['drone-legend__swatch--unknown', 'unknown to the twin'],
    ['drone-legend__swatch--door', 'open door'],
    ['drone-legend__swatch--closed', 'closed door'],
    ['drone-legend__swatch--hazard', 'no-fly zone'],
    ['drone-legend__line--plan', 'active route'],
    ['drone-legend__line--invalid', 'invalidated route'],
    ['drone-legend__line--candidate', 'candidate route'],
    ['drone-legend__line--trajectory', 'flown trajectory'],
    ['drone-legend__swatch--differs', 'twin knowledge differs from reality'],
    ['drone-legend__swatch--recent', 'just discovered'],
  ];
  return (
    <ul className="drone-legend small" aria-label="Map legend">
      {items.map(([cls, text]) => (
        <li key={text}>
          <span className={`drone-legend__mark ${cls}`} aria-hidden="true" />
          {text}
        </li>
      ))}
    </ul>
  );
}

function Controls({ sim, runtime, onAction }: { sim: SimulationState | null; runtime: DomainPluginProps['runtime']; onAction: () => void }) {
  const [busy, setBusy] = useState(false);
  const speed = String(Math.round((sim?.speed_permille ?? 1000) / 1000)) as Speed;
  const call = async (path: string, body: unknown = {}) => {
    setBusy(true);
    try {
      await runtime.post(path, body);
    } finally {
      setBusy(false);
      onAction();
    }
  };
  const running = sim?.status === 'running';
  const finished = sim?.status === 'finished' || sim?.status === 'failed';
  return (
    <div className="row-wrap drone-controls">
      {running ? (
        <Button icon={<Pause size={14} />} onClick={() => void call('/simulation/pause')} loading={busy}>Pause</Button>
      ) : (
        <Button variant="primary" icon={<Play size={14} />} onClick={() => void call('/simulation/start')} loading={busy} disabled={finished}>
          {sim?.mission_started ? 'Resume' : 'Start mission'}
        </Button>
      )}
      <Button icon={<StepForward size={14} />} onClick={() => void call('/simulation/step')} disabled={running || finished || busy}>Step 0.1 s</Button>
      <Button icon={<RotateCcw size={14} />} onClick={() => void call('/simulation/reset')} disabled={busy}>Reset (new session)</Button>
      <Segmented
        label="Simulation speed"
        value={SPEEDS.includes(speed) ? speed : '1'}
        onChange={(v) => void call('/simulation/speed', { speed_permille: Number(v) * 1000 })}
        options={SPEEDS.map((s) => ({ id: s, label: `${s}×` }))}
      />
      {sim && (
        <span className="small muted">
          Co-simulation {sim.status}, logical time {sim.now.text} s{sim.status === 'paused' && !sim.mission_started ? ' — mission not started' : ''}
        </span>
      )}
    </div>
  );
}

function MissionLayout({ props, view, live }: { props: DomainPluginProps; view: MissionView; live: boolean }) {
  const navigate = useNavigate();
  const [highlight, setHighlight] = useState<string | null>(null);
  const episode = view.episodes[view.episodes.length - 1] ?? null;
  // Attention states are those the twin's presentation metadata marks as warnings (e.g. replanning).
  const attention = props.twin.presentation.states?.[view.location]?.tone === 'warning';
  const recentEpisode = episode && view.telemetry ? view.telemetry.at - episode.at < 6000 : false;
  const candidates: CandidatePath[] = useMemo(() => {
    if (!episode || (!attention && !recentEpisode && !highlight)) return [];
    return episode.candidates
      .filter((c) => c.found && !c.duplicate_of && isPlan(c.plan))
      .map((c) => ({
        label: c.label,
        points: isPlan(c.plan) ? planPoints(c.plan) : [],
        selected: c.selected,
        highlighted: highlight === c.label,
      }));
  }, [episode, attention, recentEpisode, highlight]);
  const drone = view.telemetry ? { x: view.telemetry.x_mm, y: view.telemetry.y_mm, headingCdeg: view.telemetry.heading_cdeg } : null;
  const truthDrone = view.truthDrone ? { x: view.truthDrone.x_mm, y: view.truthDrone.y_mm, headingCdeg: view.truthDrone.heading_cdeg } : drone;
  const common: MapOverlay = {
    targets: view.mission?.targets,
    home: view.mission?.home ?? null,
    trajectory: view.trajectory,
  };
  const twinOverlay: MapOverlay = {
    ...common,
    drone,
    activePlan: view.activePlan,
    endedPlans: view.endedPlans,
    candidates,
    recent: view.recent,
  };
  const truthOverlay: MapOverlay = { ...common, drone: truthDrone, differs: differences(view.truth, view.known) };
  const session = props.state?.session ?? props.replay?.session ?? '';
  const openRecord = (seq: number) =>
    navigate(`/audit/provenance?twin=${encodeURIComponent(props.twin.id)}&session=${encodeURIComponent(session)}&seq=${seq}`);

  return (
    <div className="stack drone-view">
      <div className={live && view.truth ? 'drone-maps' : 'drone-maps drone-maps--single'}>
        {live && view.truth && (
          <figure className="drone-map-card">
            <figcaption className="row-between">
              <span className="strong">Physical world</span>
              <span className="small muted">ground truth from the simulator (observer view; the twin never reads it)</span>
            </figcaption>
            <MapCanvas grid={view.truth} overlay={truthOverlay} label="Ground truth of the building with the drone's true position" />
          </figure>
        )}
        <figure className="drone-map-card">
          <figcaption className="row-between">
            <span className="strong">Digital twin knowledge</span>
            <span className="small muted">
              {live ? 'facility plan + onboard observations + facility notices' : 'reconstructed from the recorded execution'}; {unknownCount(view.known)} cells unknown
            </span>
          </figcaption>
          <MapCanvas grid={view.known} overlay={twinOverlay} label="The twin's knowledge of the building with its plans" />
        </figure>
      </div>
      <Legend />
      <div className="grid-main-side">
        <div className="stack">
          <Panel
            title="Planner candidates"
            subtitle={attention ? 'The twin is in an attention state; the latest planning episode is shown' : 'Latest planning episode'}
          >
            <CandidatesPanel episode={episode} highlight={highlight} onHighlight={setHighlight} />
          </Panel>
          <Panel title="Mission timeline" subtitle="Every entry is a ledger record of this execution">
            <Timeline records={view.records} twin={props.twin} facility={view.facility} onSelect={live ? openRecord : undefined} />
          </Panel>
        </div>
        <Panel title="Operations" subtitle={live ? 'Live — from the twin runtime' : 'At the selected replay point'}>
          <OpsPanel
            twin={props.twin}
            state={live ? props.state : null}
            location={view.location}
            telemetry={view.telemetry}
            mission={view.mission}
            activePlan={view.activePlan}
            runtime={props.runtime}
            live={live}
          />
        </Panel>
      </div>
    </div>
  );
}

function LiveView(props: DomainPluginProps) {
  const view = useLiveMission(props);
  const navigate = useNavigate();
  if (!props.runtimeConnected) {
    return <RuntimeUnavailable what="the drone mission view" message="The twin runtime for this drone is not connected. Start the demo (make demo) or the runtime." />;
  }
  if (view.loading) return <Skeleton lines={8} />;
  return (
    <div className="stack">
      <Controls sim={view.sim} runtime={props.runtime} onAction={() => undefined} />
      {view.sim?.status === 'finished' && view.sim.session && (
        <div className="row-wrap">
          <StatusBadge tone="ok" label="Mission finished" />
          <Button
            variant="primary"
            icon={<History size={14} />}
            onClick={() => navigate(`/audit/executions/${encodeURIComponent(props.twin.id)}/${encodeURIComponent(view.sim!.session)}/replay`)}
          >
            Replay mission
          </Button>
          <Button onClick={() => navigate(`/audit/executions/${encodeURIComponent(props.twin.id)}/${encodeURIComponent(view.sim!.session)}`)}>
            Verify ledger
          </Button>
        </div>
      )}
      <MissionLayout props={props} view={view} live />
    </div>
  );
}

function ReplayView(props: DomainPluginProps) {
  const view = useReplayMission(props);
  if (view.loading) return <Skeleton lines={8} />;
  return (
    <div className="stack">
      <ModeBanner mode="replay">
        Twin knowledge, plans and positions reconstructed from the recorded execution (context records and telemetry log). The
        physical ground truth is not part of the twin's evidence and is not shown.
      </ModeBanner>
      <MissionLayout props={props} view={view} live={false} />
    </div>
  );
}

/** Plugin entry component. */
export function DroneMissionView(props: DomainPluginProps) {
  return props.mode === 'replay' ? <ReplayView {...props} /> : <LiveView {...props} />;
}
