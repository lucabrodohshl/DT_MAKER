# Impact analysis and verification staleness

## Dependency analysis

Studio keeps an explicit dependency model:

```
ontology version
  ├─ PT interpretation ──┐
  ├─ DT interpretation ──┼─ alignment evidence (V_P ∼Φ V_D) ─┐
  │                      │                                    ├─ Twin IR (compiled DT view + DT interpretation)
PT view ─────────────────┘                                    ├─ verified package ── deployment ── active twin
DT view ─────────────────────────────────────────────────────┘
```

Each version page has a **Dependencies** tab listing the interpretations over the artefact and
the twins and deployments that use it. Each **Symbols** entry lists the axioms and interpretation
entries that use the symbol, and the telemetry channels that observe it.

## Impact analysis

**Maintenance → Impact analysis** (or **Analyze impact** on a version) answers: *if this
version replaced the deployed one, what would be affected, and what must be re-verified?*
Studio compares the version with what every twin currently deploys. It uses the structural
diff, the interpretation entries that mention changed symbols, and the stored evidence for
exactly the artefacts involved.

Every dependant gets a classification **and the reason, citing evidence**:

| Classification | Meaning |
|---|---|
| **Changed** | The version under analysis |
| **Unaffected** | Its inputs are byte-identical, or it provably does not depend on the change (for example, the IR when only the ontology changed) |
| **Preserved** | Proven unaffected by a stored check: refinement evidence for condition (c), or Theorem 3 for alignment. The evidence id is shown. |
| **Potentially affected** | No entry mentions a changed symbol, but meaning can still change through Δ. Condition (c) of the refinement check decides. |
| **Requires verification** | Entries use symbols whose declaration or axioms changed. Condition (c) must be established, or alignment re-run. |
| **Definitely stale** | Its evidence examined content that has certainly changed (for example, alignment after a model change) |

A **Required next steps** box at the top lists what must be re-run, for example *run the
refinement check including the deployed interpretations* or *re-run alignment for twin X*.
When nothing is required, the box says so. The **Graph** view draws the same result as a
dependency graph.

![Impact analysis](../screenshots/tutorial/07-impact.png)

## Verification staleness

Evidence is about **the exact bytes it examined**. Whether it still **applies** depends on what
a consumer (a package, a deployment, a change) uses *now*. Applicability is computed on every
request and never cached, so dependent evidence can never silently stay green:

| Applicability | Meaning |
|---|---|
| **VALID** | Every input the consumer binds has the same content hash |
| **STALE** | At least one bound input now has different content (*dependency changed*) |
| **INVALIDATED** | An input version was REJECTED, or its stored content no longer matches its hash |
| **UNKNOWN** | The evidence examines nothing this consumer uses |

**Why is this stale?** On an evidence page, and wherever a STALE badge appears, the
explanation is structured. It names, per role, the version and hash the evidence examined and
the version and hash in use now:

> ontology dependency changed: the evidence examined process-pump@1 (5c1e0a…) but
> process-pump@3 (64cc79…) is now used.

The trust panel of a twin uses the same computation. A twin whose deployment binds a version
that its alignment evidence did not examine shows **Alignment: Stale**, never Pass.

## Re-running semantic alignment

Alignment is decided by SemPTDTAlignmentICSE, through the compiler, for the exact PT view, DT
view, ontology and interpretations. You re-run it from the **change workspace pipeline**,
stage *Check / re-check alignment* (**Re-run**), which runs it for the candidate artefacts.

The pipeline decides which route applies:
- **Preserved by Theorem 3**: all premises hold (see [Refinement](refinement.md#alignment-preserved-by-refinement-theorem-3)).
  The stage passes and cites both the refinement evidence and the original alignment
  evidence.
- **Realignment**: otherwise. The aligner runs on the candidate artefacts, and a new alignment
  evidence record is stored. If it does not pass, the stage fails with the aligner's
  diagnostics and release stays blocked.
