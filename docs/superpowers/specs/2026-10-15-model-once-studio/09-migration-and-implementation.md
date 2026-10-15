# 9. Migration and implementation plan

## 9.1 Mapping to the existing code

| Layer | Reused unchanged | Extended | New | Replaced |
|---|---|---|---|---|
| Formal core | `authoring` (twin-ta/1, validate, render XML, import), `compiler`, `ir`, `kernel`, `alignment`, `monitoring`, `package`, `ledger`, `runtime` | — | — | — |
| Model | — | — | **`twin::model`** (`include/twin/model/`, `src/model/`): document, expressions, validator, semantic/theory/behaviour compilers, contract derivation, lifter, trace | — |
| Toolchain CLI | `twin compile/align/package` | `apps/twin`: `twin model check|compile|lift` | — | — |
| Studio service | release gate, evidence, packages, instances, supervisor, previews, timing, scenarios | `BlueprintService`: a `model` section; on save it compiles and pins the derived artefacts (V_D, K, I_D, and V_P/I_P when a reference exists) and rewrites the derived sections | endpoints `GET …/model/generated`, `POST …/model/lift`, `POST /models/import-ta` | — |
| Ontology services | parser, refinement, evaluation | used to validate generated K/I | — | hand-editing (kept read-only, advanced) |
| Runtime | kernel, sessions | timer service (proposes enabled timeout labels); ingest: condition-event derivation from the generated mapping | — | — |
| Frontend | API client, query keys, live stream, runtime client, `CodeEditor`, xyflow + dagre graph code, TA rendering, knowledge-graph explorer, world canvas, drone plugin | — | design system (`src/design2` → `src/design`), Studio workspace (`src/features/studio2`), model store with operation history | Blueprint section pages (20), wizard, creation chooser, twin engineering/maintenance pages |

## 9.2 Compatibility

- **Blueprints without a `model` section** keep working exactly as today (their sections remain
  editable; the old editors stay reachable under *Advanced → Legacy editors* until migration).
- **Blueprints with a `model` section** are *model-driven*: derived sections and pins are written
  by the server; direct edits to them are refused with `409 derived_section`.
- **Existing formal artefacts** (UPPAAL/TwinTA models, `.ont`, `.interp`) are migrated by
  **lifting** (3.9): the DT view, K and I_D become a model whose compilation is checked to be
  equivalent (same IR up to naming; interpretation entries equivalent under K, decided by Z3 through
  the existing refinement/entailment services). If the check fails, the model is not adopted and
  the Blueprint stays legacy, with the differences listed.
- **The PT view** becomes the model's physical reference, with the label mapping taken from the
  data-contract `formal` pairs and the location mapping from equal interpretations.
- **Packages and bundles** keep their formats (`twin-package/1`, `twin-bundle/1`); bundles of
  model-driven versions also contain `model.json`. Published versions are immutable, so every
  existing package stays deployable and replayable.
- **Instances** keep their ids and pinned versions; upgrading a twin to a model-driven version is
  an ordinary new-version deployment.
- **API**: `/api/v1` stays; model endpoints are additive.

## 9.3 Phases (each shippable, tests green at every step)

| Phase | Content | Done when |
|---|---|---|
| **1. Language core** | `twin::model`: document parse/validate, expressions → SMT, behaviour compiler (instantiation, groups, timeouts, links, product), K/I_D/I_P, OWL/SHACL/RDF, contract derivation, trace, lifter; `twin model` CLI | unit tests per rule; golden files; the thermal chamber model compiles to a DT equal to the tutorial's and **aligns with the tutorial's independent PT** through the real aligner; lifting round-trips every example and the aligner corpus |
| **2. Studio service** | `model` section, derivation on save, generated-artefact endpoints, import of a TA as a model | `tests/studio` cover save → pins → release gate; existing Blueprint tests unchanged |
| **3. Design system** | tokens, fonts, components of chapter 8 | component tests; old pages still render (tokens aliased) |
| **4. Studio workspace** | frame, Explorer, Structure canvas, Inspector, Behavior canvas, Generated views, Problems/Checks drawer, Publish dialog, Models home, New model | vitest for the store and operations; Playwright: build the thermal chamber through the canvas |
| **5. Runtime additions** | timer service, condition-event derivation in ingest | runtime/ingest tests; e2e: a condition event moves a deployed twin |
| **6. Operate consolidation** | twin pages reduced to Overview, Behavior, Data, History, Connection, Settings; Connections area | Playwright suite on the new IA; redirects for old URLs |
| **7. Migration** | examples converted (thermal chamber, pump; drone via lifting), templates as models, docs and screenshots | every example opens model-driven; tutorial rewritten |

## 9.4 Risks

| Risk | Mitigation |
|---|---|
| Product explosion for many machines | 2 000-location limit with a per-machine breakdown; most twins have one or two machines |
| Lifted models of foreign TAs look "flat" | the advanced constructs make lifting total; Studio offers *Group transitions* and *Use state timer* refactorings that keep the compiled TA identical |
| Users miss data guards | the inspector explains the two supported patterns and offers to create a condition event |
| Ontology engineers want to write K | assumptions cover extra axioms; legacy text editing stays under Advanced for legacy Blueprints |
| Font availability offline | fonts are bundled with the app (no CDN) |
