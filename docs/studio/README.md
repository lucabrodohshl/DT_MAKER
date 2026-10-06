# Verified Twin Studio — product manual

Verified Twin Studio is the operations and engineering console of the platform. It works
for any twin: the drone and the industrial pump that ship as examples use the same generic
screens. Only the drone adds a domain view, and it does so through a plugin.

![Overview](../screenshots/01-overview.png)

## One rule above all

**The browser displays conclusions. It never makes them.**

Every behavioural state, proposition, admissible transition, prediction, conformance verdict,
ledger verdict, validation result, refinement verdict and impact classification shown in Studio
comes from an authoritative backend component:

| Conclusion | Decided by |
|---|---|
| Behavioural state, enabled transitions, predictions, what-if | the verified kernel (`twin-runtime`) |
| Meaning of telemetry (proposition truth) | interpretation evaluation with Z3 in `twin-studio` |
| Ontology / interpretation validity | the strict parser **and** SemPTDTAlignmentICSE's parser plus Z3 |
| Ontology refinement (Def. 4) | `twin::ontology::check_refinement` (Z3, three-valued) |
| Alignment preservation (Theorem 3) | `assess_preservation`, applied only when every premise holds |
| Semantic alignment | SemPTDTAlignmentICSE (via the compiler) |
| Package integrity | the package verifier |
| Ledger validity | the runtime's ledger verifier |

A trust badge is never green by default. The badge states are **Pass**, **Fail**, **Unknown**,
**Not checked**, **Stale**, **Invalidated**, **Check running**, **Unavailable** and
**Check failed**. Each state has its own icon and label, so the meaning never depends on colour
alone. A badge that has not been checked shows **Not checked**. Anything the client cannot
parse shows **Unknown**, never Pass.

## The two loops

```
OPERATIONS:   asset → telemetry → semantic meaning → verified behaviour → prediction → decision → evidence
ENGINEERING:  ontology/model/interpretation → edit → validate → refinement/alignment → impact
              → compile → verified package → deploy → operate → maintain → new version
```

## Contents

| Page | For |
|---|---|
| [Getting started](getting-started.md) | Starting the demo, the layout, personas, search, live data and time |
| [Operations](operations.md) | Assets, knowledge graph, telemetry and meaning, behaviour, conformance, events, prediction, what-if, planning |
| [Audit, provenance and replay](audit-and-replay.md) | Execution ledger, chain verification, decision provenance, replay, historical reproducibility |
| [Ontology management](ontology-management.md) | Formal ontology vs. asset knowledge graph, the editor, versioning and lifecycle, diff, interpretation editing |
| [Refinement checking](refinement.md) | Definition 4, running a check, understanding the four results, evidence, Theorem 3 |
| [Impact analysis and staleness](impact-and-staleness.md) | Dependency analysis, impact classes, evidence staleness, re-running alignment |
| [Releases, deployment and rollback](release-and-deployment.md) | Change workspace, release pipeline, packages, deployment, rollback, engineering audit, logs |
| [Tutorial: Safely evolving an ontology](tutorial-evolving-an-ontology.md) | The whole engineering loop on the pump, with real screenshots |
| [Domain plugins](plugins.md) | Writing a domain visualisation (developer guide) |
| [Developer guide](developer.md) | Architecture, API contract, building, testing, security notes |
| [Aligner findings](aligner-findings.md) | Behaviour of SemPTDTAlignmentICSE that Studio compensates for |

The runtime API is documented in [`../runtime-api.md`](../runtime-api.md). The Studio HTTP
API is specified in [`../../api/studio.openapi.yaml`](../../api/studio.openapi.yaml).
