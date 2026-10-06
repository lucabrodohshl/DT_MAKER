# twin-runtime API

`twin-runtime` executes one verified twin package. It listens on
`http://127.0.0.1:8090` by default. This document is the authoritative contract of its HTTP and
Server-Sent-Events interface. The routes are implemented in
[`src/runtime/api_server.cpp`](../src/runtime/api_server.cpp), and the JSON views in
[`src/runtime/views.cpp`](../src/runtime/views.cpp).

Verified Twin Studio forwards every route below to the runtime unchanged. The paths take the form
`/api/v1/twins/{twinId}/{runtime|simulation|planner|mission|world}/…`.

## Conventions

- **Logical time.** Time is never sent as a floating-point number.
  - In requests, times are either integer ticks (`"ticks": 16800`) or exact decimal strings in
    model time units (`"time": "16.8"`).
  - In responses, times are `{"ticks": 16800, "text": "16.8"}`.
  - The drone model uses 1000 ticks per time unit, so 1 tick is 1 ms of logical time.
- **Integers only.** Positions are in millimetres, energy in mWh, battery level in permille, and
  angles in centidegrees.
- **Errors.** Every failed request returns
  `{"error": {"code": "<twin::ErrorCode>", "message": "...", "context": [{"key": "...", "value": "..."}]}}`.
  The status codes are:

  | Status | Meaning | Error code |
  |---|---|---|
  | 400 | Malformed request | `invalid_argument`, `parse_error`, `time_not_representable` |
  | 404 | Unknown execution or package | `not_found` |
  | 409 | Operation not possible in the current state | `state_error` |
  | 422 | The kernel refused a semantic input. This is an answer, not a failure: the refusal is recorded in the ledger. | — |
  | 503 | Infrastructure unavailable: the ledger, or the Physical Twin service | — |
- **POST bodies.** Send JSON, even if it is only `{}`. A POST without a body and without a
  `Content-Length` header makes the HTTP library wait for its read timeout. Use `curl -d '{}'`.
- **Single semantic authority.** No route writes semantic state directly. `/runtime/event`,
  `/runtime/advance` and the mission controller all submit through `TwinSession::submit`: the
  kernel decides, and the ledger records the decision before it is committed.

## Health and identity

### `GET /health`
Returns the runtime's identity:

```json
{"status":"ok","service":"twin-runtime","kernel_version":"1.0.0",
 "package_hash":"1e842ff2…","session":"session-1e842ff2ca267cc2","simulation":{…}}
```

### `GET /runtime/package`
Returns the package as verified at start-up. `twin-runtime` refuses to start with any package
that does not verify. The response contains:
- `package_hash`: the SHA-256 of `manifest.json`;
- `manifest`, with the files and their roles, SHA-256 digests and producers;
- `ir_sha256` and `source_sha256`;
- `checks`: about 50 checks of the form `[{name, passed}]`;
- `alignment`, which holds:
  - `aligned`, `lint_clean`, `verdict` (relation size, zones and SMT calls),
    `label_equivalence` (E), `location_consistency`;
  - `aligner`, the source digest of SemPTDTAlignmentICSE;
  - `syntactic_baseline`.

### `POST /runtime/package/verify`
Runs every integrity check on the package directory again, now. Use it to detect files altered
on disk after start-up. Returns `{package_hash, valid, checks: [{name, passed, detail}], note}`.

### `GET /runtime/model`
Returns the Twin IR (`twin-ir/1`), which renders the behaviour graph:
- `locations`, with invariants and optional layout;
- `transitions`, each with `id`, `source`, `target`, `action`, `guard` and `resets`;
- `clocks`, `channels` and `propositions` (`at(L)` with I_D(L));
- `event_interpretations` (I_D on labels).

## Semantic state

### `GET /runtime/state`
Returns the committed state. It is a set of configurations because the monitoring semantics
preserves nondeterminism. In the drone model the set is always a singleton.

```json
{
 "session": "session-1e842ff2ca267cc2",
 "package_hash": "1e842ff2…",
 "model": {"id": "indoor-drone-dt", "version": "1.0.0", "ir_sha256": "4e84f7dd…"},
 "time": {"ticks": 16800, "text": "16.8"},
 "configurations": [{"location": "NAVIGATING",
                     "clocks": {"t_mode": {"ticks": 0, "text": "0"}, "t_flight": {"ticks": 16700, "text": "16.7"}},
                     "time": {"ticks": 16800, "text": "16.8"}}],
 "locations": ["NAVIGATING"],
 "deterministic": true,
 "propositions": [{"id": "at(NAVIGATING)", "location": "NAVIGATING", "interpretation": "(and airborne (not holding_position) (not returning_home) (= path_conflicts 0))"}],
 "enabled": [{"transition": "NAVIGATING.path_invalidated!.REPLANNING", "label": "path_invalidated!",
              "source": "NAVIGATING", "target": "REPLANNING", "guard": "true", "resets": ["t_mode"],
              "interpretation": "(> path_conflicts 0)",
              "window": {"earliest": {"ticks": 0, "text": "0"}, "latest": {"ticks": 60000, "text": "60"}},
              "enabled_now": true, "member": 0}, …],
 "deadline": {"ticks": 76800, "text": "76.8"},
 "last_transition": {"seq": 22, "transition": "REPLANNING.plan_accepted!.NAVIGATING", "label": "plan_accepted!",
                     "from": "REPLANNING", "to": "NAVIGATING", "source": "mission-controller",
                     "at": {"ticks": 16800, "text": "16.8"}},
 "conformance": {"status": "conformant", "observations": 2, "observations_rejected": 0,
                 "decisions": 4, "decisions_rejected": 0, "alarms": 0,
                 "first_violation_seq": null, "first_violation": "", "definition": "…"},
 "ledger": {"records": 24, "head": "89a53ffa…"},
 "failed": false, "closed": false
}
```

The fields are defined as follows:
- **`enabled`**: every outgoing transition of every possible configuration. Each entry carries
  the exact window of admissible delays, measured from the committed time: `latest: null` means
  unbounded.
- **`deadline`**: the latest logical time that the invariants admit without a discrete step.
- **`conformance`**: computed only from the kernel's verdicts.
  - An *observation* is an input from the PT adapter or the API.
  - A *decision* is an input from the mission controller or an operator.
  - `conformant` means that no observation has been rejected and no deadline alarm has been
    raised.

### `GET /runtime/enabled-transitions` and `GET /runtime/propositions`
These return the corresponding parts of the state on their own.

## Semantic inputs

### `POST /runtime/event`
Body: `{"label": "path_invalidated!" | "transition": "<id>", "time": "17.5" | "ticks": 17500, "source": "operator"}`.

- **200:** the kernel accepted the input. The body contains `{accepted: true, transition, from,
  to, ledger_seq, ledger_hash, …}`.
- **422:** the kernel refused it. The body contains `{accepted: false, error: {code, message,
  context: [...]}, ledger_seq, ledger_hash}`; the refusal itself is a `reject` ledger record.

A floating-point `time` is rejected with 400.

### `POST /runtime/advance`
Body: `{"time": "…"}` or `{"ticks": …}`. Records a pure passage of time. The request is refused
with 422 if the current location's invariant does not admit the delay.

## Prediction and what-if

Prediction never touches the live state. The kernel's functions take the state by value
(`kernel::explore`, `kernel::simulate`), and the session hands them a copy.

### `POST /runtime/predict`
Body: `{"depth": 1..8, "horizon_ticks": n?, "max_nodes": 1..4000}`.

The response is `{from: <state>, limits, exploration: [{root, nodes, truncated, trajectories}], note}`:
- `exploration` holds one entry per possible configuration.
- Each trajectory is a list of steps `[{transition, label, window, state}]`.
- Each edge is exact about *which* transition can fire and the window of *when*. The child state
  is the representative reached at the earliest admissible delay.

### `POST /runtime/simulate`
Body: `{"schedule": [{"label": "plan_accepted!", "time": "17"}, …]}`.

Returns `{admissible, steps: [<outcome>], refusal: <error or null>, note}`. The refusal's
`context` contains `step`, the index of the first inadmissible event. Nothing is recorded.

## Executions, ledger, replay

Every session writes two files to `--ledger-dir`:
- `<name>.ledger.jsonl`: the tamper-evident ledger;
- `<name>.telemetry.jsonl`: the telemetry received during the session. This is observation data,
  not evidence. Each sample carries `ledger_seq`, the ledger position at which it arrived.

### `GET /runtime/executions`
Returns `{"executions": [{session, ledger, telemetry, package_hash, model_id, model_version, records,
ended, started, ended_at, last_time_ticks, last_location, current, replayable}]}`, newest first.
`replayable` means that the exact package recorded in that ledger is available: either it is
running, or it is in a `--package-store`.

### `GET /runtime/executions/{session}` and `GET /runtime/executions/{session}/telemetry?max=2000`
The first returns one execution. The second returns its telemetry, thinned evenly to at most
`max` samples (the first and last samples are always kept): `{session, samples: [...], total}`.

### `GET /runtime/ledger?session=&since=&limit=200&tail=&kind=`
Returns the records of an execution (the current one if `session` is omitted):
`{session, ledger, total_records, matched, records: [{seq, kind, hash, body}]}`.
- Paging works forward from `since`. `tail=N` returns the last N matching records instead.
- Record kinds:

  | Kind | Records |
  |---|---|
  | `genesis` | The start of the session |
  | `step` | An accepted observation or decision: delay plus discrete step, guard evaluations, resets, state before and after, propositions |
  | `delay` | An accepted passage of time |
  | `reject` | A refused input; the state is unchanged |
  | `alarm` | A monitoring alarm, such as a missed deadline |
  | `context` | Non-semantic context (see below) |
  | `end` | The end of the session |

- `context` records carry one of these topics:

  | Topic | Contents |
  |---|---|
  | `knowledge` | The initial known map and the mission |
  | `map_update` | One update applied to the twin's known world |
  | `planning` | A planning episode, with every candidate, its geometric and behavioural verdicts, and the selection |
  | `command` | A command sent to the Physical Twin, and whether it was accepted |

  Context records are chained in causal order. For example, the `planning` record always
  precedes the `plan_accepted!` step it supports.

### `POST /runtime/ledger/verify`
Body: `{"session"?: "…", "replay"?: true}`.

The response contains:
- `{valid, records, head_seq, head_hash, has_end_record, issues: [{code, line, message}]}`;
- `session`, `package_hash` and `running_package`;
- with `replay: true`, the full replay report as well.

### `POST /runtime/ledger/tamper-drill`
Body: `{"session"?: "…", "line"?: n}`. Alters one field in a **copy** of the ledger and verifies
the copy. The real ledger is never modified. The response shows the first invalid line that the
verification reports.

### `POST /runtime/replay`
Body: `{"session"?: "…", "frames"?: true, "telemetry_max"?: n}`.

Replay proceeds as follows:
- It loads the **package recorded in the ledger**, never "whatever is running": it uses the
  running package or one from `--package-store`, and answers 404 if the package is unavailable.
- It verifies the chain.
- It re-executes every input with a fresh kernel instance.
- It compares every recomputed record with the recorded one.

The response is `{chain, identical, steps, delays, rejections, alarms, contexts, mismatches,
final_state, frames: [{seq, kind, hash, fields}], package: {…, running}, telemetry?: [...]}`.
In the frames, `fields` holds the kind-specific fields of the record **as recomputed by the
replay**: the same shape as the ledger record body, minus the chain members (`schema`, `session`,
`seq`, `kind`, `prev_hash`, `package`, `kernel_version`, `wall_time`). Times are integer ticks:

| `kind` | `fields` |
|---|---|
| `genesis` | `{time_base, time_after, state_after, propositions}` |
| `step` | `{input: {source, kind, name, at, payload}, input_digest, time_before, time_after, state_before, outcome: {delay, branches: [{from_member, transition, label, source, target, guard: [{atom, value, bound, holds}], resets, to_member}]}, state_after, propositions}` |
| `delay` | `{input, input_digest, time_before, time_after, state_before, state_after, propositions}` |
| `reject` | `{input, input_digest, error: {code, message, context: {key: value}}, time_after, state_after}` |
| `alarm` | `{alarm, detail, time_after, state_after}` |
| `context` | `{topic, at, data, time_after}` |
| `end` | `{reason, time_after, state_after, propositions}` |

`state_after` and `state_before` are arrays of `{location, clocks: {name: ticks}, time}`, one entry
per possible configuration. `propositions` is an array of ids such as `"at(REPLANNING)"`. A
step's transition is `outcome.branches[0]`, and its label is `input.name`. With `telemetry_max`,
the samples are the recorded telemetry, each carrying `ledger_seq`: the number of records
written when the sample arrived.

## Live stream

### `GET /runtime/stream`
This is a Server-Sent Events stream:
- Every event has an `id`, a monotone sequence number of the runtime's event hub.
- A new connection receives only events published after it connects. To resume, send `Last-Event-ID` or `?after=<id>` (`?after=0` replays the whole buffer of 4000 events).
- If the `id` sequence goes backwards, the runtime has restarted: treat it as a gap and refetch
  `GET /runtime/state`.
- Keep-alive comments are sent every second.

| Event | Payload |
|---|---|
| `state` | Same as `GET /runtime/state`. Sent every other tick and after every reset or finish. |
| `ledger` | `{seq, kind, hash, prev_hash, time_after, transition?, label?, from?, to?, input?, error?, alarm?}`, for every record after genesis |
| `telemetry` | `{at, x_mm, y_mm, alt_mm, vx_mm_s, vy_mm_s, speed_mm_s, battery_permille, energy_mwh, heading_cdeg, gimbal_cdeg, mode, route_id, waypoint_index, goal}`. Here `at` is the PT's logical observation time. |
| `pt_event` | `{at, pt_label, dt_label, note, detail}`: a flight-controller event and its translation through E |
| `observation` | The kernel's verdict on a submitted input, in the same shape as the `/runtime/event` response |
| `decision` | A mission-controller proposal: `{label, accepted, reason, at, ledger_seq}` |
| `planning` | A planning episode, in the same shape as `/planner/episodes` |
| `plan` | `{plan}`: a plan became active or ended (`completed`, `invalidated` or `superseded`) |
| `map` / `map_reset` | An update applied to the twin's known world, or the whole known map |
| `command` | `{command, accepted, error}` |
| `facility` | A scenario event that happened physically. This is observer information; the twin learns of it only through the environment API. |
| `sim` | Simulation lifecycle: `reset`, `running`, `paused`, `finished`, `failed`, or a change of `speed_permille` |
| `alarm` | A monitoring alarm, such as `deadline missed` |
| `mission` | Mission milestones |

## Co-simulation and mission

| Route | Effect |
|---|---|
| `GET /simulation/state` | `{status: paused\|running\|finished\|failed, mission_started, speed_permille, now, ticks, session, ledger, error}` |
| `POST /simulation/start` | Requests the mission start (idempotent) and runs |
| `POST /simulation/pause` | Pauses wall-clock pacing. Logical time does not advance while paused. |
| `POST /simulation/step` | Runs one co-simulation tick (100 ms of logical time) |
| `POST /simulation/reset` | Restarts the Physical Twin scenario, opens a new session and ledger, and keeps old executions |
| `POST /simulation/speed` | Body `{"speed_permille": 2000}`: logical seconds per wall-clock second, times 1000 |
| `GET /mission` | `{mission, home, targets: [{id, name, cell, status: pending\|current\|inspected\|unreachable}], goal, planning, finished, planner, episodes}` |
| `POST /mission/start` | Operator start request only. Running the simulation is a separate step. |

## Planner

| Route | Returns |
|---|---|
| `GET /planner/current-plan` | The active plan, or `null` |
| `GET /planner/history` | `{active, history: [plans with status and reason]}` |
| `GET /planner/episodes` | `{episodes: [...]}`, every planning episode |

Each episode has the form `{id, at, goal, reason, decision, selected, candidates: [...]}`. A
candidate contains:
- `label` (`A`, `B` or `C`) and `profile` (`balanced`, `known-space` or `wide-clearance`);
- `found` and `failure`; when the planner finds no route, `failure` says so, for example "no
  admissible path in the known map";
- `duplicate_of`, set when the candidate is the same route as an earlier one;
- `plan`, which holds:
  - `id`, `waypoints_mm` and `start_mm`;
  - `cost_mm`, the planner's own objective, and `objective_mm`, a common objective comparable
    across candidates;
  - `length_mm`, `unknown_mm`, `energy_mwh` and `expanded`;
- `geometric: {ok, issues}`: independent geometric validation against the **known** map;
- `behavioural: {checked, ok, detail, schedule}`. The kernel simulates the plan's nominal event
  schedule on a copy of the committed state. That schedule is the decision event, then
  `waypoint_reached!` for each waypoint, then the arrival event. The arrival times are an explicit
  assumption of the estimate;
- `selected`.

Geometric feasibility and behavioural admissibility are separate verdicts from separate checkers.
A candidate is selectable only if both pass. The selected candidate is the one with the lowest
`objective_mm`.

## The twin's knowledge of the world

| Route | Returns |
|---|---|
| `GET /world/known` | `{map: {width, height, cell_size_mm, rows: ["##..??", …]}, seq, unknown_cells}`. This is the twin's **belief**: the prior plan plus every update received through the environment API. It is never the ground truth. |
| `GET /world/telemetry` | The latest telemetry sample received from the flight controller |

The ground truth is served only by `twin-world` (default port `:8091`), at
`/observer/ground-truth` and `/observer/state`. It is for visualisation. The runtime has no client
for it: `WorldPort` exposes only `/env/*` and `/pt/*`.

## twin-world (Physical Twin and environment service)

The authoritative route table is in
[`include/twin/world/service.hpp`](../include/twin/world/service.hpp).

| Routes | Purpose |
|---|---|
| `GET /env/map/known`, `GET /env/map/updates?since=`, `GET /env/mission`, `GET /env/hazards` | Building-information service: the Digital Twin's only source of map knowledge |
| `POST /pt/command`, `POST /pt/step {"dt"}`, `GET /pt/telemetry` | Flight controller and co-simulation step |
| `GET /scenario`, `POST /admin/reset` | Scenario metadata and timeline, and restart |
| `GET /observer/ground-truth`, `GET /observer/state`, `POST /observer/inject` | Ground truth for visualisation, and operator "acts of god" |
