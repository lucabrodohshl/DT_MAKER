# Final integration report

**Product:** Verified Twin Studio. One running product: formal tooling, verified execution engine,
platform and GUI.
**Start:** `make demo`, then http://127.0.0.1:8080. See the [demo script](demo-script.md).

## 1. Components integrated

| Layer | Components |
|---|---|
| Formal tooling | SemPTDTAlignmentICSE (unmodified): semantic alignment of V_P and V_D under Φ (Z3, UDBM, UTAP); ontology services (strict parsing, validation, refinement per Def. 4, alignment preservation per Thm. 3, three-valued interpretation evaluation) |
| Toolchain | DT → Twin IR compiler (translation-validated); alignment wrapper and evidence; Verified Twin Package builder and verifier; `twin` CLI |
| Execution | Semantic kernel K_sem; production runtime K_prod in co-simulation and monitor modes; tamper-evident ledger with verification, tamper drill and replay (with frames) |
| Decision support | Prediction (bounded exploration on copies); what-if simulation; planning episodes (A* candidates, an independent geometric validator, kernel admissibility) |
| Physical side | Drone simulator and building-information service with ground truth vs published knowledge (`twin-world`); scripted pump control system (`twin-pt-feed`) |
| Platform | Assets and knowledge graph; telemetry storage and streaming (observed, ingested and logical time); content-addressed artefact store with version lifecycle; evidence registry; packages and deployments; hash-chained engineering audit; change workspaces and release pipeline |
| GUI | Verified Twin Studio (Overview, Assets, Operations, Behaviour, Predict, Audit, Engineering, Maintenance); domain plugin host; drone mission plugin |

## 2. Architecture

See [architecture.md](architecture.md). The formal chain is preserved end to end:

```
formal model ─▶ compiler ─▶ IR ─▶ semantic kernel ─▶ production runtime ─▶ API ─▶ GUI
```

- Only `TwinSession` changes semantic state, and only to kernel-computed values. The planner, the
  mission controller, the simulators, Studio and the browser hold no write path (checked by
  `tests/architecture`).
- The drone twin plans on its **known** world. The ground truth is reachable only through the
  simulator's observer API, for visualisation, and the runtime has no client for it (checked).
- Replay always uses the package recorded in the ledger. If that package is not available, replay
  refuses with a 404 rather than reinterpreting the execution.

## 3. APIs actually used

| From → to | API |
|---|---|
| Browser → Studio | `/api/v1/*` (assets, graph, telemetry, artefacts and versions, evidence, refinement checks, impact, changes and pipeline, packages, deployments, audit, logs, search, overview); SSE `/api/v1/stream` |
| Browser → runtime (via the Studio proxy) | `/api/v1/twins/{id}/{runtime,simulation,planner,mission,world}/…`, matching [runtime-api.md](runtime-api.md) one to one, including SSE `/runtime/stream` |
| Browser → simulator (via the Studio proxy, visualisation only) | `/api/v1/twins/{id}/{observer,scenario}/…` |
| Studio → runtime | runtime SSE (telemetry bridge; `Last-Event-ID` resume) |
| Studio → toolchain | in-process: `twin::compiler`, `twin::alignment`, `twin::package` (seed, pipeline stages, deploy) |
| Runtime → drone simulator | `/env/*` (map knowledge, updates, mission), `/pt/*` (commands, co-simulation step) |
| PLC feed → pump runtime | `POST /runtime/pt-event`, `POST /runtime/telemetry` |

## 4. Formal guarantees

In logical time: `V_D ≅ IR(V_D) ≅ K_sem(IR(V_D)) ~weak K_prod(IR(V_D))`, composed with the
aligner's `V_P ~Φ V_D` (proof, section 8).

- **Isomorphism of the compiled model.** Translation validation compares the IR with the aligner's
  reading of the same document, including guards and invariants as DBMs.
- **Kernel semantics.** The proof covers the window lemma, monitoring over state sets and the
  delay/step rules. Property tests compare the windows and deadlines with brute force on random
  automata.
- **Production bisimulation.** Single mutation path, write-ahead ledger, fail-stop.
- **Evidence integrity.** SHA-256 package manifests; a hash-chained execution ledger; a
  hash-chained engineering audit.

## 5. Known non-formal assumptions

- The aligner's verdict is trusted (it is reused unmodified; its known behaviours are documented
  and compensated for in [existing-aligner-integration.md](existing-aligner-integration.md) and
  `docs/studio/aligner-findings.md`).
- Planning admissibility simulates the plan's events at *estimated* arrival times. The kernel's
  verdict is exact for that schedule; the estimates are a modelling assumption (cruise speed and
  per-leg overhead).
- The PT adapter assumes that PT events arrive with correct logical timestamps.
- The drone and pump physical sides are simulated or scripted.
- Durability relies on `fsync`. Tamper evidence relies on SHA-256.

## 6. Remaining limitations

- No hard real-time claims; the proof is not mechanised.
- The ledger is tamper-evident, not immutable. External anchoring (`twin ledger anchor`) is
  manual.
- No authentication or authorisation in Studio. The role selector only adjusts disclosure depth.
- The supported model fragment is deliberately narrow: no data variables, urgency or broadcast
  ([supported-model-fragment.md](supported-model-fragment.md)).
- Formal checks (alignment, refinement) run synchronously within the request that triggers them.
  Large models therefore make that request slow.
- The drone's physics is mission-level (path following, battery, lidar line of sight), not
  aerodynamic.
- Screenshot capture uses a cached Chromium build (`PLAYWRIGHT_CHROMIUM`) because the installed
  Playwright expects a newer browser build. `npx playwright install chromium` removes this
  workaround.

## 7. Tests executed (all passing)

| Suite | Count | Command |
|---|---|---|
| C++ unit (core, IR, kernel incl. property tests, compiler fixtures, ledger, package, runtime incl. monitor mode, planner, world API, PT feed) | 147 | `ctest --test-dir build/release -L unit` |
| C++ integration: the whole aligner corpus compiles, validates and is deterministic | 1 (36 models) | `-L integration` |
| C++ architecture rules | 4 | `-L architecture` |
| C++ end-to-end drone mission, including traceability from simulator event to ledger and stream | 7 | `-L e2e` |
| Studio C++ (ontology, refinement, platform, API) | 52 | `ctest --test-dir build/studio-release -L studio` |
| Frontend unit (Vitest) | 37 | `cd web/studio && npm test` |
| Browser end-to-end against the running product (Playwright): drone mission and GUI traceability, ontology workflow, operations journey | 12 | `cd web/studio && npx playwright test` |
| macOS app smoke test (bundled stack starts, drone replans, pump conformant, Quit stops every component) | manual, scripted in this report's session | `make macos-app`, then launch |

## 8. Screenshots

All screenshots come from the running product, regenerated by `scripts/capture-screenshots.sh`
(isolated stack, deterministic mission stepping) into `docs/screenshots/`:

- `verified-twin-studio-drone-hero.png`
- `01-overview.png` … `13-pump-example.png`
- `tutorial/` (Studio tutorial)

## 9. Packaging

`make macos-app` builds `dist/macos/Verified Twin Studio.app` and `VerifiedTwinStudio-<version>.dmg`
(about 13 MB). The bundle contains the five executables, the bundled Z3 dylib (install names
rewritten; the build checks that no Homebrew path remains), the web UI, the examples and the
scenarios. A Cocoa launcher (`apps/macos/launcher.mm`) seeds the examples on first start,
starts the stack (each runtime on the package Studio deployed), opens the browser and stops
everything on Quit. The bundle is signed ad hoc. Distributing it to other Macs requires signing
with a Developer ID and notarisation; the commands are in the build script.

## 10. Documentation

- [README](../README.md)
- [docs/](.): architecture, runtime API, trusted computing base, logical time, supported model
  fragment, compiler diagnostics, aligner integration, integration audit, demo script
- `docs/studio/`: the Studio manual
- Machine-readable contracts: `api/runtime.openapi.yaml` (runtime), `api/studio.openapi.yaml` (Studio)
- API reference: `make docs` → `build/docs/html/index.html` (Doxygen, 0 warnings: every public
  declaration of the engine and of Studio is documented)
- Proof: `make proof` → `proof/build/main.pdf`
