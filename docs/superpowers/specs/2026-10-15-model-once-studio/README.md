# Model once, derive the rest — Studio redesign

Status: design written 2026-10-15; implementation started on branch `studio-model-once` at the
user's request ("forget the approval, just go implement"). This folder is the reference the
implementation follows. Where the code and this text disagree, the text is updated in the same
change.

## The idea in one paragraph

Today a twin is built by writing the same facts six times: an asset in Structure, a signal in the
data contract, a symbol in the ontology, a location in two timed automata, an entry in two
interpretation files, a state label in Presentation, a monitor in Assurance. Studio then needs a
*Cross-layer binding* page to find the places where those copies disagree (81 gaps in the drone
example). The redesign replaces the copies with **one canonical model** written in a small visual
language — asset types, assets, properties, relationships, events, actions, states and
transitions with time limits — and **derives** every formal representation from it with explicit,
deterministic, traceable rules: the OWL/SHACL schema and the RDF knowledge graph, the formal
domain theory K and the interpretations, the timed automaton V_D, its UPPAAL XML, the data
contract, the monitors and the presentation. The reverse direction exists too: an existing timed
automaton (with its ontology and interpretations) is **lifted** into the language, and
`compile(lift(TA))` equals the TA up to naming, ordering and layout.

## Documents

| # | Document | Answers |
|---|---|---|
| 1 | [Current-state audit](01-current-state-audit.md) | What exists, what is duplicated, where the complexity comes from |
| 2 | [Reference analysis](02-reference-analysis.md) | Node-RED, its Digital Twin nodes, AWS IoT TwinMaker, UPPAAL, DTDL — what to adopt, what to avoid |
| 3 | [Canonical metamodel](03-metamodel.md) | The language: concepts, types, rules, behavioural and timing semantics, lifting |
| 4 | [Compilation architecture](04-compilation.md) | Transformation rules to KG/OWL/SHACL, K and interpretations, TA and XML; validation; trace |
| 5 | [Information architecture](05-information-architecture.md) | Navigation map, screens, where every existing feature goes |
| 6 | [Studio UX specification](06-studio-ux.md) | Wireframes, visual grammar, canvas, selection, inspector, views, feedback |
| 7 | [User journeys](07-user-journeys.md) | Twelve workflows, current vs proposed, interaction counts |
| 8 | [Design system](08-design-system.md) | The aesthetic, tokens, components, conventions |
| 9 | [Migration and implementation](09-migration-and-implementation.md) | Mapping to code, compatibility, phases |
| 10 | [Worked example](10-worked-example.md) | A pump station from empty canvas to ontology, KG and timed automaton |

The machine-readable schema of the canonical model is [`twin-model-1.schema.json`](twin-model-1.schema.json).

## Decisions taken

1. **One canonical document per twin design**: `twin-model/1`, stored as the `model` section of
   the existing Blueprint version. Everything formal is derived from it and pinned by the server;
   derived sections are read-only.
2. **Eight first-class concepts**: Asset type, Asset, Property, Relationship, Event, Action,
   State, Transition. Timing is an attribute of states and transitions, not a concept. Guards on
   measured values are not part of the language because the verified fragment has no data
   variables; data conditions live in *condition events* and *state meanings*, which compile to
   interpretations.
3. **Composition by parts**: an asset type may contain assets (parts). The model itself is the
   root type, so the structure canvas always edits "the parts of a type".
4. **Behaviour compiles to the existing supported fragment** (one template, clocks, plain
   channels, conjunctive clock constraints, resets to zero). Several machines are combined by a
   synchronous product with label synchronisation; links between an event and an action are
   synchronisation. No priorities, no data guards, no broadcast: refused with an explanation.
5. **Determinism by construction**: overlapping transitions on the same trigger from the same
   state are an error unless the model explicitly allows nondeterminism; compilation is
   byte-reproducible.
6. **Two semantic targets** from the same elements: the descriptive Semantic Web layer (OWL 2 +
   SHACL schema, RDF knowledge graph; Turtle and JSON-LD) for interoperability and queries, and the
   formal domain theory K with interpretations (SMT-LIB2) for verification. OWL cannot express the
   linear arithmetic the aligner decides; SMT-LIB2 is not an interchange format.
7. **Alignment stays, and stays honest**: the physical reference V_P is never derived from the
   model (alignment against yourself proves nothing). It is imported and mapped to model states
   and triggers; I_P is generated from that mapping.
8. **Lifting** (TA → model) is part of the language definition, with a round-trip guarantee
   checked by tests on every example and on the aligner corpus.
9. **Information architecture**: three areas — **Model** (Studio), **Twins** (Operate),
   **Connections** — instead of two modes with 68 pages.
10. **Aesthetic**: an engineering-instrument look — ink on paper, hairline rules instead of cards,
    one accent, typographic status instead of pastel pills, IBM Plex Sans/Mono. The graph
    rendering the user likes (white nodes, thin borders, mono label chips, dagre layout) is kept
    and formalised.
