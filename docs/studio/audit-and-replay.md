# Audit, provenance and replay

Use **Running twins** to inspect an instance's event history and execution ledger. The runtime records each accepted or refused event and the resulting state against its exact package. Ledger verification checks the recorded chain. A failed check is an integrity problem; a disconnected runtime leaves the result unavailable.

**Replay** opens a historical execution with the design version, automaton, ontology and interpretation it used at that time. Later edits or publication do not change that history. The synchronized behavior and telemetry views help explain an old decision.

Studio's **Engineering audit** is separate: it records draft edits, validation, publication, package and deployment actions with actor and time. **Version history** and **Impact analysis** show what changed between design or artefact versions. Use these views when you need to answer who changed a definition, what was checked, and what a running twin actually executed.
