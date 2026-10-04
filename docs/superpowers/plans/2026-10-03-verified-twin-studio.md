# Verified Twin Studio Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans (the user chose native, autonomous execution). Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a domain-agnostic Digital Twin operations and engineering console, plus the C++ platform backend that serves it. Every formal conclusion is computed by the C++/Z3 tooling.

**Architecture:**
- `twin::ontology` provides the formal ontology services, on top of `aligner::semalign` and Z3.
- `twin::platform` provides persistence, lifecycle, impact and audit, on SQLite plus a content-addressed store.
- `twin::studio` and the `twin-studio` executable expose the HTTP/SSE API and proxy to `twin-runtime`.
- `web/studio` is a React/Vite SPA that only renders backend conclusions.

**Tech Stack:**
- C++20, CMake, cpp-httplib, nlohmann/json, SQLite (amalgamation, pinned), Z3, GoogleTest
- TypeScript, React 19, Vite, React Router, TanStack Query/Table/Virtual, Zod, uPlot, @xyflow/react + elkjs, CodeMirror 6, Radix UI, lucide-react
- Vitest, Testing Library, MSW, Playwright

**Spec:** `docs/superpowers/specs/2026-10-03-verified-twin-studio-design.md`

## Global Constraints

- Never modify `SemPTDTAlignmentICSE/`.
- Never modify the kernel workstream's modules (`src/{core,json,ir,kernel,compiler,runtime,ledger,package,alignment,planner,world,geo}`, `apps/{twin,twin-runtime,twin-world}`). The only allowed edits are one-line registrations in `src/CMakeLists.txt`, `apps/CMakeLists.txt`, `tests/CMakeLists.txt` and `cmake/Dependencies.cmake`.
- First-party C++ must compile with `twin_set_warnings` (`-Werror`) and follow the existing style: `twin::Result<T>`, `twin::Error`, Doxygen on every public declaration, `.clang-format`.
- Build only in `build/studio-*` binary directories, never in `build/release`.
- No hard-coded trust status anywhere. Allowed trust states: PASS, FAIL, UNKNOWN, NOT_CHECKED, STALE, INVALIDATED, CHECK_RUNNING, UNAVAILABLE.
- Z3 `unknown` must never be reported as a negative verdict. Infrastructure errors must never be reported as a NOT_A_REFINEMENT verdict.
- Published versions are immutable. Corrections always create a new version.
- The frontend contains no domain/semantic logic: no thresholds and no state-machine conditions.
- Drone code lives only in `web/studio/src/plugins/drone/`.

## Review Focus

1. **Ontology text with Windows line endings, tabs or trailing comments** must parse exactly like the aligner parses it. Test: CRLF fixture in `tests/unit/ontology/source_parser_test.cpp`.
2. **Refinement check where the candidate removes a symbol used by an old axiom** must give NOT_A_REFINEMENT with condition (a) diagnostics, not a parse crash and not CHECK_FAILED. Test: `refinement_test.cpp::RemovedSymbolIsConditionA`.
3. **Publishing a draft twice, or editing after publish,** must return a 409 StateError and leave the hash unchanged. Test: `lifecycle_test.cpp::PublishedIsImmutable`.
4. **A deployment that references evidence computed for an older ontology hash** must classify that evidence as STALE, never VALID. Test: `impact_test.cpp::EvidenceForOldHashIsStale`.
5. **SSE reconnect after a server restart (seq resets)** must make the client treat the new stream as a gap and refetch. Test: `web/studio/src/live/stream.test.ts`.

---

## Phase 1 — Foundation + Engineering loop

### Task 1: Contracts

**Files:**
- Create: `api/studio.openapi.yaml`
- Create: `api/runtime.openapi.yaml`
- Create: `api/README.md`

The Studio contract covers every route group in spec §4, with schemas for:
- Error
- TrustState
- ArtifactVersion
- OntologyStructure
- Diagnostic
- RefinementCheck
- ImpactReport
- PipelineStage
- Deployment
- AuditRecord
- Asset / Relationship
- TelemetryChannel / Sample
- SearchHit
- Overview

The runtime contract is the proposal sent to the kernel session:
- RuntimeState
- EnabledTransition
- Prediction
- LedgerRecord
- LedgerVerification
- Execution
- ReplayFrame
- the SSE `RuntimeEvent{seq,…}`

- [ ] Write both YAML files.
- [ ] Validate them: `npx @redocly/cli lint api/*.yaml`. Expected: no errors.

### Task 2: `twin::ontology` — strict source model

**Files:**
- Create: `include/twin/ontology/source.hpp`
- Create: `src/ontology/source_parser.cpp`
- Create: `src/ontology/CMakeLists.txt`
- Test: `tests/unit/ontology/source_parser_test.cpp`

**Interfaces (produced):**
```cpp
namespace twin::ontology {
struct Span { std::uint32_t line, column, length; };
struct SortDecl { std::string name; std::string comment; Span span; };
struct FunctionDecl { std::string name; std::vector<std::string> arg_sorts; std::string return_sort; std::string comment; Span span; };
struct RelationDecl { std::string name; std::vector<std::string> arg_sorts; std::string comment; Span span; };
struct Axiom { std::string id; std::string formula; std::string comment; Span span; };
struct OntologySource { std::vector<SortDecl> sorts; std::vector<FunctionDecl> functions; std::vector<RelationDecl> relations; std::vector<Axiom> axioms; std::string header_comment; };
struct InterpretationEntry { std::string key; bool is_event; std::string formula; std::string comment; Span span; };
struct InterpretationSource { std::vector<InterpretationEntry> entries; std::string header_comment; };
enum class Severity { Error, Warning, Note };
struct SourceDiagnostic { std::string code; Severity severity; std::string message; Span span; };
struct ParsedOntology { OntologySource source; std::vector<SourceDiagnostic> diagnostics; bool ok() const; };
struct ParsedInterpretation { InterpretationSource source; std::vector<SourceDiagnostic> diagnostics; bool ok() const; };
ParsedOntology parse_ontology(std::string_view text);
ParsedInterpretation parse_interpretation(std::string_view text);
std::vector<std::string> formula_symbols(std::string_view smt2);  // identifiers referenced
}
```

**Diagnostic codes:**

| Code | Meaning |
|---|---|
| ONT001 | unknown keyword |
| ONT002 | missing `:` |
| ONT003 | missing name |
| ONT004 | duplicate symbol |
| ONT005 | undeclared sort |
| ONT006 | unbalanced parentheses |
| ONT007 | undeclared symbol in formula |
| ONT008 | duplicate axiom id |
| INT001 | malformed line |
| INT002 | duplicate key |
| INT003 | unbalanced parentheses |
| INT004 | undeclared symbol |

INT004 needs the ontology, so it is checked in Task 3.

- [ ] Tests:
  - every `SemPTDTAlignmentICSE/assets/*/*.ont` and `*.interp` parses with no errors;
  - unknown keyword gives ONT001 at the right line/column;
  - CRLF input parses identically;
  - `formula_symbols("(and (> a b) (f c))")` returns `{a,b,c,f}`, excluding SMT keywords and operators.
- [ ] Implement, register in `src/CMakeLists.txt`, run `ctest -R ontology`.

### Task 3: `twin::ontology` — theory loading, validation, evaluation

**Files:**
- Create: `include/twin/ontology/theory.hpp`
- Create: `src/ontology/theory.cpp`
- Create: `include/twin/ontology/validation.hpp`
- Create: `src/ontology/validation.cpp`
- Create: `include/twin/ontology/evaluate.hpp`
- Create: `src/ontology/evaluate.cpp`
- Test: `tests/unit/ontology/validation_test.cpp`
- Test: `tests/unit/ontology/evaluate_test.cpp`

**Interfaces:**
```cpp
enum class Verdict3 { True, False, Unknown };
struct SolverConfig { unsigned timeout_ms = 10000; };
class Theory {  // one Z3 context: aligner-parsed K plus extra formulas parsed with the aligner's own InterpFileParser
 public:
  static Result<std::unique_ptr<Theory>> load(std::string_view ontology_text);  // via dtpta::OntFileParser on a temp file
  Result<z3::expr> parse_formula(std::string_view smt2) const;                  // via dtpta::InterpFileParser trick
  const std::vector<z3::expr>& axioms() const;                                  // re-parsed axiom formulas (same context)
  Verdict3 entails(const z3::expr& phi, const SolverConfig&) const;           // tri-state: Δ ∧ ¬φ unsat/sat/unknown
  Verdict3 consistent(const SolverConfig&) const;                              // Δ satisfiable?
  std::optional<std::vector<std::pair<std::string,std::string>>> counter_model(const z3::expr& phi, const SolverConfig&) const;
  std::string checker_identity() const;                                        // "z3 <ver> / dtpta-semalign / twin-ontology <ver>"
};
struct ValidationReport { bool valid; std::vector<SourceDiagnostic> diagnostics; Verdict3 consistent; std::string ontology_sha256; };
ValidationReport validate_ontology(std::string_view text, const SolverConfig&);
ValidationReport validate_interpretation(std::string_view ontology_text, std::string_view interp_text, const SolverConfig&);
struct Observation { std::string symbol; std::string value; };  // decimal text, e.g. "94.1"
enum class Truth { True, False, Unknown, InconsistentObservation };
struct EvaluationResult { std::string key; Truth truth; std::vector<std::string> symbols_used; std::vector<std::string> missing_observations; };
Result<std::vector<EvaluationResult>> evaluate(std::string_view ontology_text, std::string_view interp_text, std::span<const Observation>, const SolverConfig&);
```

**Validation must catch:**
- strict-parser errors;
- aligner-parse failure (reported as ONT100 with the aligner message);
- symbol-set mismatch between the strict parser and the aligner (ONT101);
- Δ unsatisfiable (ONT110, error);
- Δ consistency unknown (ONT111, warning).

- [ ] Tests:
  - CS6 domain.ont is valid and consistent;
  - adding `axiom bad : (< kvo_rate 0)` makes it inconsistent (ONT110);
  - evaluating pump `high_temperature` with temperature=94.1 gives True; with 60 gives False; with no observation gives Unknown;
  - contradictory observations give InconsistentObservation.

### Task 4: `twin::ontology` — structural diff + refinement (Def. 4) + preservation (Thm. 3)

**Files:**
- Create: `include/twin/ontology/diff.hpp`
- Create: `src/ontology/diff.cpp`
- Create: `include/twin/ontology/refinement.hpp`
- Create: `src/ontology/refinement.cpp`
- Test: `tests/unit/ontology/diff_test.cpp`
- Test: `tests/unit/ontology/refinement_test.cpp`

**Interfaces:**
```cpp
enum class ChangeKind { Added, Removed, Modified };
struct StructuralChange { ChangeKind kind; std::string element; /* sort|function|relation|axiom|interpretation */ std::string name; std::string before; std::string after; };
struct StructuralDiff { std::vector<StructuralChange> changes; std::vector<std::string> affected_interpretation_keys; };
StructuralDiff diff_ontologies(const OntologySource& from, const OntologySource& to);
StructuralDiff diff_interpretations(const InterpretationSource& from, const InterpretationSource& to);
std::vector<std::string> interpretation_keys_using(const InterpretationSource&, const std::set<std::string>& symbols);

struct DomainKnowledge { std::string ontology_text; std::optional<std::string> pt_interp; std::optional<std::string> dt_interp; };
enum class RefinementVerdict { ValidRefinement, NotARefinement, Unknown, CheckFailed };
struct ConditionResult { std::string condition; /* "a","b","c.P","c.D" */ RefinementVerdict verdict; std::vector<std::string> details; };
struct Obligation { std::string condition; std::string subject; Verdict3 result; std::optional<std::vector<std::pair<std::string,std::string>>> counter_model; };
struct RefinementReport { RefinementVerdict verdict; std::vector<ConditionResult> conditions; std::vector<Obligation> obligations; std::vector<std::string> assumptions; std::string checker_identity; std::string base_sha256, candidate_sha256; };
RefinementReport check_refinement(const DomainKnowledge& base, const DomainKnowledge& candidate, const SolverConfig&);
```

**Verdict rules:**
- any obligation FAIL → NotARefinement;
- otherwise any Unknown → Unknown;
- an exception or parse failure of the *base* → CheckFailed;
- a candidate parse error → CheckFailed with a diagnostic ("candidate must validate first").

The old axioms and old interpretations are parsed in the **candidate's** context. A failure caused by a missing or re-sorted symbol counts as condition (a) NotARefinement, not CheckFailed.

- [ ] Tests (cover the full CS1–CS8 + UseCase_Drone domain.ont → domain_v2.ont corpus):
  - every pair with its v1→v2 interpretations behaves as the paper's RUN 4 expects (ValidRefinement for the drone and pump);
  - relaxed axiom gives NotARefinement on (b) with a counter-model;
  - removed symbol gives NotARefinement on (a);
  - an interpretation meaning change gives NotARefinement on (c.D);
  - τ-status change (key removed) gives NotARefinement on (c);
  - `timeout_ms=1` on a nonlinear obligation gives Unknown;
  - corrupt base gives CheckFailed.

### Task 5: SQLite dependency + `twin::platform` store

**Files:**
- Modify: `cmake/Dependencies.cmake` (pinned sqlite amalgamation 3.46.x, SHA256)
- Create: `include/twin/platform/{database.hpp,object_store.hpp,artifacts.hpp,assets.hpp,telemetry.hpp,evidence.hpp,deployments.hpp,audit.hpp,impact.hpp,app_log.hpp}`
- Create: `src/platform/*.cpp`
- Test: `tests/unit/platform/*_test.cpp`

**Key interfaces:**
```cpp
class Database { static Result<std::unique_ptr<Database>> open(const std::filesystem::path&); /* migrations, prepared stmts, transaction() */ };
class ObjectStore { Result<std::string> put(std::string_view bytes); Result<std::string> get(std::string_view sha256) const; };
enum class Lifecycle { Draft, Validating, Verified, Published, Superseded, Rejected };
struct ArtifactVersion { std::string artifact_id, kind; std::int64_t version; std::optional<std::int64_t> parent; Lifecycle state; std::string content_sha256; std::string created_at; std::optional<std::string> published_at; std::string actor, description; };
class ArtifactRepository {
  Result<ArtifactVersion> create_artifact(kind, id, name, content, actor, description);   // v1 draft
  Result<ArtifactVersion> create_draft(id, from_version, actor, description);              // copy content, parent
  Result<ArtifactVersion> save_draft(id, version, content, actor);                         // StateError unless Draft
  Result<ArtifactVersion> set_state(id, version, Lifecycle, actor);                        // enforces transition table
  Result<ArtifactVersion> publish(id, version, actor);  // requires Verified; supersedes previous Published
  Result<std::string> content(id, version);
};
// Evidence: kind ∈ {validation, refinement, alignment, compile, package}; inputs = vector<{role, artifact_id, version, sha256}>; result json; evidence_sha256 over canonical json
// Impact: closure over twin bindings + evidence inputs; classify(evidence, consumer_hashes) -> {Valid, Stale, Invalidated, RecheckRequired, Unknown} with reason
// Audit: append(op, actor, artifacts, outcome, evidence_ids, reason) -> record with prev_hash/hash; verify() -> {valid, first_invalid_seq, reason}
```

- [ ] Tests:
  - CAS put/get round trip; same content gives the same hash;
  - lifecycle transition table: illegal transitions give StateError; a published version cannot be saved;
  - staleness of evidence whose input hash differs;
  - audit tampering (UPDATE a row) makes `verify` report the first invalid seq;
  - telemetry downsampling returns ≤ maxPoints buckets with min/max/avg.

### Task 6: `twin::studio` services + HTTP + `twin-studio` app + seed

**Files:**
- Create: `include/twin/studio/{server.hpp,services.hpp,pipeline.hpp,runtime_proxy.hpp,sse.hpp}`
- Create: `src/studio/*.cpp`
- Create: `apps/twin-studio/{main.cpp,CMakeLists.txt}`
- Create: `examples/industrial-pump/*`
- Test: `tests/unit/studio/api_test.cpp` (in-process server on an ephemeral port)

**Services:**
- engineering: validate, refine, impact, align (adapter over `dtpta::SemanticAlignmentChecker`), compile (`twin::compiler`), package (UNAVAILABLE until `twin::package` exists), release, deploy, rollback;
- every mutating operation writes an audit record.

**Seed:**
- `twin-studio seed --example industrial-pump --data-dir var/studio`;
- `twin-studio serve --port 8080 --data-dir var/studio --web-root web/studio/dist [--runtime twin-id=http://host:port]`.

- [ ] API tests:
  - GET ontology version; draft create, save and validate;
  - refinement POST returns a persisted report;
  - publishing an unverified draft gives 409;
  - impact on the v2 draft lists the alignment evidence as STALE with "ontology dependency changed";
  - release is blocked while the package stage is UNAVAILABLE;
  - the runtime proxy gives 503 `runtime_not_connected` when unconfigured.

### Task 7: Frontend foundation

**Files:**
- Create: `web/studio/` (Vite + TS + React)
- Create: `src/{app,api,design,live,features,plugins}`

**Contents:**
- Design tokens (light/dark), layout shell (sidebar IA, breadcrumbs, context bar, global search, role/disclosure selector);
- primitives: StatusBadge, TrustBadge, HashChip, TimeStamp, DataState, Panel, KeyValue, DataTable (TanStack + virtual), EmptyState, WhyDrawer;
- API client (fetch + typed errors + Zod for trust payloads);
- SSE store (seq, gap detection, pause/resume, jump to now).

- [ ] Vitest:
  - TrustBadge renders an icon + label for every state, and is never green when NOT_CHECKED;
  - DataState renders all five states;
  - the stream store detects a gap and calls invalidate;
  - the API error maps to a user message plus engineer details.

### Task 8: Engineering UI

**Screens:**
- Ontologies list, version view (sections: Overview / Symbols / Functions / Relations / Axioms / Interpretations / Dependencies / Visualization / Source);
- CodeMirror editor (highlight, server diagnostics as lint, autocomplete from symbols, go-to-definition, undo/redo);
- Save Draft / Validate / Check Refinement / Analyze Impact / Publish as separate actions;
- source diff (merge view) + structural diff;
- refinement run + evidence viewer (conditions, obligations, counter-models, hashes, checker identity, assumptions);
- interpretation editor with an evaluate panel;
- impact view (dependency tree with classifications + "why stale");
- change workspace + pipeline;
- packages, deployments, rollback dialog (reason required), verification history, audit log (chain status), logs viewer.

- [ ] Vitest + MSW:
  - the editor keeps unsaved edits when queries refetch;
  - the Publish button is disabled unless VERIFIED, and the server's 409 is shown;
  - the refinement UI shows CHECK FAILED distinctly from NOT A REFINEMENT;
  - the diff shows structural changes;
  - the rollback dialog requires a reason.

### Task 9: Assets, graph, telemetry, overview, search

- [ ] Asset explorer (tree + table), knowledge graph (neighbourhood expansion, filters, fit, search, list alternative).
- [ ] Asset overview (identity, status from runtime or "not connected", key telemetry, artifacts and trust).
- [ ] Telemetry (uPlot, ranges Live/15m/1h/6h/24h/7d/custom, units per axis, stale/missing/quality markers, CSV/JSON export, accessible table).
- [ ] Overview KPIs.
- [ ] Global search (⌘K).
- [ ] Tests: freshness classification is rendered from the server's `quality`/`stale` fields only; incompatible units get separate axes.

## Phase 2 — Operations loop on runtime contract

### Task 10: Runtime-backed views

These views use `/twins/{id}/runtime/*`:
- current state;
- behavioural graph (from IR locations/transitions plus runtime enabled/recent);
- semantic facts with Why (I_D(location) + evaluate);
- conformance;
- prediction tree;
- what-if (runtime predict API, with a clear SIMULATION banner);
- planning candidates;
- executions, ledger + verify, provenance chain, replay (historical deployment artifacts).

All of them render "Runtime not connected" when the proxy returns 503. Tests use MSW fixtures that follow `api/runtime.openapi.yaml`.

## Phase 3 — Plugins, docs, e2e

### Task 11: Plugin framework + drone plugin

### Task 12: Playwright e2e + screenshots

Playwright runs against the seeded server.

### Task 13: Docs

Write the `docs/studio/*` manual pages and the tutorial "Safely Evolving an Ontology", using real screenshots.
