# Ontology-layer findings about the aligner and its corpus

These findings come from building Studio's ontology services (`twin::ontology`)
on top of SemPTDTAlignmentICSE. They extend the compiler/alignment findings
in [`../existing-aligner-integration.md`](../existing-aligner-integration.md).
As with those, nothing in `SemPTDTAlignmentICSE/` is modified. Studio either
compensates for each behaviour or reports it.

| # | Finding | Consequence | How Studio handles it |
|---|---|---|---|
| S1 | `.ont`/`.interp` parsers silently skip unknown keywords and lines without `:` (`domain_parser.cpp`). `CS3_Rover/dt.interp` line 12 is such a line: a comment continuation that lacks its `;`. | An engineer may believe a line is part of the theory when the aligner ignored it. | The strict parser reports `ONT001`/`INT001` errors with their locations. Validation fails until the text is unambiguous. |
| S2 | Most shipped `domain_v2.ont` / `*_v2.interp` evolutions are **not** refinements under Def. 4. Examples: CS6 `(= dose_tolerance 5)` → `3`; Drone `bat_cap` 500 → 450; CS7/CS8 change constants; CS2/CS4/Drone give previously internal (τ) events an interpretation. Only CS1 and CS5 are valid Φ-refinements. The corpus comments cite Theorem 3, but the paper's benchmark re-runs alignment under v2 (RUN 4). | Theorem 3 cannot be used to carry alignment over for those pairs. | The refinement check reports NOT A REFINEMENT, listing the failing obligations and a counter-model. Impact analysis then requires **realignment**, as the formal theory demands. |
| S3 | `Ontology::entails` returns `false` when Z3 answers `unknown`. | An undecided query would look like a failed entailment. | Studio runs its own tri-state queries over the same context and axioms. `unknown` becomes UNKNOWN / INCONCLUSIVE and is never reported as a violation. |
| S4 | Every user sort is mapped to `Real`. | Z3 cannot detect a sort change of a shared symbol (Def. 4 (a)). | Condition (a) is checked on the declared sort names, which is stricter than the aligner's reading. This is stated as an assumption in every refinement report. |
| S5 | An inconsistent Δ entails everything. | Refinement and alignment checks would hold vacuously. | Validation reports `ONT110`. A refinement check with an inconsistent candidate is CHECK FAILED, not VALID. |
