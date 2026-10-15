# 7. User journeys

**Counting rule.** A *significant interaction* is a click that navigates or changes something, a
drag, a key command, or entering one field value (typing a name or a number counts once). Opening
a page counts once. Reading does not count. "Current" counts follow
`docs/studio/tutorial-first-twin.md` and the editors as implemented; "proposed" counts follow
chapter 6. The reference twin is the tutorial's thermal chamber (3 sensors/actuators, 3 states,
4 transitions, 1 time limit, 1 cool-down window, an existing controller model for alignment).

## A. Create a new digital twin

| | Current | Proposed |
|---|---|---|
| Steps | Studio → New Blueprint → Blank → Next → name, id, domain, icon, description, runtime mode, time unit, ticks → Create and continue (guided) | Models → New model → name → Create |
| Screens | New Blueprint (2 steps), then a guided wizard over 9 editors | one dialog, then the Studio |
| Interactions | 13 | 4 |

Simplification: ids are derived from names; domain and icon are optional metadata in the
inspector; runtime mode is decided at deployment (whether the twin is fed by a plant or a
simulator); ticks per unit is advanced (1000 by default).

## B. Define a reusable asset type

| | Current | Proposed |
|---|---|---|
| Steps | Structure → Asset types tab → + → id, name, category, description → properties: + key, label, type, default (per property) | Quick add → type `TemperatureSensor` → *New type* → inspector: one line `temperature: real degC measured -40..200` |
| Interactions (1 type, 1 property) | 9 | 4 |

## C. Add an instance of that type

| | Current | Proposed |
|---|---|---|
| Steps | Assets tab → + → id, name, type, scope, parent | drag the type onto the chamber's frame (part, named `probe`) |
| Interactions | 6 | 2 |

## D. Add properties, events and actions

| | Current | Proposed |
|---|---|---|
| A measured signal | Data → Telemetry → + → id, label, type, unit, asset, range ×2, period, ontology symbol; then Ontology → `fun` line; then Connectivity → binding | inspector row (1 line) |
| An event that changes behaviour | Data → Events → + → id, label, asset, PT label, DT label; DT view: channel + edge; PT view; I_P and I_D entries; Presentation label | inspector: `▸ over_temperature when probe.temperature > temperature_limit` |
| An action | Data → Commands → + → id, label, ack event, timeout; DT edge; interpretations | inspector: `◦ reset_latch` |
| Interactions (3 signals, 4 events, 1 action) | ≈ 120 over 6 screens | ≈ 16 on 1 screen |

## E. Connect two assets with a semantic relationship

| | Current | Proposed |
|---|---|---|
| Steps | Relationships tab → + → source, type, target | drag from heater's edge to chamber → choose `heats` |
| Interactions | 4 | 2 |

## F. Define behaviour and timing

| | Current | Proposed |
|---|---|---|
| Steps | DT view: declare clock `t` and 4 channels; double-click ×3, name ×3, invariant `t <= 600`; drag ×4, choose channel ×4, resets `t` ×4, guard `t >= 30`; then I_D for 3 locations + 4 events in SMT-LIB2; then Presentation labels/tones ×3 | Behavior view: double-click ×3 + names ×3; drag ×4 + trigger ×4; *at most 600 s* on HEATING; *not before 30 s* on the reset; meanings ×3 (optional) |
| Interactions | ≈ 47 + 7 formulas | ≈ 20 |

Simplification: the state timer and its resets are implicit; channels are the events and actions
already declared; meanings are written once, in the expression language, next to the state.

## G. Generate and inspect the ontology

| | Current | Proposed |
|---|---|---|
| Steps | write `.ont` by hand (11 lines), validate, then cross-check | press `3`, *Ontology* (or *Domain theory*) |
| Interactions | 11 lines + 2 | 2 |

## H. Generate and inspect the timed automaton

| | Current | Proposed |
|---|---|---|
| Steps | it *is* the hand-written DT view | press `3`, *Automaton*; select a state to see its locations |
| Interactions | — (it is authored) | 2 |

## I. Export the timed-automaton XML

| | Current | Proposed |
|---|---|---|
| Steps | Behavior → DT view → Export → UPPAAL XML → Download | Generated → Automaton → XML ⤓ |
| Interactions | 4 | 3 |

## J. Connect the model to data sources

| | Current | Proposed |
|---|---|---|
| Steps | Blueprint: source + binding per signal (3 × 4); instance: plant connection kind, endpoint, mapping per signal and event | Twins → twin → Connection: choose a Connection (or *New REST endpoint*), mapping table pre-filled from the model's channels, *Test* |
| Interactions | ≈ 25 | ≈ 5 |

## K. Inspect and operate an existing twin

| | Current | Proposed |
|---|---|---|
| See state, values, alerts | Overview; Behaviour → Current state; Operations → Live; Events & alerts | Overview (one page) |
| Request an action | What-if or plugin page | Behavior → *Requestable now*: click |
| Interactions to see all four | 5 pages | 1 page |

## L. Reopen a model and safely modify it

| | Current | Proposed |
|---|---|---|
| Steps | Studio → Blueprint → Create draft from this version → edit in several editors → Verification → Run all checks → Package → Publish → Instances: upgrade | Models → model → *New draft* (version menu) → edit → checks run by themselves → Publish → the twin offers *Upgrade to v4* |
| Safety | per-section revisions, two undo histories, impact by section | one undo history, named operations, impact by element (*what this change affects*: generated artefacts whose digest changes, evidence that becomes stale) |
| Interactions | ≈ 12 + edits | ≈ 5 + edits |

## Totals for the thermal chamber, from nothing to a monitored twin

| | Current | Proposed |
|---|---|---|
| Significant interactions | ≈ 300 (plus ≈ 25 lines of SMT-LIB2 and ontology text) | ≈ 75 (no formal text) |
| Screens visited | 14 editors + 6 release/operate pages | 1 Studio (3 views) + 1 twin |
| Facts entered more than once | 9 identifiers, 3 limits, 2 ranges | none |

## What a beginner needs to know

Assets, properties, events, actions, states and transitions — each explained by an empty state
and a one-line tooltip. Not needed: channels, clocks, SMT-LIB2, ontology syntax, PT vs DT, ticks,
artefact pins. A domain expert still reaches every formal object: in Generated (read), in the
inspector's advanced sections (timers, constants, assumptions, reference mapping), and in the
Library.
