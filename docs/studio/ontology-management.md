# Formal ontology and interpretation

The formal ontology is a theory in the project's .ont language. It can define **sorts, functions, relations and axioms**. An interpretation maps a behavior location or event label to a formula over that theory. Together they let Studio check meaning and alignment.

The **knowledge graph** holds concrete asset facts and relationships. The **RDF schema** describes that graph's vocabulary. Neither is the full formal ontology. In the new editor, look under **Generated → Formal ontology** for the compiled theory; the neighboring RDF schema and Knowledge graph tabs show their respective projections.

## Edit the ontology while designing a twin

1. Open a draft in **Twin designs**.
2. Select **Twin design** at the top of the left explorer.
3. In the right inspector, open **Formal ontology (.ont)** and enter extra declarations, one line at a time. For example, add a sort, a relation or an axiom in the project's ontology language.
4. To constrain one asset type, select it and add a **Formal assumption** with an axiom name and formula.
5. Save or wait for autosave, then open **Generated → Formal ontology**. Your declarations appear alongside the types, property symbols and axioms compiled from the graphical design.
6. Open **Problems** if the compiler reports a bad declaration or formula.

Do not edit the generated preview: it is rebuilt from the twin design. State, event and action meanings in the inspector are likewise compiled into interpretations. To change what HEATING means, select that state in Behavior and edit **Means**, then inspect the interpretation in **Advanced → Semantics**.

## The versioned ontology library

Open **Studio → Ontologies** for standalone formal artefacts used in advanced engineering or imported designs. Each row identifies an artefact; it can have a published version and an open draft. Open a row and its version to inspect source, diagnostics, symbol structure, references and validation. A published version is immutable. Create a new draft from it to make changes, then save, validate and run the required checks before release.

The left checkboxes on Ontologies, Interpretations and Timed automata support batch actions. **Export** opens a save dialog for the latest source or a JSON bundle with every version. **Delete** asks for confirmation and removes entries from the library while retaining their version bytes and historical references.

## Keep interpretations in sync

An interpretation refers to an exact ontology version. If a symbol changes, update the interpretation and rerun validation and alignment. The version page shows source diagnostics and references. [Evolve an ontology](tutorial-evolving-an-ontology.md) walks through this process on a published twin.

See [Refinement checking](refinement.md) and [Impact and staleness](impact-and-staleness.md) for the advanced evidence rules.
