# Architecture

Verified Twin Studio is one product built from three layers: formal tooling, a verified execution
engine, and a platform with its GUI. The organising principle is simple. **Behavioural
conclusions about the twin come from exactly one place, the semantic kernel, executing the
compiled form of a model that the semantic aligner has verified.** Everything else proposes,
transports, stores or displays.

```
 Engineering lifecycle (twin-studio + toolchain)          Operations (twin-runtime + world + Studio)

 Ontology K ─┐                                            Physical Twin (twin-world / twin-pt-feed / a PLC)
 I_P, I_D ───┼─▶ SemPTDTAlignmentICSE ─ V_P ~Φ V_D ─┐         │ PT events, telemetry       ▲ commands
 V_P, V_D ───┘   (semantic alignment, Z3)           │         ▼                            │
                                                    │    PtAdapter (E from the alignment evidence)
 V_D ─▶ twin compile ─▶ Twin IR (canonical JSON) ───┤         │ observations                │
          (translation-validated against the        │         ▼                            │
           aligner's own reading of V_D)            │    TwinSession ── kernel decides ── ledger (hash chain)
                                                    ▼         │ state copies               ▲ context records
                                  Verified Twin Package       ├─▶ prediction / what-if (copies)
                                  (hashes, evidence, IR)      ├─▶ mission controller + A* planner ──┘ (decisions = proposals)
                                                    │         ▼
                                                    └──▶ runtime API (REST + SSE) ──▶ Studio proxy ──▶ web UI
```

## Processes

| Process | Port | Role | Source |
|---|---|---|---|
| `twin-studio` | 8080 | Platform API: assets, telemetry history, artefact versions, evidence, packages, deployments, engineering audit. Serves the web UI and proxies runtimes. | `apps/twin-studio`, `src/{studio,platform,ontology}` |
| `twin-runtime` (drone) | 8090 | Co-simulation mode: executes the drone package and drives the Physical Twin simulator. | `apps/twin-runtime`, `src/runtime` |
| `twin-world` | 8091 | Drone Physical Twin and building-information service. Holds the ground truth. | `apps/twin-world`, `src/world` |
| `twin-runtime` (pump) | 8092 | Monitor mode: executes the pump package. The Physical Twin pushes events and telemetry to it. | same binary, `--monitor` |
| `twin-pt-feed` | – | A scripted Physical Twin: the pump's PLC storyline. | `apps/twin-pt-feed`, `src/ptfeed` |
| `twin` | – | Offline toolchain: `compile`, `align`, `package`, `ledger verify/anchor/show`, `replay`. | `apps/twin` |

`make demo` (`scripts/start-demo.sh`) builds, seeds and starts all of them. It then starts each
runtime on exactly the package that Studio deployed for its twin.

## Libraries and their dependency graph

There is one static library per module: target `twin_<module>`, alias `twin::<module>`, headers in
`include/twin/<module>/`. The graph is a DAG. `tests/architecture` checks the rules marked *(checked)*.

```
core ◀── json ◀── ir ◀── compiler (+ aligner::dtpta)  ◀── alignment (+ aligner::semalign)
core ◀── ir_model ◀── kernel                    (kernel: core + IR model only)          (checked)
kernel, ledger, package ◀── runtime (TwinSession: the TCB part of K_prod)
runtime, planner, geo ◀── runtime_shell (adapters, controller, driver, API)  — never world  (checked)
core ◀── geo ◀── planner, world
json ◀── ptfeed
ontology, platform, studio ── Studio (option TWIN_BUILD_STUDIO)
```

## The semantic path of one observation

1. The PT emits `poi_arrived!`. Depending on the twin, this arrives from `twin-world` through the
   co-simulation step, or from `POST /runtime/pt-event`.
2. `PtAdapter` translates it to `target_reached!` using the label-equivalence relation E recorded
   in the package's alignment evidence. E is not written by hand.
3. `TwinSession::submit` asks the kernel for the successor. The call `kernel::observe` is pure and
   works on a value.
4. The session writes the ledger record: the input, the transition, the guard evaluation, the
   state before and after, and the propositions. Only then does it commit the kernel-computed
   state (write-ahead). If the ledger cannot be written, the session fails-stop.
5. Observers receive the `ledger` event over SSE, with the same hash. Studio relays the stream to
   the UI.

`tests/e2e/drone_mission_test.cpp` (test `TraceabilityFromSimulatorEventToLedgerAndStream`)
checks this chain end to end.

## Decisions vs observations

The mission controller and the planner are **untrusted proposers**. A route is selected in a
*planning episode*:

1. The planner proposes one candidate per profile.
2. The validator checks each candidate's **geometric feasibility** against the twin's *known* map.
3. The kernel checks each candidate's **behavioural admissibility** by simulating the plan's
   event schedule on a copy of the state.
4. The episode is recorded as a ledger `context` record.
5. Only then does the controller propose the decision event (`plan_accepted!`). The kernel decides
   it like any other input.

The controller never branches on location names; it uses only the model's event vocabulary and
the kernel's enabledness queries *(checked)*.

## Knowledge vs ground truth

The drone twin knows the building only through `/env/*` of the building-information service. That
service publishes the outdated facility plan, then onboard-sensing observations and facility
notices. The ground truth is served only on `/observer/*`, for visualisation. The runtime has no
client for it *(checked)*. The planner therefore plans on the twin's knowledge. In the demo it
initially routes through fire door FD-2, which the plan shows open, and it replans when sensing
reveals that the door is closed.

## Evidence

| Evidence | Where | Tamper-evidence |
|---|---|---|
| Alignment and compilation evidence | inside the Verified Twin Package (`evidence/`) | SHA-256 per file; manifest hash |
| Execution ledger | `<ledger-dir>/<session>.ledger.jsonl` | SHA-256 hash chain plus sequence numbers |
| Recorded telemetry | `<ledger-dir>/<session>.telemetry.jsonl` | none: observation data, anchored to ledger positions |
| Engineering audit | Studio database | hash chain |
| Artefact versions | Studio content-addressed store | content hashes |

## Where to read further

| Topic | Document |
|---|---|
| Runtime API (REST and SSE) | [`runtime-api.md`](runtime-api.md) |
| What must be trusted | [`trusted-computing-base.md`](trusted-computing-base.md) |
| Logical time | [`logical-time-model.md`](logical-time-model.md) |
| Accepted models | [`supported-model-fragment.md`](supported-model-fragment.md) and [`compiler-diagnostics.md`](compiler-diagnostics.md) |
| How the aligner is reused | [`existing-aligner-integration.md`](existing-aligner-integration.md) |
| Correctness argument | `proof/` (`make proof`) |
| API reference | `make docs` (Doxygen) |
