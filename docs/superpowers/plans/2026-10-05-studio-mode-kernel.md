# Studio Mode — Kernel-session Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the engine and execution half of Studio Mode, plus the model editors. A twin
authored in the GUI is then imported or drawn, validated, aligned, compiled, packaged with
monitors, previewed on the real kernel, bound to data sources and deployed as running processes.

**Architecture:** New C++ modules `twin::monitoring` (light: property language and monitor
documents, used by the runtime), `twin::authoring` (canonical model, TwinTA text, UPPAAL
import/export, validation, diff, toolchain bridge, property checks), `twin::sandbox` (previews
and scenarios on `TwinSession`), `twin::ingest` with the `twin-ingest` executable (adapters,
mapping, data quality) and `twin::deploy` (process supervisor). The changes to the existing
compiler, alignment, package and runtime code are additive. The Studio backend (GUI session)
calls these in-process, and `src/studio/engine_routes.cpp` exposes the stateless engine
services over HTTP. The model editors live in `web/studio/src/authoring/`.

**Tech Stack:** C++20, CMake presets (`release`, `studio-release`), UTAP/UDBM (pinned), Z3,
the unmodified SemPTDTAlignmentICSE (`dtpta`), cpp-httplib, GoogleTest; React 19 + TypeScript,
`@xyflow/react`, CodeMirror 6, TanStack Query, Vitest, MSW, Playwright.

**Spec:** `docs/superpowers/specs/2026-10-05-studio-mode-design.md`

**Execution:** The user said "Finish the implementation". This plan tracks the work, and
execution starts without a further review gate. Execution is native: one session, no
subagents. Commits only if the user asks (repository rule); each task ends with a green
checkpoint instead.

## Global Constraints

- Do not modify `SemPTDTAlignmentICSE/` (use its public headers only).
- One semantics: only the toolchain rendering of `twin-ta/1` is given to the compiler and the aligner.
- No physical real-time claims; logical time only.
- No fake verification. Formal results come only from aligner, compiler, zone graph, `Ontology::entails`, kernel.
- VALID means the structure is correct. VERIFIED means a formal check passed. A suggestion is
  never an equivalence. Scenarios are labelled as tests, never proofs.
- Every public declaration gets Doxygen documentation, and `doxygen docs/Doxyfile` stays at 0 warnings.
- `tests/architecture` keeps enforcing:
  - only `TwinSession` mutates semantic state;
  - the runtime never links `twin::world`, `twin::authoring`, `twin::ingest` or `twin::deploy`;
  - `twin::ingest` and `twin::deploy` include no kernel or session headers.
- Do not edit GUI-session files (spec §10.1). Shared files are announced to the GUI session before editing.
- Each task ends with green suites: `ctest --test-dir build/release`, `ctest --test-dir build/studio-release`, `npm test` / `npm run typecheck` / `npm run lint` in `web/studio` (when UI is touched), `make api-check`.

## Review Focus

1. **Comments or notes on a declaration must survive formatting and import, and never change
   the semantic digest.** Test in Task 2: the corpus round trip (parse, print, parse) keeps
   every comment, and the digest is unchanged.
2. **An imported UPPAAL file using any unsupported construct must be refused with every
   offending construct listed, and never silently accepted.** Test in Task 4: a fixture with a
   data variable, a disjunction, an urgent location and a broadcast channel reports four
   errors with positions.
3. **Renaming a location in the diagram must update every edge, every interpretation reference
   shown, and the layout.** Test in Task 12: the `renameLocation` command updates edges and
   layout keys; undo restores everything.
4. **A late or duplicate observation from a real source must not crash ingestion or move
   logical time backwards.** Test in Task 13: out-of-order events produce a `clock_regression`
   finding and are not submitted; duplicates produce a `duplicate` finding.
5. **Upgrading or rolling back must not lose the previous execution.** Test in Task 14:
   `replace()` ends the old session with an `end` record. Both ledgers remain listed and verify.

---

### Task 1: `twin::monitoring` and `twin::authoring` model core

**Files:**
- Create: `include/twin/authoring/{model.hpp,diagnostics.hpp,validate.hpp,layout.hpp}`, `src/authoring/{model.cpp,diagnostics.cpp,validate.cpp,layout.cpp,CMakeLists.txt}`
- Modify: `src/CMakeLists.txt` (add `authoring` after `alignment`), `cmake/version.hpp.in` (`kAuthoring = "twin-authoring/1"`, `kModelFormat = "twin-ta/1"`)
- Test: `tests/unit/authoring/model_test.cpp`, `tests/unit/authoring/validate_test.cpp`, `tests/unit/CMakeLists.txt`

**Interfaces (produces):**
```cpp
namespace twin::authoring {
struct Bound { std::variant<std::int64_t, std::string> value; };                  // literal or constant name
struct Atom { std::string clock; std::optional<std::string> minus; ir::Comparison op; Bound bound; };
using Constraint = std::vector<Atom>;                                               // conjunction; empty = true
struct ClockDecl { std::string name; std::string note; };
struct ConstantDecl { std::string name; std::int64_t value{0}; std::string note; };
struct ChannelDecl { std::string name; std::string note; };
struct LocationDecl { std::string name; bool initial{false}; Constraint invariant; std::string note; };
struct Sync { std::string channel; char direction{'!'}; };                         // '!' or '?'
struct EdgeDecl { std::string id, source, target; std::optional<Sync> sync; Constraint guard;
                  std::vector<std::string> resets; std::string note; };
struct Model { std::string name, note; std::vector<ClockDecl> clocks; std::vector<ConstantDecl> constants;
               std::vector<ChannelDecl> channels; std::vector<LocationDecl> locations; std::vector<EdgeDecl> edges; };
json::Json to_json(const Model&);                 Result<Model> model_from_json(const json::Json&);
std::string content_sha256(const Model&);         // SHA-256 of the canonical JSON
struct SourceRange { std::uint32_t line{0}, column{0}, end_line{0}, end_column{0}; };
struct ElementRef { std::string kind, name, part; };  // kind: model|clock|constant|channel|location|edge
struct Diagnostic { std::string severity, code, message, hint; ElementRef element; std::optional<SourceRange> range; };
json::Json to_json(const Diagnostic&); json::Json to_json(const std::vector<Diagnostic>&);
bool has_errors(const std::vector<Diagnostic>&) noexcept;
std::vector<Diagnostic> validate(const Model&);   // TWM0xx
struct Point { std::int64_t x{0}, y{0}; };
struct LocationLayout { Point position; std::optional<Point> label; };
struct EdgeLayout { std::vector<Point> nails; std::optional<Point> label; };
struct Layout { std::map<std::string, LocationLayout> locations; std::map<std::string, EdgeLayout> edges; };
json::Json to_json(const Layout&);  Result<Layout> layout_from_json(const json::Json&);
}
```

**Validator codes:**

| Code | Severity | Meaning |
|---|---|---|
| TWM001 | error | Invalid identifier, or a UPPAAL keyword |
| TWM002 | error | Duplicate name in the shared namespace (clocks, constants, channels, locations, model name) |
| TWM003 | error | No initial location |
| TWM004 | error | Several initial locations |
| TWM005 | error | Edge endpoint unknown |
| TWM006 | error | Undefined clock (atom or reset) |
| TWM007 | error | Undefined constant |
| TWM008 | error | Undefined channel |
| TWM009 | error | Diagonal in a guard |
| TWM010 | error | Bound outside the UDBM range |
| TWM011 | error | Duplicate or invalid edge id |
| TWM012 | error | No locations |
| TWM013 | warning | Clock reset twice on one edge |
| TWM020 | warning | Unused clock |
| TWM021 | warning | Unused channel |
| TWM022 | warning | Unused constant |
| TWM023 | warning | Location unreachable in the graph |
| TWM024 | warning | Receive label `a?` (cannot be interpreted; alignment lint TWA011) |

- [ ] Step 1: Write `model_test.cpp` covering:
  - JSON round trip of a full model (all fields, notes, constant bound, diagonal invariant);
  - `model_from_json` rejects a wrong `format`, an unknown key, `sync.direction` `"x"`, and a
    non-integer `value`;
  - `content_sha256` is stable across key order.
- [ ] Step 2: Write `validate_test.cpp` with one test per code (TWM001…TWM024), each asserting
  the code, the severity and the `element`. Include: the pump DT model is valid with no errors;
  the `int` keyword as a name gives TWM001; a location named like a clock gives TWM002.
- [ ] Step 3: Run `cmake --build build/release --target authoring_model_test authoring_validate_test` and expect a compile failure.
- [ ] Step 4: Implement the model, codec, diagnostics, layout and validator (reuse
  `compiler::detail::max_constraint_constant` via a public `twin::compiler::max_clock_bound()`
  added to `compiler.hpp`).
- [ ] Step 5: Run the tests and expect PASS. Doxygen shows 0 warnings.

### Task 2: TwinTA text format (parser, printer, formatter, symbols)

**Files:** Create `include/twin/authoring/text.hpp`, `src/authoring/text_lexer.{hpp,cpp}`,
`src/authoring/text_parser.cpp`, `src/authoring/text_printer.cpp`; Test
`tests/unit/authoring/text_test.cpp`.

**Interfaces:**
```cpp
struct Symbol { std::string kind, name; SourceRange definition; std::vector<SourceRange> references; };
struct ParseResult { std::optional<Model> model; std::vector<Diagnostic> diagnostics; std::vector<Symbol> symbols; };
ParseResult parse_text(std::string_view source);          // syntax TWT0xx + validate() when it parses
std::string print_text(const Model&);                      // canonical layout, comments as notes
Result<std::string> format_text(std::string_view source);  // parse then print; refuses on syntax errors
Result<Constraint> parse_constraint(std::string_view text); // inspector fields: "t >= 30 && x < 5"
```

**Codes:**
- `TWT001` unexpected token;
- `TWT002` unterminated comment;
- `TWT003` integer out of range;
- `TWT010` `||`, `TWT011` `!`, `TWT012` `false`, `TWT013` non-zero reset, `TWT014` data
  variable keyword (`int`, `bool`), each with the compiler's hint.

- [ ] Step 1: Write the tests:
  - parse the spec §4.2 example and compare with the expected `Model`;
  - `print_text(parse(x)) == x` for the canonical sample;
  - `parse(print(m)) == m` for every corpus model imported in Task 4;
  - comments attach to the next declaration and to the end of the owner block;
  - an edge without an id gets `e<n>` with the smallest free n;
  - every error code has a test with its exact range;
  - `format_text` keeps all comments and the semantic digest (needs Task 3; add the assertion
    then);
  - `parse_constraint("t >= COOL_MIN && x - y < 3")` gives two atoms;
  - symbols: the definition and reference ranges of a clock used in three places.
- [ ] Step 2: Implement the lexer (with line and column), a recursive-descent parser with error
  recovery at `;` and `}`, the printer and the symbols.
- [ ] Step 3: Run the tests and expect PASS.

### Task 3: Renderings, semantic digest, toolchain bridge, compile from canonical

**Files:**
- Create: `include/twin/authoring/uppaal.hpp` (renderings), `include/twin/authoring/toolchain.hpp`, `src/authoring/render_xml.cpp`, `src/authoring/toolchain.cpp`
- Test: `tests/unit/authoring/render_test.cpp`

**Interfaces:**
```cpp
std::string render_toolchain_xml(const Model&);                 // deterministic, notes/layout excluded, initial first
std::string render_exchange_xml(const Model&, const Layout&);   // + coordinates, nails, comments labels
std::string semantic_digest(const Model&);                      // sha256(render_toolchain_xml)
std::string_view content_format(std::string_view artifact_content);   // "twin-ta/1" | "uppaal-xml" | "unknown"
Result<std::string> toolchain_source(std::string_view artifact_content); // XML bytes for compiler/aligner
struct CompiledModel { compiler::CompileResult result; json::Json source_map; std::string semantic_digest; };
std::variant<CompiledModel, compiler::CompileFailure> compile_model(const Model&, compiler::CompileOptions, const std::filesystem::path& work_dir);
json::Json source_map(const Model&, const ir::Model&);  // {locations:{ir id: name}, transitions:{ir id: edge id}}
```

- [ ] Step 1: Write the tests:
  - the rendering of the pump DT model compiles, and its IR (with `info.source_sha256` cleared)
    equals the IR of `examples/industrial-pump/models/pump_dt.xml`;
  - the digest is unchanged by notes and layout;
  - the initial location is emitted first even if declared last;
  - XML escaping of `<`, `&` and `>` in guards;
  - the exchange rendering, re-read by UTAP, has the same clocks, locations and edges;
  - `toolchain_source` on legacy XML returns the bytes unchanged;
  - the source map has one entry per transition.
- [ ] Step 2: Implement it. The rendering writes `<declaration>` (clocks, `const int`,
  channels), one `<template>` with locations, `<init>`, transitions, and the system line
  `P = <name>(); system P;`.
- [ ] Step 3: Run the tests and expect PASS.

### Task 4: UPPAAL importer, preservation check, export, importer framework, diff

**Files:**
- Create: `include/twin/authoring/import.hpp`, `include/twin/authoring/diff.hpp`, `src/authoring/import_uppaal.cpp`, `src/authoring/importers.cpp`, `src/authoring/diff.cpp`
- Modify: `src/compiler/utap_reader.{hpp,cpp}` (expose constant names on bounds, collect positions); add `include/twin/compiler/reader.hpp` (public strict-read API returning the source model plus constants)
- Test: `tests/unit/authoring/import_test.cpp`, `tests/integration/authoring_corpus_test.cpp`; fixtures `tests/fixtures/authoring/unsupported_mix.xml`, `tests/fixtures/authoring/with_constants.xml`

**Interfaces:**
```cpp
struct ImportOptions { std::string filename; bool legacy_system_declaration{false}; };
struct Provenance { std::string original_filename, original_sha256, imported_at, importer, importer_version;
                    json::Json options; std::string content_sha256, semantic_digest; bool preserved{false};
                    std::size_t preservation_checks{0}; };
struct ImportResult { std::string format; std::optional<Model> model; Layout layout;
                      std::vector<Diagnostic> diagnostics; std::optional<Provenance> provenance; };
class Importer { virtual std::string id() const = 0; virtual std::string version() const = 0;
                 virtual bool detect(std::string_view filename, std::string_view content) const = 0;
                 virtual ImportResult run(std::string_view content, const ImportOptions&) const = 0; };
const std::vector<const Importer*>& importers();   // uppaal-xml, twin-ta-json, twinta-text
ImportResult import_any(std::string_view content, const ImportOptions&);
json::Json to_json(const ImportResult&);
struct ExportResult { std::string content; std::string media_type; bool round_trip_verified{false}; };
Result<ExportResult> export_model(const Model&, const Layout&, std::string_view target); // "twinta"|"json"|"uppaal"|"uppaal-toolchain"
json::Json diff_models(const Model& from, const Model& to);
```

- [ ] Step 1: Write the tests:
  - importing `pump_dt.xml` gives the expected model; the provenance hashes match;
    `preserved == true`;
  - the layout has the XML coordinates;
  - `with_constants.xml` keeps constant names in the bounds;
  - `unsupported_mix.xml` gives no model and four errors (TWC014, TWC024, TWC031, TWC012), each
    with a line or an element path and a hint;
  - for every corpus file under `SemPTDTAlignmentICSE/assets` (legacy flag), the import is
    either preserved or reports errors, never a silent change;
  - export to `uppaal` re-imports with an equal semantic digest;
  - `diff_models`: an invariant change is `semanticChange: true`; a note change is false.
- [ ] Step 2: Implement it. Read with UTAP through the strict reader, read coordinates, nails
  and `comments` labels with libxml2 (already in `third_party/install`). Preservation: compile
  the original and the rendering, then compare the IRs with `info.source_sha256` cleared.
- [ ] Step 3: Run the tests and expect PASS (unit and integration).

### Task 5: Engine HTTP routes (models, import, export, diff, constraints) and contract

**Files:**
- Create: `include/twin/studio/engine_routes.hpp`, `src/studio/engine_routes.cpp`, `api/studio-engine.openapi.yaml`
- Modify: `src/studio/CMakeLists.txt` (source + `twin::authoring`), `scripts/openapi/*` (check both files)
- GUI session: one registration call in `src/studio/server.cpp`
- Test: `tests/studio/engine_routes_test.cpp` (in-process httplib server)

**Routes:**
- `POST /api/v1/authoring/models/parse` `{source}` → `{model?, diagnostics, symbols, semanticDigest?, contentSha256?}`
- `POST /api/v1/authoring/models/validate` `{model}` → `{valid, diagnostics, semanticDigest, text}`
- `POST /api/v1/authoring/models/format` `{source}` → `{source}` (422 with diagnostics on syntax errors)
- `POST /api/v1/authoring/models/render` `{model, layout?, target}` → `{content, mediaType, roundTripVerified}`
- `POST /api/v1/authoring/models/diff` `{from, to}` → diff
- `POST /api/v1/authoring/constraints/parse` `{text, kind: guard|invariant}` → `{atoms?, diagnostics}`
- `POST /api/v1/authoring/import` `{filename, content, options?}` → ImportResult JSON (200 even with errors; `model` absent)
- `GET  /api/v1/authoring/importers` → `[{id, version, description}]`

- [ ] Step 1: Write the route tests: status codes, shapes, a 400 on malformed JSON, the error envelope.
- [ ] Step 2: Implement the routes and the OpenAPI file, and extend the API check.
- [ ] Step 3: Send the GUI session the registration line and the contract. Run the tests and expect PASS.

### Task 6: Alignment diagnostics, strong mode, pair explanation; compile/align from canonical

**Files:**
- Modify: `include/twin/alignment/alignment.hpp`, `src/alignment/alignment.cpp` (counts of internal transitions, `modes`)
- Create: `include/twin/alignment/diagnostics.hpp`, `src/alignment/diagnostics.cpp`
- Test: `tests/unit/alignment/diagnostics_test.cpp` (fixtures: pump aligned; a variant with an unmatched PT label; one with a missing interpretation; a behavioural mismatch from the corpus)

**Interfaces:**
```cpp
// AlignmentEvidence additions: std::size_t pt_internal_transitions, dt_internal_transitions;
// to_json adds "modes": {"weak": "aligned"|"not_aligned", "strong": "aligned"|"not_aligned"|"not_decidable", "strongReason": "..."}
struct ElementLink { std::string view, kind, name; std::optional<std::size_t> index; };
struct AlignmentDiagnostic { std::string category, severity, message; std::vector<ElementLink> links; };
std::vector<AlignmentDiagnostic> diagnose(const AlignmentEvidence&, const AlignmentInputs&);
json::Json to_json(const std::vector<AlignmentDiagnostic>&);
struct PairExplanation { bool equivalent{false}, pt_implies_dt{false}, dt_implies_pt{false}; };
Result<PairExplanation> explain_pair(const AlignmentInputs&, std::string_view pt_label, std::string_view dt_label);
```

**Categories:** `outside_fragment`, `missing_interpretation`, `receive_label`,
`tautological_interpretation`, `unmatched_pt_event`, `unmatched_dt_event`,
`state_inconsistency`, `behavioural_mismatch`, `ontology_equivalence`.

- [ ] Step 1: Write the tests (one per category reachable from fixtures). The strong mode is
  "aligned" for τ-free pump views and "not_decidable" for the drone (its PT has τ). Also test
  `explain_pair` directions.
- [ ] Step 2: Implement it, and add `/api/v1/authoring/alignment/explain` to the engine routes.
- [ ] Step 3: Run the tests and expect PASS. The existing alignment and package tests stay green.

### Task 7: Property language, monitors document, design-time checks

**Files:**
- Create: `include/twin/monitoring/{property.hpp,monitors.hpp}`, `src/monitoring/{property.cpp,monitors.cpp,CMakeLists.txt}` (deps core, json, ir, kernel)
- Create: `include/twin/authoring/properties.hpp`, `src/authoring/properties.cpp` (deps `twin::monitoring`, `aligner::dtpta`, `aligner::semalign`)
- Test: `tests/unit/monitoring/property_test.cpp`, `tests/unit/authoring/properties_test.cpp`

**Interfaces:**
```cpp
namespace twin::monitoring {
struct Property;                                          // AST (quantifier, state formula)
Result<Property> parse_property(std::string_view text);  // positions in errors
std::string print_property(const Property&);
struct Verdict { std::string value; };                    // "satisfied" | "violated" | "inconclusive"
Verdict evaluate_on_states(const Property&, const kernel::Model&, const kernel::StateSet&); // A[] state formulas without sem()
struct MonitorSpec { std::string id, kind, name, severity; json::Json config; };
struct MonitorsDocument { std::vector<json::Json> requirements; std::vector<MonitorSpec> monitors; std::vector<json::Json> alerts; };
Result<MonitorsDocument> monitors_from_json(const json::Json&);  json::Json to_json(const MonitorsDocument&);
}
namespace twin::authoring {
struct PropertyAnalysis { bool valid; std::vector<Diagnostic> diagnostics; std::string quantifier;
  std::vector<std::string> locations, clocks, symbols; std::string design_time, runtime, evaluator; std::vector<std::string> reasons; };
PropertyAnalysis analyse_property(std::string_view text, const Model& dt, const std::optional<std::string>& ontology_text);
json::Json to_json(const PropertyAnalysis&);
struct PropertyCheckInputs { std::string property; std::string dt_xml; std::optional<std::string> ontology_text, dt_interpretation_text; };
Result<json::Json> check_property(const PropertyCheckInputs&, const std::function<void(std::string_view)>& progress = {}); // twin-property-evidence/1
std::vector<Diagnostic> validate_monitors(const monitoring::MonitorsDocument&, const Model* pt, const Model* dt,
                                          const json::Json& telemetry_schema);
}
```

- [ ] Step 1: Write the tests:
  - parser precedence and errors;
  - analysis classification for each row of the spec §4.4 table;
  - `A[] !FAULT` holds or is violated on small fixtures, with a witness path of locations;
  - `E<> COOLING` holds;
  - a clock bound above M(x) is refused as unsupported;
  - `sem(...)` guaranteed / not guaranteed on the pump DT, listing the locations;
  - `evaluate_on_states` gives satisfied, violated and inconclusive on hand-built state sets;
  - monitors document validation: unknown field, unknown label, a bad property.
- [ ] Step 2: Implement it. Use the zone graph from `dtpta::TimedAutomaton(const char* xml)`,
  `construct_zone_graph()`, `get_successors` and `get_zone_state`, with a BFS keeping parents;
  intersect each zone with ¬φ in DNF. Entailment uses `dtpta::Ontology::entails(I_D(L) → φ_L)`.
- [ ] Step 3: Add `/api/v1/authoring/properties/{analyse,check}` and `/api/v1/authoring/monitors/validate` to the routes. Run the tests and expect PASS.

### Task 8: Package additions and runtime monitors

**Files:**
- Modify: `include/twin/package/builder.hpp`, `src/package/builder.cpp` (optional canonical models, monitors, property evidence, type metadata; build-time check that the canonical rendering equals the shipped XML)
- Modify: `src/package/package.cpp` (verify the new roles when present)
- Modify: `src/runtime/{monitor_host.cpp,cosim_driver.cpp,host.cpp,api_server.cpp,views.cpp}`, `apps/twin-runtime/main.cpp` (`--instance`), `api/runtime.openapi.yaml`, `docs/runtime-api.md`
- Create: `include/twin/runtime/monitors.hpp`, `src/runtime/monitors.cpp` (MonitorSet: loads `monitors.json`, evaluates behavioural properties after each committed record, records changes as `context` records with topic `monitor`)
- Test: `tests/unit/package/package_additions_test.cpp`, `tests/unit/runtime/monitors_test.cpp`

**Runtime contract:**
- `GET /runtime/monitors` → `{monitors: [{id, kind, name, severity, evaluator, status, since, detail}], package_hash, monitors_sha256}`.
- Conformance options `events` and `unmatchedEvents` are applied by `MonitorHost::pt_event`. With
  `record`, a raise_alarm with code `unmatched_event` is written and 200 is returned with
  `accepted:false`.
- Monitor mode also gets `POST /runtime/advance` `{ticks|time}`: a time heartbeat from the
  ingestion layer, so that deadlines are detected without waiting for the next event.

- [ ] Step 1: Write the tests:
  - a package with monitors builds, verifies and lists the roles; a tampered `monitors.json`
    fails verification;
  - the runtime records a `monitor` context when `A[] !FAULT` becomes violated;
  - an `unmatchedEvents` policy test;
  - an `--instance` test (genesis carries the instance);
  - an advance test (a missed deadline shows as a rejection).
- [ ] Step 2: Implement it, and update the OpenAPI file and the docs.
- [ ] Step 3: Run the tests (release and studio-release) and expect PASS.

### Task 9: Sandbox previews and scenario runner

**Files:**
- Create: `include/twin/sandbox/{preview.hpp,scenario.hpp}`, `src/sandbox/{preview.cpp,scenario.cpp,CMakeLists.txt}` (Studio build only; deps `twin::runtime_shell`, `twin::monitoring`, `twin::ontology`, `twin::authoring`)
- Modify: `src/studio/engine_routes.cpp` (sandbox routes), `api/studio-engine.openapi.yaml`
- Test: `tests/studio/sandbox_test.cpp`

**Interfaces:**
```cpp
struct PreviewSource { std::optional<std::filesystem::path> package_dir;                  // full preview (PT via E)
                       std::optional<authoring::Model> dt_model; std::optional<std::string> dt_interpretation, ontology;
                       std::optional<json::Json> monitors; };
class PreviewSession {
 public:
  static Result<std::unique_ptr<PreviewSession>> create(const PreviewSource&, const std::filesystem::path& sandbox_dir);
  std::string id() const;  json::Json state() const;   // locations, clocks, enabled, propositions (+truth), monitors, time, banner
  Result<json::Json> advance(Ticks to);  Result<json::Json> event(std::string_view label, std::string_view level);
  Result<json::Json> observe(const json::Json& values);  json::Json trace() const;
};
class PreviewRegistry { /* id -> session, LRU of 16, idle expiry 30 min */ };
Result<json::Json> run_scenario(const PreviewSource&, const json::Json& scenario, const std::filesystem::path& sandbox_dir); // twin-scenario-report/1
```

- [ ] Step 1: Write the tests:
  - a DT-only preview of the pump DT: `restart!`, then advance, then the state;
  - a PT-level preview from a built package (`start_cmd!` through E);
  - observations give interpretation truth;
  - the ledger lives under the sandbox directory, the execution id starts with `preview-`, and
    nothing appears in the production ledger directory;
  - the pump overheating scenario passes; a scenario with a wrong expectation fails with the
    actual value.
- [ ] Step 2: Implement it, plus the routes:
  - `POST /api/v1/sandbox/previews` `{packageId?|packageDir?|dtModel?, dtInterpretation?, ontology?, monitors?}`;
  - `GET /api/v1/sandbox/previews/{id}`;
  - `POST /api/v1/sandbox/previews/{id}/{advance,event,observe}`;
  - `GET /api/v1/sandbox/previews/{id}/trace`;
  - `DELETE /api/v1/sandbox/previews/{id}`;
  - `POST /api/v1/sandbox/scenarios/run`.
- [ ] Step 3: Run the tests and expect PASS.

### Task 10: Model editors (web/studio/src/authoring)

**Files:**
- Create: `web/studio/src/authoring/{types.ts,api.ts,commands.ts,ModelEditor.tsx,ModelDiagram.tsx,DiagramCanvas.tsx,Inspector.tsx,SourceTab.tsx,ValidationTab.tsx,PropertiesTab.tsx,twinta.ts,authoring.css,index.ts}`
- Test: `web/studio/src/authoring/commands.test.ts`, `web/studio/src/authoring/ModelEditor.test.tsx`

**Components:**
- `ModelEditor` `{value: Model, layout: Layout, onChange(model, layout), readOnly?, onSelect?(ref)}`.
- `ModelDiagram` `{model, layout, highlight?: ElementRef[], onSelect?}`.

**Commands** (pure, unit-tested): `addLocation`, `renameLocation` (edges, layout), `deleteLocation`
(incident edges), `setInitial`, `addEdge`, `deleteEdge`, `setEdgeEnds`, `setGuard`, `setSync`,
`setResets`, `setInvariant`, `addClock`, `addChannel`, `addConstant`, `renameClock`, `setNote`,
`moveLocation`. The undo/redo stack holds `{model, layout}` snapshots.

**Sync:** Source tab edits → debounced `parse` → `onChange` when a model is returned →
Diagram. Diagram edits → `validate` (debounced) → diagnostics. Switching to Source prints via
`render target=twinta`. Constraint fields use `constraints/parse`.

- [ ] Step 1: Write `commands.test.ts`, with a test per command, including that a rename
  updates edges and layout and that undo restores. Write `ModelEditor.test.tsx` (MSW):
  - add a location via the toolbar;
  - connect an edge;
  - the inspector edits a guard (the backend parse is mocked);
  - the source tab shows the printed text;
  - typing invalid text shows a diagnostic;
  - read-only hides the editing tools.
- [ ] Step 2: Implement it with the existing design system and tokens.
- [ ] Step 3: `npm test`, `npm run typecheck` and `npm run lint` all pass.

### Task 11: `twin::ingest` core (bindings, mapping, units, data quality)

**Files:** Create `include/twin/ingest/{bindings.hpp,observation.hpp,mapping.hpp,units.hpp,quality.hpp}`,
`src/ingest/{bindings.cpp,mapping.cpp,units.cpp,quality.cpp,jsonpath.cpp,CMakeLists.txt}`
(deps core, json). Test `tests/unit/ingest/{mapping_test.cpp,quality_test.cpp,bindings_test.cpp}`.

**Interfaces:**
```cpp
struct SourceSpec { std::string id, adapter; json::Json config; };
struct Selector { std::string channel; std::string path; std::optional<json::Json> equals; };
struct ObservationBinding { std::string field, source; Selector select; std::string type, unit;
  std::optional<std::string> source_unit; double scale{1}, offset{0}; std::string timestamp_path, timestamp_format;
  std::string quality_path; std::vector<json::Json> quality_good; std::string missing; };
struct EventBinding { std::string label, source; Selector select; };
struct Bindings { std::vector<SourceSpec> sources; std::vector<ObservationBinding> observations;
                  std::vector<EventBinding> events; std::string time_basis{"source"}, late_policy{"reject"}; double unit_seconds{1}; };
Result<Bindings> bindings_from_json(const json::Json&);  json::Json to_json(const Bindings&);
std::vector<authoring-like Diagnostic> validate_bindings(const Bindings&, const json::Json& telemetry_schema, const std::vector<std::string>& pt_labels);
struct RawMessage { std::string source, channel; json::Json payload; std::int64_t received_ms{0}; };
struct CanonicalObservation { std::string field; json::Json value; std::string type, unit, quality, source;
                              std::int64_t observed_ms{0}, received_ms{0}; json::Json raw; };
struct CanonicalEvent { std::string label, source; std::int64_t observed_ms{0}, received_ms{0}; json::Json raw; };
struct Finding { std::string monitor, check, field, severity, message; std::int64_t at_ms{0}; };
struct MappingOutput { std::vector<CanonicalObservation> observations; std::vector<CanonicalEvent> events; std::vector<Finding> findings; };
class Mapper { public: explicit Mapper(Bindings); MappingOutput map(const RawMessage&) const; };
class QualityMonitor { public: QualityMonitor(json::Json telemetry_schema, std::vector<monitoring::MonitorSpec>);
  std::vector<Finding> on_observation(const CanonicalObservation&); std::vector<Finding> on_event(const CanonicalEvent&);
  std::vector<Finding> tick(std::int64_t now_ms, const std::map<std::string, bool>& source_connected); json::Json status() const; };
```

- [ ] Step 1: Write the tests:
  - JSON path selection, including arrays;
  - type coercion and refusal;
  - unit conversion between degF/degC/K, bar/kPa/psi and ms/s; an incompatible unit is an
    error at validation;
  - each timestamp format;
  - quality mapping;
  - each missing policy;
  - an event selector with `equals`;
  - each data-quality check (stale, missing, invalid_type, out_of_range, clock_regression,
    duplicate, disconnected).
- [ ] Step 2: Implement it. Step 3: run the tests and expect PASS.

### Task 12: Adapters, MQTT client, pipeline, connection test, `twin-ingest`

**Files:** Create `include/twin/ingest/{adapter.hpp,mqtt.hpp,pipeline.hpp,runtime_client.hpp,connection_test.hpp}`,
`src/ingest/{adapter.cpp,adapter_simulator.cpp,adapter_replay.cpp,adapter_rest.cpp,adapter_mqtt.cpp,adapter_opcua.cpp,mqtt.cpp,pipeline.cpp,runtime_client.cpp,connection_test.cpp}`,
`apps/twin-ingest/{main.cpp,CMakeLists.txt}`, `tests/support/mini_mqtt_broker.{hpp,cpp}`. Test
`tests/unit/ingest/{mqtt_test.cpp,adapters_test.cpp,pipeline_test.cpp}`, `tests/e2e/ingest_runtime_test.cpp`.

**Interfaces:**
```cpp
class Sink { public: virtual void on_message(const RawMessage&) = 0; virtual void on_status(std::string_view source, bool connected, std::string_view detail) = 0; };
class Adapter { public: virtual Status start(Sink&) = 0; virtual void stop() = 0; virtual json::Json status() const = 0; };
Result<std::unique_ptr<Adapter>> make_adapter(const SourceSpec&);   // opcua -> ErrorCode::Unavailable "adapter_unavailable"
struct PipelineConfig { Bindings bindings; json::Json telemetry_schema; std::vector<monitoring::MonitorSpec> quality_monitors;
                        std::string runtime_url; std::int64_t ticks_per_unit{1000}; std::int64_t heartbeat_ms{1000}; };
class Pipeline { public: static Result<std::unique_ptr<Pipeline>> start(PipelineConfig); void stop(); json::Json status() const;
                 void subscribe(std::function<void(std::string_view topic, const json::Json&)>); };
Result<json::Json> test_source(const Bindings&, std::string_view source_id, std::chrono::milliseconds timeout);
```

**`twin-ingest`:** `--config FILE --runtime URL --port N --instance ID`.
Routes: `GET /health`, `GET /status`, `GET /stream` (SSE: `observation`, `event`, `finding`,
`source`), `GET /observations/latest`.

- [ ] Step 1: Write the tests:
  - MQTT against `mini_mqtt_broker`: connect, subscribe with a wildcard, receive QoS 0
    publishes, keepalive ping, reconnect after the broker restarts;
  - the simulator adapter emits the pump feed's events in order;
  - the replay adapter paces a JSONL file;
  - the REST adapter polls a local httplib server;
  - opcua gives `adapter_unavailable`;
  - the pipeline forwards telemetry and events to an in-process runtime, rejects late events
    with a finding, and sends heartbeats as `advance`;
  - `test_source` returns the raw payload and the mapped values.
- [ ] Step 2: Implement it. The e2e test: `twin-runtime --monitor` plus `twin-ingest` with the
  pump simulator binding gives the same ledger records as `twin-pt-feed` does today.
- [ ] Step 3: Run the tests and expect PASS. Add `POST /api/v1/ingest/test` to the engine routes.

### Task 13: `twin::deploy` supervisor

**Files:** Create `include/twin/deploy/{process.hpp,supervisor.hpp}`, `src/deploy/{process.cpp,supervisor.cpp,CMakeLists.txt}`.
Test `tests/e2e/supervisor_test.cpp`.

**Interfaces:**
```cpp
struct SupervisorConfig { std::filesystem::path bin_dir, data_dir; std::vector<std::filesystem::path> package_store;
                          int port_min{18100}, port_max{18999}; std::chrono::milliseconds health_timeout{30000}; };
struct RuntimeOptions { std::optional<double> speed; bool paused{false}; bool deterministic{false}; };
struct InstanceSpec { std::string instance_id; std::filesystem::path package_dir; std::string package_hash; std::string mode;
                      std::optional<std::string> world_url; std::optional<json::Json> ingest_config; RuntimeOptions options; };
struct InstanceStatus { std::string instance_id, state, runtime_url, ingest_url, package_hash, last_error, started_at;
                        int runtime_pid{0}, ingest_pid{0}; std::filesystem::path log_dir; };
class Supervisor { public: static Result<std::unique_ptr<Supervisor>> create(SupervisorConfig);
  Result<InstanceStatus> start(const InstanceSpec&); Result<InstanceStatus> replace(const InstanceSpec&);
  Status stop(std::string_view id, std::string_view reason); std::vector<InstanceStatus> status() const;
  std::optional<InstanceStatus> status(std::string_view id) const; void on_change(std::function<void(const InstanceStatus&)>); void stop_all(); };
json::Json to_json(const InstanceStatus&);
std::filesystem::path current_executable_dir();
```

- [ ] Step 1: Write the tests, using real binaries from `build/*/bin`:
  - `start` brings up a pump monitor instance with simulator ingest, and runtime health is OK;
  - the ports are stable across `stop` and `start`;
  - `replace` with a second package ends the old session; both ledgers verify;
  - killing the runtime gives the `failed` state with no restart;
  - killing ingest gives a restart;
  - `stop_all` leaves no child processes.
- [ ] Step 2: Implement it with `posix_spawn`, log files under `instances/<id>/logs`, health
  checks over httplib, and a watchdog thread.
- [ ] Step 3: Run the tests and expect PASS. Send the GUI session the integration notes
  (construction in `twin-studio`, reconciliation, observer → `runtime_url` + bridge).

### Task 14: Formal content: drone and pump as types, Thermal Chamber fixtures

**Files:**
- Create: `examples/thermal-chamber/models/{physical.xml,digital.xml,chamber.ont,pt.interp,dt.interp,physical_with_variable.xml}`, `examples/thermal-chamber/{monitors.json,scenarios.json,feed.json,bindings.json}`
- Create: `examples/industrial-pump/{monitors.json,scenarios.json,bindings.json}`, `examples/indoor-drone/{monitors.json,scenarios.json}`
- Create: `examples/*/models/*.tta.json` (canonical forms produced by the importer and checked in for review), `examples/industrial-pump/evolution/pump_dt_v2.xml` (DT change that keeps weak alignment), `examples/industrial-pump/evolution/pump_dt_broken.xml`
- Test: `tests/integration/examples_test.cpp` (every example: import preserved, alignment as expected, monitors valid, scenarios pass)

- [ ] Step 1: Author the Thermal Chamber models (PT: IDLE/HEATING/HOLDING/OVERHEATED/DOOR_OPEN;
  DT: STANDBY/WARMING/STABLE/FAULT/SERVICE). They are τ-free, with labels mapped by the
  interpretations over `chamber.ont` (temperature, temp_setpoint, temp_limit, door_open,
  heater_on).
- [ ] Step 2: Write the test, then make the fixtures pass it (iterate on the models until the aligner says ALIGNED).
- [ ] Step 3: Run the tests and expect PASS.

### Task 15: Acceptance tests (service level), architecture rules, docs, proof, README

**Files:**
- Create: `tests/e2e/studio_journeys_test.cpp` (Studio build: journeys A–D through `Services` + engine APIs + supervisor, with no browser)
- Modify: `tests/architecture/architecture_test.cpp` (new rules)
- Create: `docs/authoring/{README.md,model-format.md,twinta.md,uppaal-import.md,properties-and-monitors.md,scenarios.md,bindings-and-adapters.md,data-quality.md,deployment-supervisor.md}`
- Modify: `docs/architecture.md`, `docs/trusted-computing-base.md`, `docs/final-integration-report.md`, `README.md`, `proof/sections/*` (canonical rendering step; strong vs weak lemma), `Makefile`, `scripts/start-demo.sh`, `apps/macos/launcher.mm`, `scripts/capture-screenshots.sh` (shared; announce first)

- [ ] Step 1: Write the journeys test. The platform parts depend on the GUI session's APIs:
  until they land, the test drives the engine parts directly and the platform parts are added
  when available.
- [ ] Step 2: Write the docs and the proof additions, and rebuild them (`make docs`, `make proof`).
- [ ] Step 3: Run everything: C++ (both presets), Vitest, Playwright, the API check, Doxygen, and the proof build.

## Coordination with the GUI session (dependencies)

| We need from them | For |
|---|---|
| One-line registration of `engine_routes` in `server.cpp` | Tasks 5, 6, 7, 9, 12 |
| Platform: model artefacts accept `twin-ta/1` content, and the pipeline uses `authoring::toolchain_source` | Tasks 3–4 integration |
| Supervisor construction, reconciliation, deploy/rollback calls, `runtime_url` + bridge registration | Task 13 integration |
| Seeding examples as Twin Types using the importer | Task 14 integration |
| Playwright journeys A–D, Studio Guide, screenshots | Task 15 |

| They need from us | When |
|---|---|
| Formats + TS types + headers | Tasks 1–4 (first) |
| `ModelEditor`, `ModelDiagram` | Task 10 |
| Sandbox routes | Task 9 |
| `test_source`, `twin-ingest`, Supervisor | Tasks 12–13 |
