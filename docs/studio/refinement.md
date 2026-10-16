# Check ontology refinement

Refinement is an advanced check for a changed formal ontology. Use it when an existing running twin or interpretation relies on the old theory and you need evidence about whether the new theory preserves its relevant meaning.

## Run the check

Open the candidate ontology draft under **Studio → Ontologies** or through a change workspace. Validate the .ont source first. Select the deployed or published version as the baseline and the new draft as the candidate, then run **Refinement**. Inspect the exact version references shown in the report.

## Read the verdict

A passing syntax check only means the source parses. The refinement report separately shows the logical obligations, solver verdict, any counterexample or missing premise, and the exact hashes checked. A failed obligation means the change did not meet this refinement relation. An unknown result needs investigation; it is not a pass.

If refinement passes and the required alignment premises hold, Studio can determine whether previous alignment evidence remains applicable. Otherwise rerun alignment on the candidate. Open [Impact and staleness](impact-and-staleness.md) to see the downstream checks before release.

For the full user workflow, see [Evolve an ontology](tutorial-evolving-an-ontology.md).
