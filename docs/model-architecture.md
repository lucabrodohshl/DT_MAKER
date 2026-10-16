# The canonical twin model

The model editor saves one `twin-model/1` document for a design. The document describes a system as asset types, parts, properties, events, actions, relationships, links, states and timed transitions. The server compiles its formal outputs; the browser edits source elements and displays the results. The typed representation and strict JSON decoder are in [`include/twin/model/document.hpp`](https://github.com/lucabrodohshl/DT_MAKER/blob/main/include/twin/model/document.hpp).

![The canonical twin model flows through validation and compilation into formal and operational outputs](/model-flow.svg)

## What the source represents

| Element | Purpose |
|---|---|
| Asset type and part | A reusable type and a named occurrence inside the root system |
| Property | A measured, fixed or per-twin value, with a type and optional unit or range |
| Event and action | An observed occurrence or a requested operation |
| Relationship and link | An association between assets or an event-to-action connection |
| State and transition | A behavioral mode and a possible change, with timing rules |

Every element has an identity used for diagnostics and generated trace links. The model's `root` names the system type. A published design version is immutable; edits belong in a new draft.

## Compilation and authority

`twin::model::compile` is a pure function of semantic model content and the compiler version. Its `Artifacts` result includes the timed automaton, `.ont` ontology, `.interp` interpretation, Turtle and JSON-LD projections, derived design sections, and a trace back to source elements. Invalid input produces located `TMLnnn` diagnostics instead of artifacts. See [`include/twin/model/compile.hpp`](https://github.com/lucabrodohshl/DT_MAKER/blob/main/include/twin/model/compile.hpp) and [`src/model`](https://github.com/lucabrodohshl/DT_MAKER/tree/main/src/model) for the implementation.

The generated **formal ontology** carries sorts, symbols and axioms used for semantic checks. The **RDF schema** describes a vocabulary; the **knowledge graph** contains concrete assets and facts. They are separate outputs. The timed automaton is the behavioral input to the existing compiler and verified kernel. The trace records which source elements produced each generated element, so the editor can navigate in both directions.

The model compiler targets the project's [supported timed-automaton fragment](supported-model-fragment.md). A model request outside that fragment is refused, not silently approximated. Behavioral state and admissible transitions remain decisions of the kernel at runtime. A model edit does not itself deploy anything; [release checks](blueprint-release.md) must pass for the exact saved version.

## Boundaries and extension points

The model library has no network, database, clock or UI dependency. Studio stores versions, calls the compiler, records diagnostics and coordinates release evidence. The frontend keeps editor state and renders the returned artifacts. Additional domain views belong in [plugins](plugins.md) and cannot decide kernel state.

For an existing formal automaton, the CLI and model library include a lift path; review its diagnostics before treating the lifted document as an editable source. The [developer guide](developer.md) covers the surrounding service, API, tests and build commands.
