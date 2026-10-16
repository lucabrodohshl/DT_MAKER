# The twin design editor

Open a card in **Twin designs**. A design with a canonical model opens the new graphical editor. The header shows version, save state, Problems, undo and redo, Validate, Advanced and Publish.

## Structure

The left explorer contains the twin design, system and reusable asset types. Select **Twin design** to edit its name and extra formal ontology declarations. Select an asset type to edit its properties, events, actions and assumptions. Double-click blank canvas space to add an asset of an existing type or create a new type. Drag between asset ports to add a typed relationship. Double-click a type in the explorer or on the canvas to work inside it.

Properties distinguish measured, fixed and per-twin values. Measured values can carry units, range and sample period. Fixed values become constraints in the compiled theory. Relationship vocabulary defines the allowed links. The inspector edits the selected element and shows diagnostics about it.

## Behavior

Choose a type's machine in the Behavior tab. Double-click to add a state; the first is initial. Drag from one state to another to create a transition, then choose its event, action, timeout or internal trigger. Select a state to set its **Means** formula and **At most** time limit. Select a transition to edit its trigger and time window.

The editor saves the source design. The server compiles the behavior directly into the Digital Twin timed automaton. You can inspect the exact automaton in Generated. The advanced area can also hold a separate Physical System view for alignment.

## Formal ontology and interpretations

The **Formal ontology (.ont)** inspector field on the twin design accepts additional sorts, functions, relations and axioms, one declaration per line. These lines stay in the design and are included on each compilation. The **Formal assumptions** section on an asset type adds axioms for that type. Property definitions also produce symbols and constraints.

State and event **Means** fields produce the Digital Twin interpretation, mapping behavior labels to formulas over the ontology. The advanced Semantics area shows the separate versioned artefacts and coverage. The **Knowledge graph** and **RDF schema** Generated tabs show structural facts and vocabulary; they are narrower projections, not substitutes for the formal ontology.

## Generated

Generated has **Knowledge graph**, **RDF schema**, **Formal ontology**, **Automaton** and **Contract** tabs. It is read only because the source of truth is the design. Use **Source**, **Copy** or **Download** to inspect outputs. Selecting a source element shows what it generated; selecting an automaton element traces back to its source. Generated results appear only after the current edit has saved and compiled.

## Problems, saving and history

Autosave waits briefly after an edit. **Save** forces it. If another editor changed the same revision, Studio reports a conflict instead of silently overwriting. **Undo** and **Redo** cover Structure, Behavior and inspector edits. **Problems** lists server diagnostics; click one to select the element. **Validate** saves and opens the checks.

## Advanced work

**Advanced** opens the section editor for imported formal views, world and data configuration, semantics, assurance and release. **Publish** opens release readiness. See [Release and deployment](blueprint-release.md) for the steps after design.
