# Integration audit

This audit inventories what the two workstreams built, where it lives and how complete it is. It
covers the kernel/runtime/drone workstream and the Studio/platform/GUI workstream. It also lists
the contract mismatches found while connecting them and how each one was resolved.

The audit was written during integration rather than strictly before it: the first
reconciliations (runtime contract, proxy families, example format) happened in parallel with this
inventory. Each mismatch below records its resolution and the place where it was resolved.

## 1. Components

| Component | Location | State |
|---|---|---|
| Semantic aligner (SemPTDTAlignmentICSE) | `SemPTDTAlignmentICSE/` (unmodified), built by `cmake/Aligner.cmake` | Complete. The source digest is recorded in every package. |
| DT → Twin IR compiler | `src/compiler`, `include/twin/compiler`, CLI `twin compile` | Complete. Translation-validated against the aligner (DBM equality). All 36 corpus views compile deterministically. |
| Twin IR (canonical JSON) | `src/ir` | Complete. Round-trip checked, hashed. |
| Semantic kernel K_sem | `src/kernel` | Complete. Pure functions. Property tests check enabling windows and deadlines against brute force. |
| Production runtime K_prod | `src/runtime`, `apps/twin-runtime` | Complete: session (TCB), co-simulation host, monitor host, PT adapter (E), mission controller (planning episodes), validator, executions and package registry, REST and SSE API. |
| Tamper-evident ledger, verification, replay | `src/ledger`, CLI `twin ledger …`, `twin replay` | Complete, including `context` records and replay frames. |
| Verified Twin Package | `src/package`, CLI `twin package …` | Complete. About 50 checks; the runtime refuses unverified packages. |
| Alignment wrapper and evidence | `src/alignment`, CLI `twin align` | Complete. Evidence format `twin-alignment-evidence/1`. |
| Prediction | `kernel::explore`, `kernel::simulate`; `POST /runtime/predict`, `POST /runtime/simulate` | Complete. Works on copies. |
| Path planner (untrusted) | `src/planner` | Complete. A*, three profiles per planning episode. |
| Drone Physical Twin and evolving-map API | `src/world`, `apps/twin-world` (`WorldService`) | Complete. Ground truth vs published knowledge; sensing; facility notices. |
| Scripted Physical Twin (pump PLC) | `src/ptfeed`, `apps/twin-pt-feed`, `scenarios/pump_operating_cycle.json` | Complete. |
| Ontology services | `src/ontology` | Complete: strict parser, validation, refinement (Def. 4), preservation (Thm. 3), three-valued evaluation (Z3). |
| Platform persistence | `src/platform` (SQLite plus content-addressed store) | Complete: artefact versions and lifecycle, evidence, audit chain, assets and relationships, telemetry with `logical_ticks`. |
| Studio application and HTTP API | `src/studio`, `apps/twin-studio` | Complete: `/api/v1/*`, runtime proxy, runtime telemetry bridge, seeding. |
| GUI (Verified Twin Studio) | `web/studio` (React 19, TypeScript, Vite) | Complete: every navigation section and the plugin host. |
| Drone visualisation plugin | `web/studio/src/plugins/drone` | Complete: live and replay. |
| Examples | `examples/indoor-drone`, `examples/industrial-pump`, `models/indoor_drone`, `scenarios/` | Complete. |
| One-command start | `make demo` → `scripts/start-demo.sh` | Complete. |
| Correctness proof | `proof/` (LaTeX, handwritten) | Complete. Not mechanised. |

## 2. Persistence

| Information | Store | Survives restart |
|---|---|---|
| Assets, relationships | Studio `studio.db` | yes |
| Telemetry history (observed, ingested, logical) | Studio `studio.db` (`telemetry_samples`) | yes |
| Ontology, interpretation and model versions; drafts | Studio `studio.db` + `objects/` (content-addressed) | yes; published versions are immutable |
| Alignment, refinement, validation, compile and package evidence | Studio `studio.db` (`evidence`) | yes |
| Compiled IR, verified packages | `<data>/packages/PKG-*` (and inside every package) | yes |
| Deployments, engineering audit trail | Studio `studio.db` (hash-chained audit) | yes |
| Execution ledgers, recorded telemetry | `<data>/ledgers/{drone,pump}/*.jsonl` | yes; listed and replayable after restart |

## 3. APIs in use

| Interface | Contract | Consumers |
|---|---|---|
| Studio platform API `/api/v1/*` | `src/studio/server.cpp` (Studio OpenAPI) | web UI |
| Runtime API (REST and SSE) | [`runtime-api.md`](runtime-api.md), authoritative | Studio proxy `/api/v1/twins/{id}/{runtime\|simulation\|planner\|mission\|world}/…`, Studio telemetry bridge |
| World API | `include/twin/world/service.hpp` (route table) | runtime `WorldPort` (`/env`, `/pt` only); Studio proxy `/api/v1/twins/{id}/{observer\|scenario}/…` (visualisation only) |
| Package format | `include/twin/package/package.hpp` (`twin-package/1`) | runtime, Studio (build, verify, deploy) |
| Ledger format | `include/twin/ledger/record.hpp` (`twin-ledger/1`) | runtime, replay, UI (records displayed verbatim) |

## 4. Mocks and duplicated truth

- The frontend's MSW handlers and fixtures exist only in tests (`web/studio/src/test/*`,
  `*.test.tsx`). No production screen imports them. The running application talks to the real
  Studio API, which proxies the real runtimes.
- No production screen hard-codes `VERIFIED`, `ALIGNED`, `CONFORMANT`, `LEDGER VALID`,
  `REFINEMENT PASS` or `PACKAGE VALID`. Every trust badge renders evidence returned by the
  backend. Conformance is computed by the runtime from kernel verdicts; ledger validity by
  `POST /runtime/ledger/verify`; package integrity by the package verifier; alignment and
  refinement by the aligner and the ontology checker.
- `tests/architecture` checks that neither the mission controller, the planner nor the drone
  plugin contains the twin's location names: there is no shadow state machine. Display emphasis
  comes from the twin's presentation metadata.

## 5. Mismatches found and how they were resolved

| # | Mismatch | Resolution |
|---|---|---|
| 1 | Proxy family `world` pointed at the simulator when `--world` was given, which hid the twin's *known* map. | `world` always routes to the runtime (twin knowledge); new families `observer` and `scenario` route to the simulator (ground truth). |
| 2 | The error `context` was an object in the runtime and an array in Studio. | The runtime uses `[{key, value}]` everywhere. |
| 3 | With `--paused` the mission never started: nothing issued the operator's start request. | `POST /simulation/start` requests the mission start (idempotent); `POST /mission/start` added. |
| 4 | Seeding failed for runtime-fed channels without a history profile. | Studio seeds history only for channels with a profile. |
| 5 | The telemetry bridge re-ingested the replay buffer on reconnect and dropped logical time. | Resume with `Last-Event-ID`; detect restarts; `logical_ticks` column (schema v2). |
| 6 | Pump telemetry came from a Studio-side generator while the twin state came from elsewhere, so they could contradict each other. | The pump runtime runs in monitor mode. `twin-pt-feed` emits *consistent* events and telemetry, and Studio persists the runtime-fed channels. |
| 7 | `GET /runtime/executions/{session}` lacked `replayable` and `current`. | Added. |
| 8 | The Studio replay page guessed the replay frame fields. | The per-kind frame table is documented in `runtime-api.md`; the Studio replay page maps the documented fields. |
| 9 | The drone plugin's replay timeline read `package.hash`, which replay frames do not carry. | Fixed in the plugin. |
| 10 | The knowledge graph and behaviour graph rendered small, with an empty minimap. | Studio graph layout fixed. |

## 6. Integration work (all done)

1. Runtime contract and documentation (`runtime-api.md`); Studio proxy and types aligned to it.
2. Examples in the Studio seed format (`examples/indoor-drone`, `examples/industrial-pump`).
   Seeding runs the real validation, alignment, compilation and packaging.
3. Runtimes started on exactly the packages that Studio deployed (`start-demo.sh`), so hashes
   agree everywhere.
4. Runtime telemetry persisted by Studio with three time bases.
5. Drone plugin inside the Studio shell (live and replay); Replay Mission entry point.
6. Pump twin executed by a real runtime (monitor mode) so every generic screen works for it.
7. One-command start, end-to-end tests, screenshots from the running product, documentation.
