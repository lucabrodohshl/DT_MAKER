# Twin Blueprint Studio — Design

Status: implemented on branch `claude/wizardly-cray-e06bva` (2026-10-06).
Source request: "VERIFIED TWIN STUDIO — COMPLETE DIGITAL TWIN AUTHORING ENVIRONMENT + UX
REBUILD" (100 sections, referred to as §n). It evolves the Studio Mode design of 2026-10-05:
the formal tooling stays exactly as it is; Studio becomes the place where a customer builds the
*whole* twin — structure, world, data, presentation, behaviour, semantics, assurance, tests,
release, instances and deployment — without editing repository files.

## 1. Core model (§1, §2)

```
Twin Blueprint  (blueprints)                         reusable engineering definition
 └─ Blueprint Version (blueprint_versions)           v1 published · v2 draft …  (immutable once published)
     ├─ document  twin-blueprint/1                   non-formal sections (JSON, versioned, hashed per section)
     │    identity · structure · world · data · connectivity · presentation · behavior(layout)
     │    · assurance · simulation · scenarios
     └─ pins  {pt_model, dt_model, ontology, pt_interpretation, dt_interpretation} → artefact versions
          (existing artefact store: lifecycle, evidence, impact analysis — unchanged)
Twin Instance  (twins + instance columns)            Drone-01, Drone-02 … each with identity, placement,
                                                     bindings, deployment target, runtime, ledger
```

The four model families are separate documents and are never collapsed into one graph:

| Model | Answers | Where |
|---|---|---|
| Asset / structural | what exists | `structure` (asset types, assets, typed relationships) |
| World / context | where and how it is arranged | `world` (twin-world/1: layers, objects, geometry, topology) |
| Behavioural | how it may behave | PT / DT timed automata (formal artefacts, twin-ta/1) |
| Semantic | what observations and states mean | ontology K, interpretations I_P / I_D (formal artefacts) |

They reference each other by id (a world object may bind to an asset; a telemetry item names
its owning asset and an ontology symbol; an event names its PT/DT labels), but a wall is never a
location and a location is never an asset.

## 2. Formats

### 2.1 `twin-blueprint/1`

```jsonc
{
  "format": "twin-blueprint/1",
  "identity": {"name", "description", "domain", "icon", "tags", "modelId", "timeUnit", "ticksPerUnit",
               "runtimeMode": "monitor|cosimulation", "plugin": "drone|null"},
  "structure": {"root": "<asset id>",
                "assetTypes": [{"id","name","category","description","properties":[{"key","type","unit","default"}]}],
                "assets": [{"id","name","type","parent","scope":"instance|context","description","properties":{},"tags":[]}],
                "relationships": [{"id","source","type","target"}]},
  "world": { twin-world/1 (§2.2) },
  "data": {"properties": [...], "telemetry": [...], "events": [...], "commands": [...]},
  "connectivity": {"sources": [{"id","kind","name","config"}], "bindings": [{"id","target":{"kind","id"},"source","select","unit","timestamp","quality"}]},
  "presentation": {"displayName","icon","primaryView","plugin","keyTelemetry","importantAssets",
                   "importantPropositions","importantMonitors","importantPredictions","states","events","charts"},
  "behavior": {"pt": {"layout": twin-ta-layout/1}, "dt": {"layout": twin-ta-layout/1}},
  "assurance": {"requirements": [...], "monitors": [...], "alerts": [...]},     // twin-monitors/1
  "simulation": {"kind": "mobile-robot|event-script|none", ...},
  "scenarios": [ twin-scenario/2 … ]
}
```

Numbers that carry meaning (bounds, ranges, coordinates in the world) are decimal strings or
integers, never floating point, so section hashes are stable (`twin::json` canonical form).
Each section has its own SHA-256; impact analysis compares section hashes (§67).

### 2.2 `twin-world/1` (§14–§19)

A generic scene: `mode` (`spatial` | `topology` | `diagram`), `units`, `bounds`, `grid`
(size, snapping), `layers` and `objects`.

* **Layer**: `{id, name, role, visible, locked}` with `role` one of `shared` (both the physical
  ground truth and the twin's initial knowledge), `ground-truth` (physical/simulation truth only),
  `knowledge` (initial twin knowledge only), `event` (applied by scenario/simulation timeline
  events only), `annotation` (presentation only) and `background` (imported images — never
  semantic geometry, §18).
* **Object**: `{id, layer, kind, semanticType, name, geometry, properties, assetBinding, tags}`.
  `kind` is geometric (`point`, `line`, `polyline`, `polygon`, `rect`, `region`, `label`,
  `waypoint`, `zone`, `node`, `edge`, `connector`, `image`); `semanticType` is free text supplied by
  domain tool palettes (`wall`, `door`, `obstacle`, `tank`, `pipe`, `bus` …). The core schema
  hard-codes no domain vocabulary; templates supply tool palettes that create ordinary objects.
* **Topology**: `node` objects (`{x,y}`) and `edge` objects (`{from, to, directed}`) with typed
  labels; `group` via `properties.group`. Topology never becomes formal behaviour (§24).

The **mobile-robot simulation adapter** (`twin::scene`) gives meaning to a documented vocabulary
(`floor`, `wall`, `obstacle`, `door{state}`, `hazard`, `unknown`, `free`, `start`, `target`) and
rasterises layers into the occupancy grids of the existing drone simulator: ground truth = layers
`shared` + `ground-truth`, initial knowledge = `shared` + `knowledge`, later objects override
earlier ones, outside the floor is wall. The drone example's world is authored as vectors and a
test proves the rasterisation reproduces the previous hard-coded map cell for cell.

### 2.3 Observation model (§21–§22)

`simulation.observation`: sensor range, proximity range, update interval, observed semantic
types, unobservable layers, noise (`none` only is executable in this version; others are refused
as unsupported, never approximated), discovery (`on-sight`). It is simulator configuration, never
part of the trusted kernel: it is passed to `twin-world`, which publishes only what the sensor
model observes through the environment service; the DT never reads ground truth.

### 2.4 `twin-scenario/2` (§49–§56, §59)

`{id, name, description, start, steps[], horizon}`. Steps: `event` (formal label, `level`
`dt`|`pt`, absolute `at` or `delay`), `delay`, `observe` (telemetry values), `world` (environment
change: add/remove object, set property; never a formal transition by itself), `expect`
(location, proposition, transition, monitor, world condition). Formal steps are executed by the
semantic kernel (`twin::runtime::what_if` on the compiled DT); world steps by the scene model;
results are **tests**, never proofs.

## 3. Authority (§87, §96)

| Conclusion | Computed by |
|---|---|
| Section validity (VALID) | `twin::studio` blueprint validator (structure, references), `twin::scene` (world), `twin::authoring` (models), ontology services |
| Timing windows, legality of a scenario step, why-explanations | kernel (`enabling_window`, `explain_window`) through `runtime::what_if` on the compiled DT |
| Alignment (VERIFIED) | SemPTDTAlignmentICSE (C++/Z3), unchanged |
| Compilation, IR | compiler with translation validation, unchanged |
| Package integrity | `twin::package` verifier |
| Rasterised simulator world | `twin::scene` |
| Deployment state | supervisor (process table) |

The browser renders these results; it never computes them.

## 4. Verified Core Package vs Twin Deployment Bundle (§60–§61, §96)

* **Verified Core Package** = the existing `twin-package/1` (DT view, IR, ontology,
  interpretations, PT view, alignment evidence, compilation manifest, monitors, type metadata).
  This is the proof boundary.
* **Twin Deployment Bundle** `twin-bundle/1` = `bundle.json` (manifest: per-file SHA-256, the core
  package hash, a `verificationScope` table) + `blueprint.json` (the blueprint document) +
  `simulation/*.json` (generated simulator inputs). Integrity-protected, **not** formally verified.

## 5. Instances and deployment (§63–§66)

An instance is a `twins` row with `blueprint_id`, `blueprint_version`, `config`
(identity metadata, placement, source bindings, runtime options, target), `world_url`,
`desired_state`. Creating it instantiates the blueprint's `instance`-scoped assets under the
instance prefix (context assets are shared), creates telemetry channels from the data contract,
and never copies or edits formal artefacts (§64). Deploying runs the **supervisor**: it writes
the bundle's generated inputs, starts `twin-runtime` (monitor or co-simulation), `twin-world`
(mobile-robot simulation) or `twin-pt-feed` (event-script simulation) on allocated local ports,
waits for health, registers the URLs and starts the telemetry bridge, so the instance appears on
*Your Twins* immediately (§65). Instances whose desired state is `running` are restarted when
Studio starts. A runtime fail-stop is never restarted automatically.

## 6. HTTP API (Studio `/api/v1`, contract in `api/studio.openapi.yaml`)

| Route | Purpose |
|---|---|
| `GET/POST /blueprints`, `GET /blueprints/templates` | list, create (blank, template, clone, import bundle) |
| `GET /blueprints/{id}`, `GET /blueprints/{id}/versions/{v}` | blueprint, full version (document, pins, statuses) |
| `PUT /blueprints/{id}/versions/{v}/sections/{section}` | save a section (draft only, optimistic `revision`) |
| `POST /blueprints/{id}/versions/{v}/drafts` | new draft from a version (§66) |
| `GET/PUT /blueprints/{id}/versions/{v}/models/{pt|dt}`, `POST …/import` | canonical model + layout; UPPAAL import |
| `PUT /blueprints/{id}/versions/{v}/semantics/{role}` | ontology / interpretation content or pin |
| `GET /blueprints/{id}/versions/{v}/status` | per-section status, completeness, readiness gate |
| `POST /blueprints/{id}/versions/{v}/{validate|align|compile|package|publish}` | real checks, evidence |
| `POST /blueprints/{id}/versions/{v}/timing` | legal delay intervals and explanations (kernel) |
| `POST /blueprints/{id}/versions/{v}/scenarios/{sid}/run`, `…/scenarios/run` | scenario tests |
| `GET /blueprints/{id}/versions/{v}/world/raster` | rasterised simulator grids (what the simulator will load) |
| `GET /blueprints/{id}/versions/{v}/impact?against=` | section-level impact classification |
| `POST /blueprints/{id}/previews`, `/previews/{id}/…` | isolated preview runtime (STUDIO PREVIEW) |
| `GET/POST /instances`, `POST /instances/{id}/{deploy|stop|start}` | instances and deployment |
