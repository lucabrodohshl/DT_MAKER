# 5. Frontend information architecture

## 5.1 Areas

The product is organised around three user goals instead of two modes with 68 pages.

| Area | Goal | Who |
|---|---|---|
| **Models** | design, check and publish a digital twin model | engineers |
| **Twins** | run, watch, operate and audit deployed twins | operators, engineers |
| **Connections** | define brokers, endpoints and gateways once, use them in many twins | integrators |

A fourth, rarely used place — **Library** (shared type libraries, evidence, packages, engineering
audit, system logs) — sits in the user menu, not in the main navigation.

```
/                          → /twins (operators) or /models (last used)
/models                    Models home: recent models, drafts, templates, import
/models/new                New model: name + start (blank, template, import TA, import bundle)
/models/:id[/v/:n]         THE STUDIO — one workspace, everything below is panels/views
      ?view=structure|behavior|generated     (the canvas view)
      &focus=<element id>                    (selection, shareable)
      &gen=graph|ontology|theory|automaton|xml (generated sub-view)
/twins                     All twins: state, health, alerts, model version
/twins/:id                 Twin overview (live)
/twins/:id/behavior        Live state machine, what can happen next, what-if
/twins/:id/data            Signals, events, alerts (one timeline)
/twins/:id/history         Ledger, provenance, replay
/twins/:id/connection      Plant connection: endpoint/broker, mapping, test, status
/twins/:id/settings        Runtime, storage, logs, deployment
/connections               Brokers, endpoints, gateways; usage by twins
/library/*                 Shared types, evidence, packages, audit, logs (advanced)
```

## 5.2 Where every existing page goes

| Existing | New place | How |
|---|---|---|
| Studio home, New Blueprint, 5-way chooser, 11-step wizard | Models home, New model | one dialog: name + start; no wizard (the canvas *is* the guide) |
| Overview (completeness, next steps, gate) | Studio header + Problems drawer | the header shows check state and Publish; Problems lists what blocks |
| Build → Structure | Structure view (canvas + inspector) | |
| Build → World & Layout | Structure view → *Arrange: Graph / Map* | same assets, spatial arrangement; layers in the inspector |
| Build → Data & Connectivity: data contract | **derived** | measured properties, events, actions |
| Build → Data & Connectivity: sources/bindings | Connections + twin Connection | integration is per deployment |
| Build → Presentation | **derived** + state inspector (label, tone) | dashboards are pinned from Operate |
| Behavior → Physical System View | Behavior view → *Physical reference* (advanced) | import + mapping table |
| Behavior → Digital Twin View | Behavior view (authoring) + Generated → Automaton (read-only) | |
| Semantics → Ontology | Generated → Domain theory / Ontology (read-only) + Assumptions in the inspector | |
| Semantics → Interpretations | **derived** (state meanings, event conditions) | visible in Generated → Domain theory |
| Semantics → Cross-layer binding | **removed** | gaps cannot occur; unbound signals are twin-level problems |
| Assurance → Requirements | inspector of a state ("Never reach") + Checks panel (advanced) | |
| Assurance → Monitors | **derived** + Checks panel for custom properties | |
| Assurance → Alignment | Checks panel → Alignment | |
| Assurance → Verification | Checks panel | |
| Test → Scenario Builder, Preview | **Simulate** mode of the Behavior view | one timeline, saved runs become scenarios |
| Release → Package | Publish dialog (one step) + Library → Packages | |
| Release → Instances, Deployment | Twins → New twin (from a model) | |
| Operate: Overview | Twin overview | |
| Operate: Assets, Knowledge graph | Twin overview (assets as a tree) + Generated → Graph | |
| Operate: Operations (live, telemetry, events) | Twin → Data | one timeline with filters |
| Operate: Plant connection | Twin → Connection | |
| Operate: Behaviour (state, model, facts, conformance, monitors) | Twin → Behavior | one page: live machine + checks |
| Operate: Predict (what-if, predictions, planning) | Twin → Behavior (what-if panel); drone planning in its plugin view | |
| Operate: Audit (timeline, provenance, replay, ledger) | Twin → History | tabs |
| Operate: Engineering (models, ontology, interpretations, verification, package, deployment) | **removed** — "Open model" | duplicates of the Studio |
| Operate: Maintenance (changes, impact, versions, readiness, rollback) | Twin → Settings (version, rollback) + Studio version menu (compare/impact) | |
| Operate: Administration | Twin → Settings | |
| Studio library: models/ontologies/interpretations/evidence/packages/changes/refinement/impact/history/deployments/audit | Library (advanced) | |

Count: **≈ 16 routed screens** (Models 3, Studio 1 with views, Twins 7, Connections 2, Library 3)
instead of ≈ 68.

## 5.3 Classification of functionality

| Class | Content |
|---|---|
| **Core** | types, assets, properties, relationships, events, actions, states, transitions, time limits and windows; structure and behaviour views; problems; publish; twins list and overview |
| **Contextual** | units, ranges, periods, re-arm conditions, links, groups, meanings, tones, values per asset; generated trace for the selection; twin connection mapping; what-if from the current state |
| **Advanced** | named timers, constants, diagonal limits, internal transitions, inheritance, external parts, assumptions, nondeterminism, physical reference and mapping, alignment details, requirements in property syntax, generated sources (Turtle, SMT, XML), libraries, IRI base, scenarios as files, ledger internals |
| **Internal** | IR, package layout, hashes beyond one "digest" line, label mangling, ticks per unit (fixed at 1000 unless changed under Advanced), runtime ports, supervisor processes, bundle formats |

## 5.4 Questions asked of every screen

- *Does it deserve a page?* Only the Studio, a twin, the lists and Connections do. Everything else
  is a view, a panel, a drawer or a popover.
- *Can it happen on the canvas?* Creating assets, types, relationships, links, states and
  transitions does; renaming does (inline); moving between parts does (drag into a container).
- *Is it a duplicate?* Engineering/maintenance pages under each twin were; Presentation was; the data
  contract was.
- *Is it advanced?* If a beginner building a first twin never needs it, it is hidden until used.
