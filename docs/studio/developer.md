# Developer guide

## Architecture

```
 browser ── web/studio (React, TypeScript) ──HTTP/SSE──▶ twin-studio (C++)
                                                           ├─ platform: SQLite (WAL, migrations) + content-addressed object store
                                                           │    assets · telemetry · artefact versions · evidence · packages ·
                                                           │    deployments · changes · engineering audit · app log
                                                           ├─ ontology: strict parser · validation · evaluation · diff ·
                                                           │    refinement (Def. 4, Z3) · Theorem 3 assessment
                                                           ├─ compiler / aligner (SemPTDTAlignmentICSE, unmodified)
                                                           ├─ blueprints: catalogue · sections · PT/DT models · validation ·
                                                           │    timing windows & scenarios (kernel) · previews · release gate ·
                                                           │    package + bundle · instances · live monitors
                                                           ├─ supervisor: deployed instances ──▶ twin-runtime / twin-world /
                                                           │    twin-pt-feed processes (pid files, health checks, fail-stop)
                                                           ├─ runtime bridge: ingests runtime telemetry (Last-Event-ID resume)
                                                           └─ proxy  /api/v1/twins/{id}/{runtime|simulation|planner|world|
                                                                     mission|observer|scenario}/…  ──▶ twin-runtime / twin-world
```

- **`twin-studio`** owns the engineering lifecycle and the platform data. It never executes
  behaviour itself: timing windows and scenarios run on the semantic kernel library, previews and
  deployed instances run `twin-runtime` processes under its supervisor.
- **Blueprints** (`BlueprintService`, `include/twin/studio/blueprints.hpp`) compose the existing
  authorities — validators, aligner, compiler, kernel, package verifier — and record evidence
  for exactly a version's inputs. Every mutating operation appends an engineering-audit record.
- **`twin-runtime`** runs the verified kernel for one twin. It owns the behavioural state, the
  execution ledger, prediction, simulation and replay. Studio reaches it only through the
  proxy, so the browser has a single origin.
- **`web/studio`** is a client. It contains no digital-twin semantics: no thresholds, no state
  rules, no refinement logic. Trust states the client cannot parse map to *Unknown*.

## Source layout

| Path | Content |
|---|---|
| `include/twin/ontology`, `src/ontology` | Strict `.ont`/`.interp` parser, validation, three-valued evaluation, structural diff, refinement checker |
| `include/twin/platform`, `src/platform` | Database, object store, artefacts and lifecycle, evidence and applicability, audit, assets, telemetry, twins/packages/deployments, app log |
| `include/twin/studio`, `src/studio` | Services (core, artefacts, checks, release, ops), HTTP server, SSE events, seed, runtime bridge |
| `src/studio/blueprints_*.cpp` | Blueprint service: catalogue and sections (`core`), validation and world rasterisation (`validate`), formal artefacts, alignment and compilation (`formal`), timing windows and scenarios (`test`), previews (`preview`), release gate, package, instances and deployment (`release`), live monitors (`monitors`) |
| `src/studio/supervisor.cpp` | Deployment supervisor of instance and preview processes |
| `apps/twin-studio` | `serve`, `seed`, `demo`, `version` |
| `tests/studio` | GoogleTest suites (ontology, platform, services and HTTP) |
| `web/studio/src/api` | Typed client, zod schemas for trust-relevant payloads, TanStack Query hooks |
| `web/studio/src/runtime` | Runtime API types, client and stream client, replay accessors, exact logical time |
| `web/studio/src/live` | Studio SSE stream (sequence, epoch, gap detection, resync) |
| `web/studio/src/design` | Design system: tokens, components, tables, graph layout |
| `web/studio/src/features` | One folder per product area |
| `web/studio/src/features/blueprint` | The Blueprint workspace: pages per section, the timed-automaton canvas (`ta/`), the world editor (`world/`), semantics editors, the Scenario Builder (`test/`), the guided wizard, undo/redo and reference-following renames |
| `web/studio/src/features/studio` | Studio home and navigation |
| `web/studio/src/plugins` | Plugin contract, registry and the drone reference plugin |
| `examples/*` | The drone, pump and thermal-chamber examples (Blueprint documents, models, ontologies, interpretations, evolutions, seed data) |
| `examples/templates` | Blueprint templates offered by *From template* |

## Building

Studio is opt-in (`-DTWIN_BUILD_STUDIO=ON`). The presets enable it:

```bash
cmake --preset studio-release
```

```bash
cmake --build --preset studio-release
```

```bash
cd web/studio && npm ci && npm run build
```

`twin-studio serve --web-root web/studio/dist` serves the built UI. In development, run
`npm run dev` in `web/studio`; Vite proxies `/api` to `TWIN_STUDIO_URL` (default
`http://127.0.0.1:8080`).

## Documentation website

The manual in `docs/studio/*.md` is the single source. `python3 scripts/docs/build_site.py`
(also run by `npm run build` and `make docs-site`) renders it into a static website in
`web/studio/dist/docs`. The site has:
- sidebar navigation, full-text search and a table of contents per page
- a screenshot tour and click-to-enlarge screenshots
- light and dark themes

`twin-studio` serves it at `/docs/`, and the app's **Help** link and the *Learn more* links on
key pages point into it. The site uses relative links only, so it also works when opened
from disk. It needs Python-Markdown (`pip install markdown`).

## API contract

- **Studio API**: [`api/studio.openapi.yaml`](../../api/studio.openapi.yaml), served under
  `/api/v1`. Errors have the shape `{error: {code, message, context: [{key, value}]}}`. The
  HTTP status codes are:
  - 400: invalid argument, parse error, time not representable
  - 404: not found
  - 409: lifecycle state or integrity conflict, such as publishing an unvalidated draft
  - 422: rejected by validation or by the kernel (invariant, transition not enabled, time
    regression, …)
  - 503: an upstream runtime is unavailable
  - 500: I/O or internal error
  The UI shows the message and the code-specific explanation; technical context is shown only
  on request.
- **Contract checks**: the component schemas of `api/studio.openapi.yaml` are generated from the
  client's types in `web/studio/src/api/types.ts` (`python3 scripts/openapi/studio_components.py`;
  `--check` fails when the file is out of date), and
  `STUDIO_URL=… python3 scripts/openapi/check_studio_contract.py` calls every documented `GET`
  operation of a running server, with identifiers discovered from it, and validates the
  responses against the specification (Blueprints, versions, sections, previews, instances and
  their monitors included). `make api-check` runs both.
- **Runtime API** (through the proxy): [`api/runtime.openapi.yaml`](../../api/runtime.openapi.yaml)
  and [`../runtime-api.md`](../runtime-api.md).
- **Live events**: `GET /api/v1/stream`, SSE with `seq`, server `epoch` and `lastEventId`
  resume. A `resync` event, or a new epoch, tells clients to refetch.
- **Schema evolution** is explicit:
  - The API is versioned in the path (`/api/v1`).
  - Evidence documents carry a format id (`twin-refinement-evidence/1`, …).
  - The database has numbered migrations.
  - The client validates trust-relevant payloads with zod and refuses to render a verdict from
    a document it cannot read.

## Testing

| Suite | Command | Covers |
|---|---|---|
| C++ unit and integration | `ctest --test-dir build/studio-release -L studio` | Parser, validation, evaluation, diff, refinement corpus (CS1–CS8, drone, pump), platform repositories, lifecycle, applicability, audit chain, services, HTTP API, proxy, SSE; Blueprints (`studio_blueprint_tests`): creation, sections and revisions, validation, the drone and pump Blueprints end to end, timing windows and scenarios, release gate, package and bundle, instances, deployment, previews, live monitors |
| Frontend unit and component | `cd web/studio && npm test` | Stream ordering/gaps/pause, logical time, prediction tree, telemetry gaps/quality/CSV, schemas, graph layout, replay accessors; trust badges, error states, refinement verdicts, publish gating and 409, diagnostics, rollback reason, audit-chain failure (API mocked with MSW, in tests only) |
| End-to-end | `cd web/studio && npm run e2e` (against a running stack; `STUDIO_URL` to point elsewhere) | Asset → telemetry → behaviour → *Why?* → prediction → ledger verification → replay → ledger export; the ontology journey (draft, diagnostics, validate, refinement, impact, publish gating); the drone integration; `blueprint-studio.spec.ts`: a thermal-chamber twin built from scratch to a deployed, monitored instance, the drone preview, the Scenario Builder refusing an illegal step |
| Screenshots | `scripts/capture-screenshots.sh` | Regenerates `docs/screenshots/` (including both tutorials and the Studio tour: `scripts/screenshots/first-twin.mjs`, `scripts/screenshots/studio.mjs`) from an isolated stack |

If `npx playwright install` is not possible, set `PLAYWRIGHT_CHROMIUM` to an existing Chromium
binary.

## Security notes

- **Bind address.** `twin-studio` listens on `127.0.0.1` by default (`--host` to change).
  There is no authentication in this version. Expose it only behind an authenticating reverse
  proxy that enforces roles server-side.
- **Actor.** `X-Twin-Actor` attributes engineering actions in the audit trail. It is
  self-declared and **not** an access-control mechanism. The UI's Operator/Engineer detail level
  is presentation only.
- **No secrets in the UI or logs.** Structured log fields whose names look like credentials
  (password, secret, token, credential, authorization, api_key, private_key, cookie) are redacted before they are written. The UI shows hashes,
  never key material.
- **Integrity, not secrecy.** Ledgers and the engineering audit are hash-chained
  (tamper-evident), and evidence documents and packages are content-addressed and re-verified
  on read. This detects modification; it does not prevent it. Back up the data directory.
- **Input handling.** Formulas and models are parsed by the backend's strict parser and the
  aligner, never evaluated in the browser. Formal checks run on a dedicated large-stack thread,
  with per-query Z3 timeouts.
