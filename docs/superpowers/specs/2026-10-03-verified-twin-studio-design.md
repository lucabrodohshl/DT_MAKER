# Verified Twin Studio — Design Specification

Status: approved for autonomous implementation (2026-10-03)
Owner: Studio/platform workstream (the kernel/compiler/runtime is a separate, parallel workstream)

## 1. Intent

Build **Verified Twin Studio**, a general-purpose Digital Twin operations and
engineering console. It has two connected loops:

* **Operations:** asset → telemetry → semantic meaning → verified behaviour →
  prediction → decision → execution evidence
* **Engineering:** ontology/interpretation/model → edit → validate → refinement and
  alignment → impact → compile → verified package → deploy → operate →
  maintain

Users: operator, reliability engineer, DT/formal engineer, ontology engineer,
auditor and administrator. All of them share one product, with depth revealed
progressively.

### Hard constraints (from the request)

* **The browser has no semantics.** Every behavioural, semantic and formal
  conclusion comes from C++ backend evidence. The frontend renders conclusions;
  it never derives them.
* **The semantic kernel stays the single behavioural authority.** Studio never
  executes behaviour itself. It proxies the runtime (`twin-runtime`, built by the
  parallel workstream).
* **Formal ontology decisions come from SemPTDTAlignmentICSE.** Validation,
  refinement (paper Def. 4), alignment preservation (Theorem 3) and alignment
  run on SemPTDTAlignmentICSE's ontology/interpretation semantics and Z3.
* **Trust badges show only evidence-backed states:** PASS / FAIL / UNKNOWN /
  NOT CHECKED / STALE / INVALIDATED / CHECK RUNNING.
* **Published artifacts are immutable and content addressed.** History always
  refers to exact historical versions.

### Decisions taken with the user

| Decision | Choice |
|---|---|
| Studio backend | C++ (`apps/twin-studio`, modules `twin::ontology`, `twin::platform`, `twin::studio`) |
| Operational data before `twin-runtime` exists | A contract (`api/runtime.openapi.yaml`) plus an honest "runtime not connected" state. Mock data appears only in tests. |
| Coordination | Agree the runtime contract with the kernel session; never edit its modules |
| Process | Spec, then plan, then autonomous phased implementation |

## 2. Formal grounding (from the paper and the aligner)

* **Ontology:** K = (S, F, R, Δ) in the aligner's `.ont` format
  (`sort`, `fun name : [A ...] -> R`, `rel name : [A ...]`, `axiom id : smt2`).
  All user sorts are read as `Real` by the aligner.
* **Interpretation:** `.interp` maps `Location : φ` and `label! : φ`.
  Domain knowledge is Φ = (K, {I_P, I_D}).
* **Refinement Φ₁ ⊑ Φ₂ (Def. 4):**
  * (a) S₁ ⊇ S₂, F₁ ⊇ F₂, R₁ ⊇ R₂, and shared symbols keep their declared sorts;
  * (b) Δ₁ ⊨ Δ₂;
  * (c) for X ∈ {P, D} and every label x: Δ₁ ⊨ I₁,X(x) ↔ I₂,X(x), with τ status
    (absent ⇔ absent) preserved.
* **Theorem 3:** Φ′ ⊑ Φ and V_P ∼Φ V_D ⇒ V_P ∼Φ′ V_D (weak alignment too).
  Studio reports **ALIGNMENT PRESERVED BY REFINEMENT** only when two things hold:
  a VALID Def. 4 check of exactly (Φ_old → Φ_new) for the evidence's interpretation
  pair, and a PASS alignment evidence for exactly Φ_old and the same PT/DT model
  hashes. Otherwise it reports REALIGNMENT REQUIRED.
* **Aligner behaviours we compensate for (without modifying the aligner):**
  * `Ontology::entails` maps Z3 `unknown` to "not entailed". The refinement
    checker therefore runs its own tri-state checks, and UNKNOWN is never
    reported as NOT A REFINEMENT.
  * The `.ont`/`.interp` parsers silently skip unknown keywords and malformed
    lines. Studio's strict source parser rejects them with located diagnostics.
    The authoritative Z3 reading still comes from the aligner's parser, and the
    two are cross-checked (symbol sets must agree).
* **Propositions are location labels.** The kernel's propositions are
  `at(l)`; meaning is I_D(l). Observation-level truth ("is `high_temperature`
  true given these readings?") is computed by a **3-valued interpretation
  evaluator** in Z3:
  * Δ ∧ obs ⊨ φ → TRUE
  * Δ ∧ obs ⊨ ¬φ → FALSE
  * otherwise → UNKNOWN
  * Δ ∧ obs unsatisfiable → INCONSISTENT_OBSERVATION

## 3. Architecture

```
web/studio (React, Vite, TS) ──HTTP/JSON + SSE──▶ twin-studio (C++)
                                                   ├─ twin::studio    HTTP routes, SSE hub, runtime proxy, error mapping
                                                   ├─ twin::platform  artifact store (CAS), versions & lifecycle, assets &
                                                   │                  relationships, telemetry history, dependency/impact
                                                   │                  graph, evidence registry, deployments, engineering
                                                   │                  audit (hash-chained), app log
                                                   ├─ twin::ontology  strict .ont/.interp parser, validation, structural
                                                   │                  diff, Def.4 refinement, Thm.3 preservation, 3-valued
                                                   │                  interpretation evaluation        (aligner::semalign + Z3)
                                                   ├─ twin::compiler  (existing, parallel workstream) compile stage
                                                   └─ proxy ─────────▶ twin-runtime (parallel workstream): state, transitions,
                                                                       prediction, ledger, replay
```

Module dependencies form a DAG:

* `ontology` depends on `core`, `json` and `aligner::semalign`.
* `platform` depends on `core`, `json`, `ontology` and SQLite.
* `studio` depends on `platform`, `ontology`, `compiler` and httplib.

None of them is linked into `twin_kernel`.

### 3.1 Persistence (`var/studio/` by default; `--data-dir`)

* `objects/<sha256[0:2]>/<sha256>` holds immutable content-addressed blobs
  (ontology text, interpretation text, models, IR, evidence JSON, packages).
* `studio.db` (SQLite, WAL) is the source of truth for everything else. It is
  organised into these tables:
  * Artifacts: `artifacts`, `artifact_versions` (lifecycle, parent, content hash,
    timestamps, actor, description), `drafts` (mutable, which only drafts may be)
  * Assets: `assets`, `relationships`
  * Telemetry: `telemetry_channels`, `telemetry_samples`
  * Evidence: `evidence` (refinement, validation, alignment, compile, package),
    `evidence_deps`
  * Twins: `twins` (asset ↔ artifacts binding), `deployments`
  * Engineering: `changes` (change workspaces), `audit` (hash-chained engineering
    audit)
* `logs/studio.jsonl` holds structured application logs. They are diagnostic,
  not evidence.
* The execution ledger stays the runtime's, and Studio only reads it through
  the runtime API. Telemetry history, the engineering audit, evidence and app
  logs are separate stores (§73).

### 3.2 Lifecycles

* **Version lifecycle:** DRAFT → VALIDATING → VERIFIED → PUBLISHED → SUPERSEDED,
  or REJECTED.
  * Only DRAFT content may change.
  * Publish freezes the content hash.
  * Edits to a published version create `<id>@<n+1>` as a DRAFT with
    `parent = <n>`.
* **Evidence state:** VALID, STALE, INVALIDATED, RECHECK_REQUIRED or UNKNOWN.
  * State is **computed at read time** from the dependency graph. Evidence
    whose input hashes are no longer the hashes a consumer references is STALE
    for that consumer.
  * State is never cached as green. Each evidence row stores its exact input
    hashes.
  * "Why stale" returns the changed dependency edge.

### 3.3 Release pipeline (per change workspace)

The stages are EDIT → SAVE → VALIDATE → CHECK REFINEMENT → ANALYZE IMPACT →
CHECK/RECHECK ALIGNMENT (or PRESERVED BY THM 3) → COMPILE → BUILD PACKAGE →
VERIFY PACKAGE → RELEASE → DEPLOY.

* Each stage is computed from stored evidence, with its state, evidence id,
  timestamp and diagnostics.
* RELEASE is blocked unless every mandatory stage is PASS for the exact
  artifact hashes.
* COMPILE uses `twin::compiler`.
* ALIGN uses an adapter over `aligner::semalign`, until `twin::alignment` exists.
* BUILD/VERIFY PACKAGE uses `twin::package` once it exists. Until then those
  stages report `UNAVAILABLE` (not PASS), and release stays blocked.

### 3.4 Deployments and rollback

* A deployment is an immutable record binding a twin to a package and to
  artifact versions (PT, DT, ontology, interpretations, IR hash, kernel version).
* The current deployment is the latest record. Rollback appends a record that
  points to an earlier package and carries a reason. Nothing is deleted.
* Executions reference their deployment, so replay resolves historical artifacts.

## 4. API (`api/studio.openapi.yaml`, `/api/v1`)

* Errors use `{"error":{code,message,context[],diagnostics[]?}}`, which is
  `twin::Error` mapped to HTTP codes. Operators see `message`; engineers can
  expand `context` and `diagnostics`. Stack traces are never sent.
* Live updates use SSE `/api/v1/stream?topics=…`. Every event carries a
  monotonically increasing `seq`. On a gap or reconnect the client refetches
  authoritative state.

Route groups:

* **Search:** `/search?q=`
* **Assets:** `/assets`, `/assets/{id}`, `/assets/{id}/neighborhood?depth=&types=`,
  `/relationships`
* **Telemetry:** `/assets/{id}/telemetry/channels`,
  `/telemetry/{channel}/samples?from=&to=&maxPoints=` (server-side min/max/avg
  downsampling), `POST /telemetry/ingest`
* **Twins and runtime:** `/twins`, `/twins/{id}` (artifact bindings, deployment,
  presentation metadata), `/twins/{id}/runtime/*`, which proxies
  `api/runtime.openapi.yaml`
* **Ontologies:** `/ontologies`, `/ontologies/{id}/versions`,
  `/ontologies/{id}/versions/{v}` (source, structure, hash, lifecycle),
  `/ontologies/{id}/versions/{v}/symbols/{name}` (definition and cross-references),
  `POST /ontologies/{id}/drafts`, `PUT /…/draft`, `POST /…/validate`,
  `POST /…/publish`
* **Interpretations:** same shape as ontologies, plus `POST /interpretations/{id}/versions/{v}/evaluate`
  (3-valued evaluation against observation bindings)
* **Diff:** `/diff?kind=ontology&from=&to=`, which returns both the source diff
  and the structural diff
* **Refinement:** `POST /refinement-checks` (base Φ, candidate Φ),
  `/refinement-checks/{id}`
* **Evidence:** `/alignment-evidence/{id}`, `POST /alignment-runs`,
  `/verification-history`
* **Impact:** `/impact?artifact=&version=` (dependency closure with
  potentially-affected / stale / requires-verification / preserved
  classifications)
* **Changes:** `/changes`, `/changes/{id}`, `/changes/{id}/pipeline`,
  `POST /changes/{id}/stages/{stage}/run`, `POST /changes/{id}/release`
* **Packages and deployments:** `/packages`, `/packages/{id}`, `/deployments`,
  `POST /deployments`, `POST /deployments/rollback`
* **Audit:** `/audit` (engineering audit, chain verification included),
  `/logs` (app logs)
* **Overview:** `/overview` (estate KPIs, computed server-side)

## 5. Frontend (`web/studio`)

**Stack:**

* TypeScript, React 19, Vite, React Router
* TanStack Query (server state), TanStack Table + Virtual
* Zod (validating trust-critical payloads)
* uPlot (time series), @xyflow/react + elkjs (graphs)
* CodeMirror 6 (editors, lint, autocomplete, merge view for source diff)
* Radix UI primitives (accessible dialogs, menus, tabs, tooltips), lucide icons
* Vitest + Testing Library + MSW (tests), Playwright (e2e and screenshots)

**State discipline:**

* Server state lives in the query cache. Trust-status queries use `staleTime: 0`
  and refetch on window focus.
* UI state stays local to components or the URL.
* The live stream is a store (seq tracking, pause/resume, gap → invalidate).
* Replay, simulation (what-if) and engineering drafts each have their own
  store. Drafts persist on the server; nothing is authoritative in
  localStorage.

**IA and routes** (per request §3):

* Overview: `/`
* Assets: `/assets`, `/assets/:id/{overview,telemetry,behavior,predictions,events}`,
  `/graph`
* Operations: `/operations/{live,telemetry,events}`
* Behavior: `/assets/:id/behavior/{state,graph,facts,conformance}`
* Predict: `/assets/:id/predict/{future,what-if,planning}`
* Audit: `/audit/executions/:id`, `/audit/executions/:id/replay`,
  `/audit/provenance/:kind/:id`, `/audit/ledger`
* Engineering: `/engineering/{models,ontologies,interpretations,alignment,verification,packages,deployments}/…`
* Maintenance: `/maintenance/changes/:id`, `/maintenance/refinement/:id`,
  `/maintenance/impact`, `/maintenance/history`, `/maintenance/rollback`
* Admin: `/admin/logs`

**Design system:**

* Restrained industrial look, light-first with a dark theme.
* Status is always shown as icon + label + colour, never colour alone.
* Spacing, typography and colour come from token CSS variables. Components:
  StatusBadge, TrustBadge, HashChip, TimeStamp (labels logical, event,
  ingestion and display time), DataState (loading, empty, error, stale,
  unavailable), Panel, KeyValue, DataTable, EmptyState, WhyDrawer.

**Domain plugins:**

* Plugins implement `DomainPlugin {id, matches(asset), Component}` and receive
  props and hooks: `asset`, `telemetry`, `semanticState`, `events`,
  `predictions`, `replayFrame`, `planner`.
* They are registered in `src/plugins/registry.ts`. Generic views never import
  plugin code.
* The drone plugin is the reference implementation (phase 3).

## 6. Second example: `examples/industrial-pump`

* Centrifugal process pump PT/DT views in the aligner fragment, with locations
  NORMAL, DEGRADED, COOLING, STOPPING and FAULT.
* Ontology `process-pump` v1 contains thresholds and non-negativity axioms over
  temperature, vibration, pressure and rpm. The PT/DT interpretations reference
  the thresholds symbolically.
* The v2 maintenance scenario:
  * adds `sort Bearing`-related functions and tightens a bound, which is a valid
    refinement, so alignment is preserved by Theorem 3;
  * a counter-scenario relaxes an axiom, so the result is NOT A REFINEMENT,
    with a counter-model.
* The asset graph is Plant → Line 4 → Pump P-101 → Motor M12 → Bearing B4,
  plus sensors with `observedBy` / `feeds` / `powers` relationships.
* Telemetry comes from a deterministic generator (seeded). These are PT
  observations, not semantics.
* `twin-studio seed --example industrial-pump` creates everything through the
  same service APIs, so seeding runs real validation, refinement, alignment
  and compile. No status is written directly.

## 7. Phases

1. **Foundation and engineering loop (all real):** contracts, `twin::ontology`,
   `twin::platform`, `twin-studio` server and seed, frontend shell and design
   system, the ontology/interpretation editors, diff, refinement, impact,
   change workspace and pipeline, packages and deployments (reporting
   UNAVAILABLE where the package library is missing), audit, logs, search,
   assets, graph and telemetry.
2. **Operations loop on the runtime contract:**
   * behaviour state/graph/facts/why, conformance, prediction, what-if,
     planning, ledger + verify, provenance, replay;
   * wired to `twin-runtime` when present, honest "runtime not connected"
     otherwise.
3. **Plugins and quality:** drone plugin, docs and tutorial (real screenshots),
   Playwright e2e and visual regression, accessibility pass.

## 8. Testing

* **C++ (GoogleTest):**
  * strict parser diagnostics;
  * refinement on every aligner corpus v1→v2 pair: VALID where the paper
    expects it; NOT A REFINEMENT for the relaxed counter-example, with
    counter-model; UNKNOWN propagation via a forced timeout;
  * structural diff, evaluator truth tables, CAS immutability, lifecycle
    transition rules, staleness computation, audit chain tamper detection,
    and HTTP route tests against an in-process server.
* **Frontend:** Vitest/RTL/MSW component and interaction tests for the areas
  listed in request §47.
* **End to end:** Playwright e2e runs against the real `twin-studio` seeded
  with the pump example.

## 9. Out of scope / honest limits

* No authentication in v1. A role selector only controls disclosure depth and
  is documented as *not* a security boundary.
* No drag-and-drop dashboard editor.
* Operational features depend on `twin-runtime`. Until it exists they show
  "Runtime not connected".
