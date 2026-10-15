# 1. Current-state audit

Inspected: `web/studio/src` (34 105 lines, React 19 + TypeScript, xyflow, dagre, CodeMirror,
TanStack Query), `src/` (45 955 lines of C++20), `examples/`, `docs/studio/`, the specs of
2026-10-03/05/06, and the screenshots in `docs/screenshots/`.

## 1.1 What the system is (and what must not be lost)

The backend is a serious piece of formal engineering and the redesign keeps all of it.

| Capability | Where | Kept as |
|---|---|---|
| Canonical timed-automaton model `twin-ta/1`, UPPAAL import/export, TwinTA text | `src/authoring` | the target of the behaviour compiler and the input of the lifter |
| Compiler with translation validation into the Twin IR | `src/compiler`, `src/ir` | unchanged; fed with generated XML |
| Semantic aligner (SemPTDTAlignmentICSE, Z3) | `src/alignment`, `SemPTDTAlignmentICSE/` | unchanged; fed with generated K, I_P, I_D |
| Semantic kernel, sessions, ledger, replay, what-if, timing windows | `src/kernel`, `src/runtime`, `src/ledger` | unchanged |
| Ontology parser, refinement (Def. 4), interpretation evaluation | `src/ontology` | validates generated K; refinement between model versions |
| Blueprint versions, release gate, Verified Core Package + Deployment Bundle, supervisor | `src/studio`, `src/package` | unchanged; the `model` section is one more section |
| Plant connectivity (REST/MQTT), event-script and drone simulators | `src/ingest`, `src/ptfeed`, `src/world`, `src/scene` | unchanged; mapping generated from the model |
| Monitors and property language | `src/monitoring` | monitors derived from the model |

The supported formal fragment (`docs/supported-model-fragment.md`) is the hard constraint on the
language: **one template, clocks only (no data variables), plain channels, conjunctions of
`x ⋈ c` in guards and of `x ⋈ c` / `x − y ⋈ c` in invariants, resets to zero, the initial
location first**. Interpretations are SMT-LIB2 formulas over the ontology; event interpretations
exist only for send labels `a!`.

## 1.2 The modelling workflow today

Building the tutorial's thermal chamber (`docs/studio/tutorial-first-twin.md`) takes twenty steps
across fourteen pages: New Blueprint → Identity → Structure (types, assets, relationships) → World
→ Data & Connectivity (signals, events with PT and DT labels, commands, sources, bindings) →
Physical System View → Digital Twin View → Ontology → Interpretations (I_P and I_D) → Cross-layer
binding → Requirements → Monitors → Verification → Alignment → Scenario → Package → Instance →
Deployment → Operate → Monitors.

## 1.3 The same facts, maintained in many places

Counted in `examples/thermal-chamber` (occurrences of the identifier per file; the user writes
every one of them):

| Fact | blueprint.json | DT model | PT model | ontology | I_P | I_D |
|---|---|---|---|---|---|---|
| state `HEATING` | 3 | 4 | 7 | – | 1 | 1 |
| state `OVERHEATED` | 6 | 3 | 5 | – | 1 | 1 |
| signal `temperature` | 36 | 1 | 1 | 7 | 5 | 6 |
| signal `heater_on` | 10 | – | 2 | 1 | 6 | 5 |
| limit `temperature_limit` | – | – | – | 3 | 5 | 5 |

Concrete duplications with no link between the copies:

- **The temperature limit exists three times**: `max_temperature` default `80` on the asset type
  `ThermalChamber` (Structure), `axiom limit_val : (= temperature_limit 80)` (Ontology), and the
  text "never exceeds the 80 degC limit" (Requirement REQ-S1). Changing one changes nothing else.
- **The plausible range exists three times**: telemetry `range {-40, 200}` (Data), the
  data-quality monitor `temperature-range {min −40, max 200}` (Monitors), `axiom ambient_bound :
  (>= temperature (- 40))` (Ontology).
- **Every state meaning is written twice**: `pt.interp` and `dt.interp` hold identical location
  formulas for `IDLE`, `HEATING`, `OVERHEATED`.
- **Every event is declared four times**: a data-contract event with `formal: {pt, dt}` labels, a
  channel in each timed automaton, and an entry in each interpretation.
- **Every state is named again in Presentation** (`states.HEATING.label = "Heating"`, tone) and in
  monitor formulas as strings (`A[] !OVERHEATED`, `A[] (HEATING -> t <= 600)`) that a rename does
  not reach.
- **Each signal is bound three times**: `ontologySymbol` on the signal, a binding in Connectivity,
  a plant-connection mapping per instance.

The product compensates with a consistency page instead of preventing inconsistency: **Semantics →
Cross-layer binding** reports 81 gaps on the drone Blueprint (`docs/screenshots/studio/
studio-cross-layer-binding.png`).

## 1.4 Implementation details the user must understand

- Channel syntax `a!` versus event ids versus labels: three names for one event.
- The PT/DT distinction and label equivalence E before anything works.
- SMT-LIB2 prefix syntax (`(and heater_on (not door_open) (<= temperature temperature_limit))`)
  for every state and event meaning.
- Clocks and resets (`t := 0` on every edge) for "time spent in this state".
- Ticks per unit, runtime mode, model id, plugin id at creation time.
- Instance-scoped versus context assets, parent ids, scope selects.
- Artefact versions, pins and evidence hashes in the overview.

## 1.5 Navigation and screens

| Area | Pages | Notes |
|---|---|---|
| Operate, per twin (`twinNav.ts`) | 9 groups, 33 pages | Engineering (6) and Maintenance (5) repeat Studio pages |
| Studio, per Blueprint version (`nav.ts`) | 7 groups, 20 pages | one page per document section |
| Studio library (`router.tsx`) | ~16 pages | artefact versions, evidence, packages, changes, impact, audit |
| Creation | 5-way chooser + 11-step wizard | |
| Total routed pages | ~68 | plus ~20 redirects for earlier URLs |

Unnecessary navigation (examples):

- Adding an event that changes a state: Data & Connectivity (event, PT label, DT label) → DT view
  (channel, edge) → PT view → Interpretations (two files) → Presentation (label) → back to
  Verification. Six pages for one concept.
- Checking whether a signal is meaningful: Cross-layer binding, then Fix links back to four
  different editors.
- Releasing: Verification → Package → Instances → Deployment → Operate.

## 1.6 The frontend code

- **Two component vocabularies**: `src/design/components` (Panel, Callout, PageHeader, StatusBadge…)
  and `src/features/blueprint/ui.tsx` (EdPage, Pane, InspectorSection, TextField…) solve the same
  problems differently.
- **Three timed-automaton renderers**: `engineering/ta/TaDiagram.tsx`, `blueprint/ta/TaCanvas.tsx`,
  `behavior/BehaviorGraph.tsx`; plus graph code in `StructurePage`, `DataPage`, `ImpactPage`,
  `KnowledgeGraphPage`.
- **Styling sprawl**: 442 inline `style={{…}}` objects, 434 CSS classes over 10 stylesheets,
  39 hard-coded hex colours in TSX, 219 `Callout` uses (explanatory banners on almost every page),
  329 status-badge uses, 181 `Panel`s.
- **Two undo histories**: Blueprint sections and the timed-automaton editor each have their own.
- **Section-oriented store** (`editor.tsx`): edits are whole-section replacements, so an operation
  has no name, no precise patch and no cross-section atomicity beyond hand-written refactors
  (`refactor.ts`).

## 1.7 The look

What reads as "assembled" rather than designed (see `docs/screenshots/studio/*.png`):

- pastel pill badges everywhere — *DRAFT*, *Valid*, *All changes saved*, *RELEASE BLOCKED*, *Live*,
  *Fresh* — competing for attention in one header;
- an info banner restating the page purpose on nearly every page;
- cards inside cards (Completeness, This version, Sections grid of 12 cards each saying *Valid*);
- a green check on every navigation item, so nothing stands out;
- uppercase letter-spaced micro-headings, pill-shaped segmented controls, rounded 10 px cards with
  shadows — the default palette of generated UIs.

What works and is kept: the **graph rendering** — white nodes with a thin border, bold name, mono
metadata, small white label chips on thin curved edges, dotted containment, dagre layout, the live
legend (current, enabled now, enabled after a delay, last taken).

## 1.8 Necessary, overlapping, historical

| Concept | Verdict |
|---|---|
| Asset types, assets, relationships | necessary |
| World & Layout | necessary for spatial twins; becomes a second arrangement of the same assets |
| Data contract (signals, events, commands) | **overlapping**: derivable from measured properties, events and actions |
| Connectivity sources/bindings | necessary, but deployment-level: moves to Twins/Connections |
| Presentation | **overlapping**: state labels/tones belong to states; dashboards are Operate preferences |
| PT view | necessary as an *independent* reference for alignment; advanced |
| DT view | **historical** as a hand-edited artefact: derived from behaviour |
| Ontology K | **historical** as hand-written text: derived from properties, constants, assumptions |
| Interpretations I_P, I_D | **historical** as files: I_D from meanings, I_P from the reference mapping |
| Cross-layer binding | **exists because of the duplication**; disappears |
| Requirements, monitors | necessary; data-quality and conformance monitors become derived |
| Scenario Builder, Preview | necessary; merged into Simulate |
| Package / Instances / Deployment pages | one Publish action plus the Twins area |
| Changes, refinement, impact, history | advanced; per model version |
