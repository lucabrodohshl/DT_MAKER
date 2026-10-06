# Studio Mode — Design Specification

Status: draft for user review (2026-10-05)
Owners: two parallel workstreams, as approved by the user:
*Kernel session* (engine and execution side, and the model editors) and *GUI session* (platform
APIs for twin types and instances, and the Studio-mode UI). Section 10 lists who owns what.
Source request: "VERIFIED TWIN STUDIO / TWIN BUILDER — AUTHORING + VERIFICATION + DEPLOYMENT
INTEGRATION" (91 sections, referred to below as §n). Appendix A maps every section to this design.

## 0. Summary

The product gets a second mode. **OPERATE** is what exists today. **STUDIO** is where users author
and evolve twins. In Studio a user defines a **Twin Type**. A Twin Type contains:

* an asset and telemetry schema;
* the PT view and the DT view (imported, drawn, or written as text);
* the ontology and the two interpretations;
* monitors and scenarios.

The user then verifies the alignment, compiles the DT view, releases a **Verified Twin Package**,
creates **Twin Instances**, binds their data sources and deploys them. Deployment actually starts
the instance's runtime and data adapters, and "Open in Operate" shows the new twin on the
existing generic screens with no configuration.

Five principles carry the design:

1. **One timed-model semantics.** Every authoring path produces one *canonical model*. Its
   deterministic UPPAAL rendering is the only thing the unmodified aligner and the existing
   compiler read. The compiled IR is what the kernel executes, so the user's DT view is exactly
   what runs.
2. **Formal conclusions only from formal tools.** The aligner, the compiler with translation
   validation, the zone graph, Z3 and the kernel are the only sources. The browser never parses,
   reasons or decides. VALID means the structure is correct; VERIFIED means a formal check
   passed. A suggestion is never an equivalence, and a test is never a proof.
3. **Reuse.** The artefact lifecycle, evidence, packages, deployments, audit, ontology services,
   refinement, diff and impact analysis already exist. Studio Mode composes them; it does not
   duplicate them.
4. **Deployment is real.** A local supervisor starts `twin-runtime` and a new `twin-ingest`
   process for each instance, from the deployment record.
5. **Each monitor runs where its source of truth lives:**
   * conformance in the runtime's kernel;
   * semantic properties in Studio;
   * data quality in the ingestion layer;
   * alerts outside all of them.

## 1. Scope

### 1.1 What the user asked for (condensed)

* An OPERATE | STUDIO switch in the same application (§3). The selected twin keeps its context
  across the switch (§3, §83).
* Twin Types (reusable, versioned) and Twin Instances (identity, bindings, runtime state,
  ledger, deployment) as first-class concepts (§4).
* A Studio home, a non-linear 12-stage wizard and templates (§5–7).
* Asset definition, composition, telemetry schema and presentation, kept separate from formal
  semantics (§8–10, §57).
* Data bindings through adapters (MQTT, REST, OPC UA, simulator, replay), canonical observation
  mapping and connection tests. Protocol logic stays out of the kernel (§11–13, §39).
* PT and DT views by UPPAAL import, a visual editor or a text editor, all over ONE canonical
  model. Unsupported constructs are rejected, never approximated, and provenance is kept
  (§14–20, §75–77).
* Ontology selection and editing reusing the existing subsystem (§21–22), and interpretation
  mapping with suggestions labelled "Suggested" (§23–24).
* An alignment workspace that shows backend evidence, correspondences and actionable failures
  (§25–27).
* Four monitor families: conformance, property, data quality and alert policies, plus a
  requirement library. Monitorability comes from real backend capability (§28–33, §58–59).
* Compiling, an IR viewer, the package, release readiness, instances, deployment, editing a
  deployed twin through new versions, clone, import/export, preview on the real kernel, a
  scenario test builder, versioning, impact, release comparison, controlled rollout and
  rollback (§34–49, §60–64).
* Drafts, autosave, optimistic concurrency and history (§65–69). A Studio Guide with real
  screenshots and in-app help (§70–71, §89). The existing design system (§72–75).
* Typed authoring APIs and long-running jobs, with no fake verification (§78–81).
* The drone and the pump as genuine Studio Twin Types (§50–51). Acceptance journeys: a twin
  built from scratch (Thermal Chamber), a UPPAAL import, a change to an existing twin, and
  monitor creation (§52–53, §85–88).

### 1.2 Assumptions (confirmed or to be corrected in review)

* **Deployment target:** this machine only in this version (the "local runtime target"). The
  supervisor is behind an interface, so a remote node agent can be added later.
* **Adapters with working implementations:** simulator (scripted source), replay (file), REST
  polling and MQTT 3.1.1 (a client we write; tested against an in-test broker).
* **OPC UA:** bindings can be defined and validated, but connection tests and deployment report
  `adapter_unavailable`, because no OPC UA library is installed. An optional build flag for
  open62541 is the extension point.
* **Properties:** only the forms in §4.4 are checkable. Everything else is shown as
  *unsupported*, never approximated.
* **Authentication:** still none (unchanged).
* **Strong alignment:** the aligner decides *weak* semantic alignment only. Strong alignment is
  reported only when neither view has internal (τ) transitions. Weak and strong timed
  bisimulation coincide then, and the argument goes into the proof appendix. Otherwise the
  strong mode is shown as "not decidable by this checker version".

### 1.3 Non-goals for this version

* No online marketplace (§84). We provide the local library, templates, clone and
  import/export bundles.
* No new formal importers beyond UPPAAL XML and the native formats (§20, §44). The importer
  framework is ready for them.
* No liveness model checking (`A<>`, `-->`, `E[]`) and no runtime monitoring of arbitrary TCTL
  (§30).
* No physical real-time claims (unchanged).

## 2. Concepts and vocabulary

| Term (UI) | Meaning | Formal object |
|---|---|---|
| Twin Type | Reusable definition, e.g. *CentrifugalPump v4* | Bundle of pinned artefact versions |
| Twin Type Version | `v4` (released, immutable) or `v5-draft` (editable) | Lifecycle record + pins |
| Twin Instance | A deployed or deployable twin of one physical asset, e.g. *Pump-017* | Existing `twins` record, extended |
| Physical System Specification (PT View) | A formal behavioural specification of the physical system, used to verify the DT view | V_P (timed automaton) |
| Digital Twin View (DT View) | The behaviour the twin executes; compiled, never hand-coded | V_D (timed automaton) |
| Ontology | Domain theory | K = (S, F, R, Δ) |
| Interpretations | The meaning of PT and DT states and events | I_P, I_D |
| Alignment | Semantic weak timed bisimulation modulo the ontology | V_P ∼Φ V_D |
| Twin IR | Compiled, hashed executable form of the DT View | IR(V_D) |
| Verified Twin Package | Immutable bundle of all of the above plus evidence | `twin-package/1` |
| Monitor | Something that watches a deployed twin (four kinds, §4.4) | — |
| Scenario | Example trace with expectations: a *test*, never a proof | `twin-scenario/1` |
| Binding | How an abstract telemetry field or PT event gets its data | `twin-bindings/1` |
| VALID | Structurally correct (parses, references resolve, inside the supported fragment) | — |
| VERIFIED | A formal check passed (alignment, translation validation, property check, refinement) | evidence record |
| Suggested | A heuristic proposal from name similarity or history. Never shown as equivalence. | — |

The existing artefact lifecycle has a state named `verified` that means "validated". The UI shows
it as **Validated** so that VERIFIED keeps meaning formal evidence (§65). The stored value is
unchanged, for compatibility.

## 3. Architecture

### 3.1 Processes

```
 Browser ── /api/v1 ──▶ twin-studio  (platform: types, instances, artefacts, jobs, evidence, packages,
                         │            deployments, audit, monitors aggregation, alerts; engine services
                         │            in-process: authoring, compile, align, properties, sandbox, package)
                         │
                         ├─ Deployment Supervisor (in-process) ── spawns / stops / watches, per instance:
                         │        ├── twin-runtime  (package → session → kernel → ledger; monitor or co-simulation mode)
                         │        └── twin-ingest   (adapters → canonical observations → runtime; data-quality monitors)
                         │
                         ├─ runtime proxy  /twins/{instance}/{runtime,simulation,planner,mission,world}/…
                         └─ ingest proxy   /twins/{instance}/ingest/…           (new)

 twin-world (drone physical system simulator) and external sources (MQTT broker, REST endpoint,
 OPC UA server, files) are the "physical side". `make demo` and the Mac app start twin-studio and
 the simulators; Studio starts every instance's runtime and ingestion from its deployment record.
```

### 3.2 One semantics: the model pipeline

```
 UPPAAL XML ──import (UTAP strict reader)──┐
 Diagram editor (structure edits) ─────────┼──▶ Canonical model  twin-ta/1  (artefact content)
 Text editor (TwinTA source) ──parse───────┘          │  + layout sidecar (presentation only)
                                                      │
                                     toolchain rendering (deterministic UPPAAL XML, fragment only)
                                                      │  semantic digest = SHA-256 of these bytes
                          ┌───────────────────────────┴─────────────────────┐
                          ▼                                                 ▼
             unmodified SemPTDTAlignmentICSE                existing compiler (strict reading +
             (alignment, zone graph, Z3)                    translation validation against the
                                                            aligner's reading) ──▶ Twin IR ──▶ kernel
```

* Diagram and text are two views of the same canonical model. Neither has its own semantics. All
  parsing, formatting and validation happens in C++ (`twin::authoring`). The browser sends
  structure or text and renders what comes back (§18–19, §78).
* The compiler and the aligner keep reading UPPAAL XML, so translation validation and the proof
  chain V_D ≅ IR(V_D) ≅ K_sem ∼ K_prod are unchanged. V_D is now "the toolchain rendering of the
  canonical model".
* **Import preservation check.** On import, the original document and the rendering of the
  imported canonical model are both compiled. Their IRs (ignoring the source hash) must be equal,
  otherwise the import fails. This is the guarantee behind "round trip only where semantic
  preservation can be guaranteed" (§45).

### 3.3 Authority boundaries

| Conclusion | Computed by | Never by |
|---|---|---|
| Model structure (VALID) | `twin::authoring` validator + strict compiler reading | browser |
| Alignment, label equivalence E, location consistency | aligner + `twin::alignment` | browser, platform |
| Property verdicts (design time) | `twin::authoring::properties` (aligner zone graph, aligner Ontology/Z3) | browser |
| Compilation, IR | compiler | anyone else |
| Behavioural state, conformance, behavioural property monitors | kernel via `TwinSession` (runtime, sandbox) | Studio, browser, ingest |
| Semantic property monitor values | `twin::ontology` three-valued evaluator, in Studio | browser |
| Data quality | `twin::ingest` | kernel |
| Alerts | platform alert policies, consuming the results above | kernel |
| Package integrity | `twin::package` verifier | — |

The rules of the previous phases still hold. Only `TwinSession` mutates semantic state, the
planner and external analytics only *propose*, and ground truth stays on the simulator's
observer API. `tests/architecture` gets rules for the new modules: `twin::ingest` and
`twin::deploy` must not include kernel or session headers, and the browser contains no model
parser.

## 4. Formats (the contract between the two workstreams)

All formats are JSON with a `format` tag, encoded canonically with `twin::json` (sorted keys,
integers and strings only where hashed). JSON Schemas and references live in `docs/authoring/`
(Kernel session).

### 4.1 Canonical timed-automaton model — `twin-ta/1`

```json
{
  "format": "twin-ta/1",
  "name": "ProcessPumpDT",
  "note": "DT view of pump P-101 (reliability vocabulary).",
  "clocks":    [{"name": "t", "note": "time in the current mode"}],
  "constants": [{"name": "COOL_MIN", "value": 30, "note": ""}],
  "channels":  [{"name": "restart", "note": ""}, {"name": "cooling_complete", "note": ""}],
  "locations": [
    {"name": "STOPPED", "initial": true, "invariant": [], "note": ""},
    {"name": "COOLING", "invariant": [{"clock": "t", "op": "<=", "bound": 300}], "note": ""}
  ],
  "edges": [
    {"id": "e1", "source": "STOPPED", "target": "NORMAL",
     "sync": {"channel": "restart", "direction": "!"}, "guard": [], "resets": ["t"], "note": ""},
    {"id": "e6", "source": "COOLING", "target": "NORMAL",
     "sync": {"channel": "cooling_complete", "direction": "!"},
     "guard": [{"clock": "t", "op": ">=", "bound": "COOL_MIN"}], "resets": ["t"], "note": ""}
  ]
}
```

* An atom is `{clock, minus?, op, bound}`. `op` is one of `< <= == >= >`. `bound` is an integer
  or a constant name. `minus` (a diagonal constraint) is accepted only in invariants, as in the
  supported fragment. `sync: null` means the internal action τ.
* Names are identifiers and must not be UPPAAL keywords. Locations, clocks, channels and
  constants share no names. Edge ids are unique and stable: the editor keeps them, and the
  parser assigns `e<n>` only to new edges.
* Exactly one location is initial. The toolchain rendering emits it first (TWC034).
* **Content hash** = SHA-256 of the canonical JSON. This is the stored artefact content.
  **Semantic digest** = SHA-256 of the toolchain rendering. It excludes notes and layout, and is
  what the alignment evidence and the package bind. Platform staleness checks for model roles
  compare semantic digests (§6.6), so editing a note or moving a node never invalidates
  verification (§57).
* **Layout sidecar** `twin-ta-layout/1`. It holds location positions, edge bend points and label
  positions, keyed by location name and edge id. It is presentation only: stored next to the
  artefact version, mutable even for published versions, never hashed into evidence or
  packages.
* Existing artefacts whose content is UPPAAL XML remain valid (legacy content). "Open in editor"
  imports them into a new draft version in `twin-ta/1`.

### 4.2 Text format — TwinTA (`.tta`)

A lossless text form of `twin-ta/1` (notes included, layout excluded):

```
// DT view of pump P-101 (reliability vocabulary).
automaton ProcessPumpDT {
    clock t;                       // time in the current mode
    const COOL_MIN = 30;
    channel restart, cooling_complete, controlled_stop;

    initial location STOPPED;
    location NORMAL;
    location COOLING { invariant t <= 300; }

    edge e1: STOPPED -> NORMAL { sync restart!; reset t; }
    edge e6: COOLING -> NORMAL { guard t >= COOL_MIN; sync cooling_complete!; reset t; }
    edge e9: NORMAL -> STOPPING { sync controlled_stop!; reset t; }
}
```

```
model      = { comment } "automaton" IDENT "{" { decl } "}"
decl       = clockDecl | constDecl | chanDecl | locDecl | edgeDecl
clockDecl  = "clock" IDENT { "," IDENT } ";"
constDecl  = "const" IDENT "=" INT ";"
chanDecl   = "channel" IDENT { "," IDENT } ";"
locDecl    = [ "initial" ] "location" IDENT ( ";" | "{" { "invariant" constr ";" } "}" )
edgeDecl   = "edge" [ IDENT ":" ] IDENT "->" IDENT ( ";" | "{" { edgeItem } "}" )
edgeItem   = "guard" constr ";" | "sync" IDENT ( "!" | "?" ) ";" | "reset" IDENT { "," IDENT } ";"
constr     = "true" | atom { "&&" atom }
atom       = IDENT [ "-" IDENT ] OP ( INT | IDENT )        OP = "<" | "<=" | "==" | ">=" | ">"
```

* **Comments.** Comments (`//`, `/* */`) attach to the next declaration as its note. A comment
  that ends a block attaches to the block's owner. Formatting is *parse, then print*: it never
  drops a comment and never changes the semantic digest. A test checks this on the whole corpus.
* **Errors and hints.** `||`, `!`, `false`, non-zero resets and data variables are parse errors
  with the same hints as the compiler (TWC024/025/026/052/014).
* **Editor services.** The parser keeps source ranges, so diagnostics, go-to-symbol, find
  references and the outline come from the backend (`symbols` in the parse result).

### 4.3 UPPAAL XML import and export

* **Import** = detect, parse with the compiler's strict UTAP reader, then convert. Every
  unsupported construct is collected, not just the first. Each is reported with its construct,
  XML position (line and column where UTAP gives them, otherwise the element path), the reason
  (the TWC code and why the aligner would diverge) and the supported alternative (§15, §76).
  Nothing is approximated. A document with errors produces a report and **no** model; the user
  fixes the source or recreates the construct in the editor.
* **What is converted:**
  * locations, invariants, edges, guards, synchronisations, resets, clocks, channels and
    constants become the canonical model;
  * UPPAAL coordinates and nails become the layout sidecar;
  * `comments` labels become notes.
* **Provenance** (§77), stored with the version: original file name, original SHA-256, import
  time, importer id and version, options (e.g. legacy `<system>` handling), canonical content
  hash, semantic digest, and the preservation check result (§3.2).
* **Export** produces either the toolchain rendering, or the *exchange rendering* (adds
  coordinates and notes, for UPPAAL users). Before the file is offered, the export is re-imported
  and must give the same semantic digest (§45).
* **Importer framework** (`twin::authoring::Importer`): `detect`, `import` (→ model, layout,
  diagnostics, provenance), `id`, `version`. Registered importers: `uppaal-xml`, `twin-ta-json`
  and `twinta-text`. A future importer (statecharts, SCXML, SysML) must lower to `twin-ta/1`;
  anything it cannot lower soundly is reported as unsupported (§20).

### 4.4 Monitors and requirements — `twin-monitors/1` (artefact kind `monitors`)

```json
{
  "format": "twin-monitors/1",
  "requirements": [
    {"id": "REQ-S1", "title": "No uncontrolled trip", "category": "safety",
     "severity": "critical", "description": "…", "monitors": ["never-fault"]}
  ],
  "monitors": [
    {"id": "conformance", "kind": "conformance", "name": "Behavioural conformance",
     "severity": "critical", "events": "all", "unmatchedEvents": "record"},
    {"id": "never-fault", "kind": "property", "name": "Never tripped",
     "severity": "critical", "property": "A[] !FAULT"},
    {"id": "bearing-envelope", "kind": "property", "name": "Bearing temperature envelope",
     "severity": "warning", "property": "A[] sem((not (> bearing_temp bearing_temp_limit)))"},
    {"id": "temp-fresh", "kind": "data_quality", "name": "Bearing temperature freshness",
     "severity": "warning", "field": "bearing_temp", "check": "stale", "maxAgeSeconds": 30}
  ],
  "alerts": [
    {"id": "AL-1", "monitor": "conformance", "on": "violated", "severity": "critical",
     "message": "Behaviour deviates from the verified model"}
  ]
}
```

**Conformance** (§29). The runtime's existing conformance over state sets does the work. The
monitor configures:
* which PT events are monitored (`"all"` or a list);
* what happens to an event that E does not translate (`reject` it with a 4xx, or `record` it as
  an alarm);
* the severity.

**Property language** (§30). Precedence from loosest to tightest: `->`, `||`, `&&`, `!`.

```
property = "A[]" state | "E<>" state | "A<>" state | "E[]" state | state "-->" state
state    = state "->" state | state "||" state | state "&&" state | "!" state | "(" state ")"
         | "true" | "false" | LOCATION | CLOCK OP bound | CLOCK "-" CLOCK OP bound
         | "sem(" SMT-LIB2 formula over ontology symbols ")"
```

**Monitorability is decided by the backend** (`analyse_property`), never by the UI:

| Form | Design time | Runtime | Evaluated by |
|---|---|---|---|
| `A[] φ`, φ over locations/clocks | **checkable**: zone-graph reachability of ¬φ on the aligner's own zone graph of V_D (clock bounds must not exceed the clock's maximal constant in the model, otherwise *unsupported*) | **monitorable**: evaluated on every committed state set: *satisfied* (all configurations), *violated* (none), *inconclusive* (some) | runtime (ledgered, see below) |
| `E<> φ`, φ over locations/clocks | **checkable** (reachability) | not monitorable (a possibility, not a run property) | — |
| `A[] φ`, φ containing `sem(…)` and locations (no clocks) | **checkable as "guaranteed by the model"**: for every reachable location L, Δ ⊨ I_D(L) → φ_L, where φ_L is φ with each location atom replaced by its truth value at L (aligner `Ontology::entails`); otherwise lists the locations where φ is *not guaranteed* | **monitorable**: three-valued over current observations and the runtime's current locations | Studio |
| `A<>`, `E[]`, `-->`; `sem` mixed with clock atoms | unsupported in this version | unsupported | — |

* **Requirements** (§31). A requirement stores id, title, description, category, severity,
  linked monitors and an optional formal expression. Monitorability, verification status and
  associated model elements are *computed* (from `analyse_property` and evidence), never stored
  as facts.
* **Data quality** (§32). Checks: `stale` (max age), `missing` (required field absent),
  `invalid_type`, `out_of_range` (range from the telemetry schema or the monitor), `clock_regression`,
  `duplicate` and `disconnected`. These are integration health, never formal violations.
* **Alert policies** (§33). Each maps a monitor result (`violated`, `inconclusive`, `unknown`,
  `finding`) to an alert severity and message. They are evaluated by the platform, outside the
  kernel.
* **Ledgered verdicts.** The runtime records each verdict *change* of a runtime-evaluated
  property as a ledger `context` record (`topic: "monitor"`). Replay therefore reproduces them
  with the monitor version shipped in the package that the ledger names (§59).

### 4.5 Scenarios — `twin-scenario/1` (artefact kind `scenarios`, a list per type version)

```json
{"format": "twin-scenario/1", "id": "overheating", "name": "Pump overheating",
 "level": "pt",
 "steps": [
   {"at": "0",  "event": "start_cmd!"},
   {"at": "10", "observe": {"bearing_temp": 70}},
   {"at": "20", "observe": {"bearing_temp": 95}},
   {"at": "20", "event": "alarm_raise!"},
   {"at": "20", "expect": {"location": "DEGRADED"}},
   {"at": "20", "expect": {"transition": "NORMAL.condition_degraded!.DEGRADED"}},
   {"at": "21", "expect": {"proposition": {"at(DEGRADED)": "true"}}},
   {"at": "21", "expect": {"monitor": {"bearing-envelope": "violated"}}},
   {"at": "30", "expect": {"conformance": "conformant"}}
 ]}
```

* `level` says which vocabulary the events use. `pt` events go through E from the alignment
  evidence; `dt` labels are fed to the kernel directly.
* Times are logical, in model units as decimal strings.
* The results are *test results* (pass/fail per expectation, with the actual value). The UI
  labels them "Tests", never "proof" (§49).

### 4.6 Data bindings — `twin-bindings/1` (per instance) and canonical observations

```json
{
  "format": "twin-bindings/1",
  "sources": [
    {"id": "plc", "adapter": "mqtt", "config": {"host": "10.0.0.5", "port": 1883, "topics": ["plant/line4/pump17/#"]}},
    {"id": "sim", "adapter": "simulator", "config": {"scenario": {"…": "twin-pt-feed scenario"}, "speed": 1.0, "loop": true}},
    {"id": "api", "adapter": "rest", "config": {"url": "http://…/pump17", "periodMs": 1000}},
    {"id": "trace", "adapter": "replay", "config": {"file": "…/trace.jsonl", "speed": 2.0}},
    {"id": "opc", "adapter": "opcua", "config": {"endpoint": "opc.tcp://…"}}
  ],
  "observations": [
    {"field": "motor_temperature", "source": "plc",
     "select": {"topic": "plant/line4/pump17", "path": "payload.temperature"},
     "unit": {"from": "degF"}, "timestamp": {"path": "payload.ts", "format": "epoch_ms"},
     "quality": {"path": "payload.q", "good": ["GOOD"]}, "missing": "mark_missing"}
  ],
  "events": [
    {"label": "alarm_raise!", "source": "plc",
     "select": {"topic": "plant/line4/pump17/events", "path": "payload.code", "equals": "ALARM"}}
  ],
  "time": {"basis": "source", "late": "reject"}
}
```

* A **canonical observation** has the shape `{field, value, type, unit, observedMs, receivedMs,
  quality, source, raw}`. Datatype and unit validation, unit conversion (a small unit registry
  plus explicit `scale`/`offset`), missing values, timestamps and quality are all handled in
  `twin::ingest`, outside the semantic core (§13).
* **Events** are PT labels. They are selected from source messages, and the simulator adapter
  emits them directly. The kernel receives them in logical-time order. A late event follows the
  `time.late` policy: `reject` produces a `clock_regression` finding and is not submitted;
  `clamp` submits it at the current logical time and records a finding.
* OPC UA config validates, but reports `adapter_unavailable` (§1.2).

### 4.7 Package additions (backward compatible; manifest format unchanged)

| File | Role | Purpose |
|---|---|---|
| `model/dt_view.tta.json` | `dt_source_model` | Canonical DT model. Its toolchain rendering is byte-identical to `model/dt_view.xml` (checked at build) |
| `semantics/pt_view.tta.json` | `pt_source_model` | Canonical PT model (same check) |
| `monitors/monitors.json` | `monitors` | Monitor and alert definitions, versioned with the package (§59) |
| `evidence/properties.json` | `property_evidence` | Design-time property check results for this package's inputs |
| `meta/type.json` | `type_metadata` | Twin Type id, version, name, runtime mode, telemetry schema digest |

* The manifest already lists files generically, so older packages and loaders keep working.
* The runtime loads `monitors.json` if present. Without it, only the default conformance
  monitor applies.
* Presentation stays outside the package, so a presentation change never requires a new package
  (§57).

## 5. Engine services (Kernel session)

All APIs are C++ libraries used in-process by `twin-studio`, plus two executables. Every
function returns API-ready JSON or a structured error, like `Services`.

| Module | Responsibility |
|---|---|
| `twin::authoring` (new) | Canonical model types and codec. Validator (VALID, located `TWM0xx` diagnostics). TwinTA parser, printer and formatter with symbols. Toolchain and exchange renderings, semantic digest. Importer framework, UPPAAL importer, preservation check. Structural model diff (§62). Property analysis and design-time checks. Compile-from-canonical with an IR ↔ source map (§35). |
| `twin::alignment` (extended) | Diagnostics categories for the failure UX (§27), each linked to model elements: unmatched PT event, unmatched DT event, missing interpretation, state semantic inconsistency (no PT location consistent with a DT location), behavioural mismatch (the aligner's counterexample label pair, with the edges carrying those labels) and ontology-equivalence failure (which direction of I_P(a) ↔ I_D(b) fails, via `Ontology::entails`). Correspondence data for visualisation: E, the location-consistency table, the final relation size. Optional strong-mode derivation (§1.2). |
| `twin::package` (extended) | Build from canonical models plus monitors (§4.7); verification of the new roles; runtime compatibility report. |
| `twin::runtime` (extended) | Loads `monitors.json`. Applies conformance options. Evaluates behavioural `A[]` properties on committed state sets and ledgers verdict changes. `GET /runtime/monitors`. A new `--instance <id>` option records the instance in the genesis record and in status, because instances of one type share one package. |
| `twin::sandbox` (new, Studio build) | Preview sessions: the real `TwinSession` on a compiled draft, an isolated ledger under `sandbox/`, execution id `preview-…`, never listed with production executions (§46–47). Semantic evaluation via `twin::ontology`. Scenario runner (§48–49). |
| `twin::ingest` (new) + `twin-ingest` (new executable) | Adapters, mapper, data-quality monitors, forwarding to the runtime in logical-time order. Status API (`/health`, `/status`, `/stream` SSE of canonical observations and findings). Connection test (`test_binding`) used by Studio before deployment (§12). |
| `twin::deploy` (new) | Deployment supervisor (§5.1). |

Long computations (alignment, compilation, property checks, package build, scenario runs) take a
`Progress` callback (phase, fraction, log line). Job orchestration is platform work (§6.3). This
deliberately refines the split proposed in chat, where "jobs" sat with the Kernel session: the
computations stay in the engine, while job records, queues and SSE progress live with the
platform's persistence and event hub.

### 5.1 Deployment supervisor

* **Interface.** `Supervisor::start(InstanceSpec)`, `replace(InstanceSpec)` (upgrade or
  rollback: the old session ends with an `end` record and a new session starts on the new
  package), `stop(id, reason)`, `status()`, and an observer callback.
* `InstanceSpec` contains:
  * the instance id;
  * the package directory and hash;
  * the mode (`monitor` | `cosimulation`), with the world URL for co-simulation;
  * the bindings (monitor mode);
  * runtime options from the package's permitted set: speed, start paused, deterministic.
* **Per instance** it allocates stable ports (persisted, from a configurable range) and starts
  `twin-runtime` (`--package`, `--package-store`, ledger dir `instances/<id>/ledgers`) and, in
  monitor mode with bindings, `twin-ingest`. It waits for health, then reports the URLs. The
  platform uses the observer callback to register `runtime_url` and start the telemetry bridge,
  so Operate needs no manual configuration (§82).
* **Reconciliation.** When Studio starts, every instance whose latest deployment says *running*
  is started again. `make demo` and the Mac launcher stop starting twin runtimes themselves.
* **Failures.** A runtime fail-stop (ledger write failure) is *not* restarted automatically:
  the state is `failed` and an operator decides, because restarting would hide an integrity
  event. `twin-ingest` restarts with back-off. Every state change is audited.

## 6. Platform (GUI session)

The GUI session designs the details. These are the requirements the engine relies on.

1. **Twin Type and Type Version.**
   * **Identity:** id, name, description and tags.
   * **Version:** a number, a state (`draft` | `released` | `deprecated`), a parent version, and
     a revision counter for optimistic concurrency (§68).
   * **Pins:** artefact versions for `definition` (`twin-type/1`: asset properties,
     composition with pinned component type versions and a cycle check (§9), relationships,
     telemetry schema (§10), runtime mode, plugin id), `pt_model`, `dt_model`, `ontology`,
     `pt_interpretation`, `dt_interpretation`, `monitors` and `scenarios`, plus a `presentation`
     document that is not semantic (§57).
   * **Packages** are per released type version.
   * **Reuse:** the existing artefact lifecycle (immutable once published) and the change
     workspace / release pipeline.
2. **Instances** extend the existing `twins` record with:
   * the type and the deployed type version;
   * the asset (created or linked in the asset graph);
   * metadata;
   * bindings (`twin-bindings/1`, versioned per instance);
   * the runtime target (`local`), the permitted runtime options and the desired state.

   Instance creation never copies or edits the formal model (§38). The telemetry channels come
   from the type's telemetry schema, sourced `runtime:<instance>/telemetry#<field>`.
3. **Jobs** (§80):
   * kinds: validate, align, compile, properties, package, scenarios;
   * states: QUEUED → RUNNING → PASS | FAIL | ERROR;
   * persisted records with logs, progress events on `/api/v1/stream`, cancellation;
   * the user can navigate away and come back.

   Formal checks that run synchronously today move onto jobs.
4. **Release readiness** (§37), computed from evidence on every request:
   * asset schema VALID and telemetry schema VALID;
   * PT and DT models VALID (strict reading);
   * ontology validation and interpretation completeness (every location and synchronised label
     interpreted);
   * alignment PASS – WEAK/STRONG;
   * DT compilation (translation-validated);
   * monitors VALID, with every property analysed;
   * package integrity and runtime compatibility.

   Scenario tests are reported but never gate as proofs. The verdict is READY TO RELEASE or
   RELEASE BLOCKED, with each failing item's reason and a link to fix it.
5. **Release comparison** (§62). The candidate is compared item by item with the previous
   release: schema, telemetry, PT/DT model diff (`twin::authoring` structural diff), ontology
   diff (existing), interpretation diff, monitor diff, alignment result and package hashes.
   **Impact** (§61) is the existing dependency graph extended to types and instances
   (e.g. "14 instances use v4").
6. **Staleness.** For model roles, evidence applicability compares *semantic digests* (§4.1).
7. **Deployments** (§40, §63–64).
   * The deployment record carries the immutable package, the instance, the bindings hash, the
     runtime target and the kind (`deploy` | `upgrade` | `rollback`).
   * **Rollouts:** one instance, selected instances, staged (batch size, stop on failure) or
     all. A new type version never upgrades instances silently.
   * **Rollback** reuses the existing mechanism. Every deployment calls the supervisor.
8. **Drafts and history** (§66–69): save draft, safe autosave (never into published versions),
   save state shown, revision conflicts (409 with both revisions), and history in the
   engineering audit (draft created, model imported, ontology changed, interpretation changed,
   monitor added, alignment run, compilation run, package released, deployed, rolled back).
9. **Monitors in Operate** (§58). One aggregation per instance, combining:
   * runtime monitors (`/runtime/monitors`);
   * Studio-evaluated semantic properties;
   * `twin-ingest` data-quality state.

   Each result carries its definition, kind, current result, evidence, relevant state and the
   originating type and version. Semantic properties are re-evaluated whenever a bound telemetry
   value or the runtime's location changes (the telemetry bridge already receives both),
   throttled to once per second per monitor. Alert policies are evaluated here. Findings are
   stored with the monitor version that produced them (§59).
10. **Library and bundles** (§42–45, §84):
    * **Clone:** new ids and lineage, all evidence marked *not applicable* until re-run.
    * **Templates:** shipped definitions with no evidence.
    * **Bundles:** a deterministic type bundle `twin-type-bundle/1` (manifest plus artefact
      files with hashes) for export and import.
    * **Guided import:** artefacts can be imported in any order (PT and DT XML first, then
      ontology, interpretations and so on).
11. **External analytics** (§55). External components are registered on a type (name,
    endpoint, purpose). They read through the public API and can only *propose* events through
    the runtime's input path, where the kernel decides and the ledger records the source.
12. **HTTP API** (§79). The platform routes live in `api/studio.openapi.yaml`. The engine
    routes live in `api/studio-engine.openapi.yaml` (owned by the Kernel session) and are
    served by `src/studio/engine_routes.cpp`, which `server.cpp` registers with one call:
    * `POST /api/v1/authoring/models/{parse,validate,format,render,diff}`;
    * `POST /api/v1/authoring/import`;
    * `POST /api/v1/authoring/properties/analyse`;
    * `POST|GET|DELETE /api/v1/sandbox/previews[/{id}[/advance|event|observe|state|trace]]`;
    * `POST /api/v1/ingest/test`.

    The API-check script covers both files. Studio also proxies
    `/api/v1/twins/{instance}/ingest/…` to that instance's `twin-ingest`, the same way it
    proxies the runtime.

## 7. User interface (GUI session; model editors by the Kernel session)

1. **Modes.**
   * A header switch, OPERATE | STUDIO. OPERATE keeps Overview, Assets, Operations,
     Behaviour, Predict and Audit. STUDIO holds Studio Home, Twin Types, Instances,
     Engineering and Maintenance; the existing pages move here, so navigation is not
     duplicated.
   * Context carries over: an Operate asset page offers **View definition** (the deployed type
     version, read-only) and **Edit definition** (creates or opens `v<n+1>-draft`) (§3, §41,
     §83).
   * Studio offers **Open in Operate** for instances.
2. **Studio home** (§5):
   * lists of types, instances, drafts, recently modified, verification required, deployment
     ready and templates;
   * actions: New Twin Type, New Twin Instance, Import Model, Clone, Create from Template.
3. **Type workspace** (§74). The layout:
   * left: section navigation (Asset, Data, PT, DT, Ontology, Interpretations, Monitors,
     Scenarios, Verification, Package, Instances);
   * center: the editor;
   * right: inspector and diagnostics;
   * bottom drawer: job output, compiler output and impact.

   The 12-stage wizard (§6) is a guided overlay on the same sections. Users can jump between
   sections freely, and status is shown per section.
4. **Model editors (Kernel session)** — `web/studio/src/authoring/`:
   * `ModelEditor` has tabs **Diagram | Source | Validation | Properties** (§75).
   * **Diagram** (§17), based on `@xyflow/react`:
     * create, rename and delete locations; set the initial location; edit invariants;
     * create transitions; edit guard, sync and resets (text fields validated by the backend);
     * declare clocks, channels and constants;
     * pan, zoom, multi-select, keyboard shortcuts, undo/redo (command stack over canonical
       model edits), auto-layout (dagre) for imported or new elements, and a validation overlay.
   * **Source** (§18), based on the existing CodeMirror setup: TwinTA highlighting, debounced
     backend parse, diagnostics with navigation, search, go to symbol, find references, format.
     Both tabs edit the same canonical model through the backend (§19).
   * `ModelDiagram` is a read-only renderer, also used by the import preview, the alignment
     workspace and the IR viewer.
5. **Ontology** (§21–22). Reuse the existing ontology editor, validation, versions, refinement,
   diff and impact. Studio adds selection (use, pin a version, fork as draft, create, import).
6. **Interpretations** (§23–24):
   * PT and DT observables (locations and synchronised labels, from the canonical models) side
     by side;
   * formula editing with ontology-symbol autocomplete;
   * backend validation, missing-mapping indicators, references and source locations;
   * suggestions marked **Suggested** from name similarity, ontology symbols and earlier
     mappings.
7. **Alignment workspace** (§25–27):
   * the inputs (versions and hashes) and the requested mode;
   * **Run semantic alignment** as a job;
   * result, timing, hashes and checker version;
   * correspondences rendered from evidence (E and location consistency), drawn between the two
     `ModelDiagram`s;
   * failure categories from `twin::alignment` diagnostics, each linking to the PT/DT editor
     element, the ontology symbol or the interpretation entry.
8. **Monitors and requirements** (§28–33). Separate builders per kind. Each property shows the
   backend's analysis: design-time checkable / runtime monitorable / unsupported, with reasons.
   A design-time check runs as a job.
9. **Compile and IR viewer** (§34–35): compiler version, source digest, IR hash and diagnostics.
   The IR viewer is read-only, with states, clocks, transitions, guards, propositions, stable
   ids and the source map back to editor elements.
10. **Package and release** (§36–37, §62): contents, readiness checklist, comparison, release.
11. **Instances, bindings and deployment** (§38–40, §63–64):
    * the instance form;
    * the bindings editor per field and per event (adapter, selector, mapping);
    * **Test connection**: status, latest raw payload, canonical value, type, unit, timestamp,
      quality;
    * live preview from `twin-ingest`'s stream;
    * **Deploy**, then **Open in Operate**;
    * rollout and rollback screens.
12. **Preview and scenarios** (§46–49):
    * a **STUDIO PREVIEW** banner;
    * inject events, observations and time advances;
    * state, transitions, propositions and monitors come from the sandbox;
    * a scenario builder with expectations;
    * results labelled "Tests".
13. **Help** (§71). A `[?]` tooltip with a one-sentence definition and **Learn more** linking
    to the Studio Guide page.
14. **Visual language** (§73). The existing tokens and components. Industrial, restrained,
    professional.
15. **No formal logic in the browser** (§78, §81). Statuses come from persisted evidence and
    jobs. Tests enforce that no production code assigns formal outcomes literally (a lint rule
    plus an architecture test).

## 8. Examples and acceptance

1. **Drone and pump as Twin Types** (§50–51). Seeding goes through the same services a user
   would. Each example's models are imported (UPPAAL to canonical, with the preservation check),
   and it gets its ontology, interpretations, monitors, scenarios, definition and presentation
   (the drone references the `drone` plugin; the pump has none). The current twins become
   instances `Drone-01` and `P-101`, deployed through the supervisor.
2. **Thermal Chamber fixtures** (Kernel session). Telemetry `temperature` (real, °C) and
   `door_open` (boolean). A PT view, a DT view, a `thermal-chamber.ont` ontology and both
   interpretations, as UPPAAL files (`physical.xml`, `digital.xml`) for the import journey. A
   simulator scenario for the fake source. Monitors: `overheating` (semantic property),
   `temperature-fresh` (data quality) and `conformance`.
3. **Acceptance journeys** (§52–53, §85–88). Each has a C++ end-to-end test at the service
   level (Kernel session) and a Playwright test through the GUI (GUI session):
   * **A. Build from scratch:** template → Thermal Chamber → telemetry → PT and DT drawn in the
     diagram editor → ontology → mappings → overheating monitor → alignment PASS → compile →
     package → instance `Chamber-01` → simulator binding → deploy → Open in Operate. Operate
     shows live telemetry, formal state, propositions, the behaviour graph, monitor findings
     and the ledger.
   * **B. UPPAAL import:** upload `physical.xml` and `digital.xml` → both visualised → the
     unsupported-feature report (a variant with a data variable is rejected and the location is
     shown) → ontology → mappings → alignment → compile → package → deploy.
   * **C. Change a deployed twin:** Operate P-101 → Edit definition → `v2-draft` → apply a
     fixture-defined DT change that keeps weak alignment (inserting an internal step) →
     alignment shows STALE → re-run → compile → release → upgrade only P-101 → v1 history and
     executions are still replayable. A variant that breaks alignment shows RELEASE BLOCKED
     with the failure category and links.
   * **D. Monitors:** freshness, conformance and a semantic property on one deployment. Operate
     shows three monitor kinds, separately.

## 9. Documentation and screenshots

* **Studio Guide** (GUI session; the existing HTML docs site at `/docs/`). Sections as §70:
  overview, types vs instances, first twin, telemetry, importing UPPAAL, drawing timed
  automata, PT view, DT view, ontology, interpretations, monitors, alignment, compilation, IR,
  packages, instances, deploying, updating a type, rollback, import/export.
* **Reference pages** (Kernel session, `docs/authoring/`, included in the site): `twin-ta/1`,
  TwinTA grammar, import diagnostics, property language and monitorability, scenarios,
  bindings and adapters, data-quality checks, deployment supervisor.
* **Screenshots** (§89). The 15 named images are captured from the running product by the
  existing capture pipeline: deterministic data, en-US, UTC. The GUI session writes the UI
  steps; the Kernel session keeps the stack orchestration in `scripts/capture-screenshots.sh`.
* The README, the architecture document and the final report are updated (Kernel session).
  The proof appendix gets the strong/weak argument and the "canonical model → rendering" step
  in the chain.

## 10. Work split, ownership and sequencing

### 10.1 Files

| Owner | Paths |
|---|---|
| Kernel session | `include/twin/{authoring,ingest,deploy,sandbox}/**`, `src/{authoring,ingest,deploy,sandbox}/**`, `apps/twin-ingest/**`; changes in `compiler`, `alignment`, `package`, `runtime`, `kernel`; `src/studio/engine_routes.{hpp,cpp}`; `api/studio-engine.openapi.yaml`, `api/runtime.openapi.yaml`; `web/studio/src/authoring/**`; `tests/{unit,integration,architecture,e2e}/**`; `docs/authoring/**`, `proof/**`; examples' formal files (`*.xml`, `*.tta.json`, `*.ont`, `*.interp`, `monitors.json`, `scenarios`, feed scenarios) |
| GUI session | `include/twin/{platform,studio,ontology}/**`, `src/{platform,studio,ontology}/**` (except `engine_routes`), `apps/twin-studio/**`, `api/studio.openapi.yaml`, `web/studio/**` (except `src/authoring/`), `tests/studio/**`, `docs/studio/**` and the docs site, examples' `example.json` and type metadata |
| Shared (announce before editing) | `scripts/start-demo.sh`, `scripts/capture-screenshots.sh`, `apps/macos/launcher.mm`, `Makefile`, `README.md`, `CMakeLists.txt` / presets |

### 10.2 Interfaces between the sessions

1. **Formats** (§4). The Kernel session publishes the JSON Schemas, the TypeScript types
   (`web/studio/src/authoring/types.ts`) and examples first.
2. **C++ headers** of `twin::authoring`, `twin::sandbox`, `twin::ingest` and `twin::deploy`.
   They are stubbed with real signatures before the implementations land, so the platform can
   build against them.
3. **Engine HTTP routes** (§6.12) and **React components** (`ModelEditor`, `ModelDiagram`)
   with documented props.
4. **Runtime additions** (`/runtime/monitors`) in `api/runtime.openapi.yaml`.

### 10.3 Phases

| Phase | Kernel session | GUI session | Exit criterion |
|---|---|---|---|
| 1 | Formats; `twin::authoring` (codec, validator, TwinTA, renderings, digest, UPPAAL import with preservation check, export, diff); engine routes for models and import | Data model for types, versions, instances and jobs; the mode switch; Studio home and the type workspace skeleton | A UPPAAL file can be imported via the API into a draft type version and shown read-only |
| 2 | Model editors (diagram and source); compile-from-canonical and the IR map; alignment diagnostics; monitors format and property analysis/checks; package additions; runtime monitors; sandbox and scenario runner | Wizard and templates; ontology and interpretation screens; alignment workspace; monitors builder; compile and IR viewer; jobs UI; release readiness and comparison | Journey A runs up to "package released", with the GUI driving it |
| 3 | `twin::ingest` and `twin-ingest` (simulator, replay, REST, MQTT, OPC UA placeholder, mapping, data quality, connection test); supervisor; demo and Mac launcher changes | Instances, bindings, connection test and live preview; deploy, rollout and rollback; Operate integration (Open in Studio, monitors panel); alerts | Journeys A and D complete end to end |
| 4 | Drone and pump formal content as types, Thermal Chamber fixtures, C++ acceptance tests, reference docs, proof update, README and report | Seeding as types, Playwright journeys A–D, Studio Guide, screenshots | All acceptance tests green; documentation and screenshots done |

Each phase ends with the full test suites green: C++, Vitest, Playwright, the API check and
Doxygen with 0 warnings.

## 11. Risks and open points

* **Model editor ownership.** The diagram editor is the largest UI piece. If it slips, the
  beginner journey (§86) slips. Mitigation: build it first in phase 2, and keep `ModelDiagram`
  read-only reuse simple.
* **UTAP positions.** Line and column may not be available for every construct. The fallback
  is the element path (template, location or edge, label kind).
* **Zone-graph property checks** use the aligner's zone construction unmodified. Bounds beyond
  the model's maximal constants are refused rather than risk extrapolation errors.
* **Migration** of the existing drone and pump data: `make demo-fresh` re-seeds. Existing data
  directories need a one-off migration; the GUI session decides between an automatic migration
  and a documented reset.
* **Scope.** This is a large build. Phases deliver vertical slices, so the product stays
  runnable after every phase.

## Appendix A — Traceability (request § → design)

| § | Topic | Design | Owner |
|---|---|---|---|
| 1–2 | Product concept, terminology | §0, §2 | both |
| 3 | Operate/Studio switch | §7.1 | GUI |
| 4 | Types vs instances | §2, §6.1–6.2 | GUI |
| 5–7 | Home, wizard, templates | §7.2–7.3, §6.10 | GUI |
| 8–10 | Asset, composition, telemetry schema | §6.1 | GUI |
| 11–13, 39 | Adapters, connection tests, canonical mapping | §4.6, §5 (`twin::ingest`), §7.11 | Kernel (engine), GUI (UI) |
| 14–19, 75 | PT/DT editors, import, one model | §3.2, §4.1–4.3, §7.4 | Kernel |
| 20, 44, 76–77 | Importer framework, unsupported constructs, provenance | §4.3 | Kernel |
| 21–24 | Ontology, interpretations, suggestions | §7.5–7.6 | GUI |
| 25–27 | Alignment workspace, visualisation, failure UX | §5 (`twin::alignment`), §7.7 | Kernel (diagnostics), GUI (UI) |
| 28–33 | Monitors, properties, requirements, data quality, alerts | §4.4, §5, §6.9 | Kernel (formats, evaluation), GUI (aggregation, UI) |
| 34–36 | Compile, IR viewer, package | §4.7, §5, §7.9 | both |
| 37 | Release readiness | §6.4 | GUI |
| 38, 40–41 | Instances, deploy, edit existing | §5.1, §6.2, §6.7, §7.1, §7.11 | both |
| 42–43, 45, 84 | Clone, import bundles, export, library | §4.3, §6.10 | both |
| 46–49 | Preview, sandbox, scenarios, assertions | §4.5, §5 (`twin::sandbox`), §7.12 | both |
| 50–53 | Drone, pump, from-scratch demo, UPPAAL demo | §8 | both |
| 54 | No hand-written DT software | §0, §3.2; the UX never asks for code | both |
| 55–57 | External analytics, plugins, presentation | §6.1, §6.11, §4.1 (layout), §4.7 | GUI |
| 58–59 | Monitors in Operate, versioning | §4.4, §4.7, §6.9 | both |
| 60–64 | Versioning, impact, comparison, rollout, rollback | §6.1, §6.5–6.7 | GUI (+ supervisor) |
| 65–69 | VALID vs VERIFIED, drafts, autosave, concurrency, history | §2, §6.8 | GUI |
| 70–71, 89 | Guide, help, screenshots | §9, §7.13 | both |
| 72–74 | UX principles, visual quality, layout | §7 | GUI |
| 78–81 | Backend authority, API, jobs, no fake verification | §3.3, §6.3, §6.12, §7.15 | both |
| 82–83 | Studio ↔ Operate | §5.1, §7.1 | both |
| 85–88 | Acceptance tests | §8.3 | both |
| 90–91 | Product model, final rule | §0, §3 | both |
