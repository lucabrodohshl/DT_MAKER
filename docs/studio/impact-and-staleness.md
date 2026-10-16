# Impact analysis and stale evidence

A twin design draft and each formal artefact version have exact content hashes. Validation, alignment, compilation and package evidence refer to those bytes. Changing a property, behavior edge, ontology axiom or interpretation formula can make earlier evidence stale.

## Review a change

Open the design's **Advanced → Impact analysis**, or select a version under **Studio → Ontologies**, **Interpretations** or **Timed automata**. Compare it with its parent or a deployed version. The report identifies changed inputs, dependants and the checks that must run again.

A renamed ontology symbol may affect interpretation formulas. A changed event or transition may affect alignment, compilation, monitors and scenarios. A visual position change does not alter the formal theory. Follow the report's fix links rather than assuming a previous green badge still applies.

## Re-run required checks

Save the draft, inspect Problems, validate the formal inputs, rerun alignment where needed, then rebuild the package in **Release → Package**. Publish only when the release gate passes for the current version. The prior published version remains available for existing running twins and historical replay.
