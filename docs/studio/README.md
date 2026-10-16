# Doppelganger

Doppelganger has two connected areas:

- **Twin designs** is where you author reusable definitions. In the new editor, Structure and Behavior are the inputs. The server compiles them into a timed automaton, a formal ontology, interpretations, a knowledge graph and a contract. A design is saved as a draft, then verified and published.
- **Running twins** is where you use a published design as a concrete twin, connect its runtime and data, and watch behavior, telemetry, conformance, prediction and history.

Start with [Create your first twin](tutorial-first-twin.md). It walks through the buttons and fields, including where the timed automaton, ontology and interpretation come from. [Getting started](getting-started.md) is a shorter map of the app.

## The words used in Studio

| Term | What it means |
|---|---|
| Twin design | A reusable, versioned definition you edit and publish |
| Running twin | A concrete instance of a published design |
| Formal ontology | The .ont theory with sorts, functions, relations and axioms |
| Interpretation | Formulas that give states and events meaning over that ontology |
| Knowledge graph | Assets and facts, with an RDF schema; narrower than the formal ontology |
| Timed automaton | The behavior compiled from states, events, transitions and time limits |

The older API and file format still use “blueprint” in places. The app calls the concept a twin design.

## Follow a task

- [Create and edit a twin design](tutorial-first-twin.md)
- [Understand the design editor](blueprint-workspace.md)
- [Edit a formal ontology](ontology-management.md)
- [Evolve a published ontology](tutorial-evolving-an-ontology.md)
- [Release a design and create a running twin](blueprint-release.md)
- [Connect a running twin to a real system](tutorial-connect-real-twin.md)
- [Run scenarios and preview behavior](scenarios-and-preview.md)
- [Read operational views](operations.md)
- [Review evidence, impact and history](impact-and-staleness.md)

## Who computes the result

The browser edits the design and displays results. The server validates and compiles it. Runtime state, admissible transitions, conformance and prediction come from the verified kernel. Ontology and interpretation checks use the formal parsers and solver. Release readiness depends on evidence for the exact saved version. If the design has problems, open **Problems** in the editor and follow the diagnostics back to an element.

For API and implementation details, see the [developer guide](developer.md), [Studio API](https://github.com/lucabrodohshl/DT_MAKER/blob/main/api/studio.openapi.yaml) and [runtime API](runtime-api.md).
