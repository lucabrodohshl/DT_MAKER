# Trusted computing base

This document answers one question: what has to be correct for the guarantee
`V_D ≅ IR(V_D) ≅ K_sem(IR(V_D)) ~weak K_prod(IR(V_D))` to hold for a running twin, and what may be
wrong without breaking it? The proof in `proof/` states the guarantee precisely. Here we map it to
code.

## In the TCB

| Component | Code | Why it must be correct | How it is checked |
|---|---|---|---|
| Semantic kernel | `src/kernel`, `include/twin/kernel` | It *is* K_sem: initial state, delay, discrete steps, enabling windows, monitoring over state sets. | Proof, sections 5–6; unit and property tests (enabling windows and deadlines against brute force on thousands of random automata); purity by construction (const inputs, value results) |
| IR model and validator | `src/ir/model.cpp`, `src/ir/validate.cpp` | The kernel trusts a validated IR (well-formed indices, canonical constraints). | Unit tests; the kernel refuses unvalidated models |
| Session commit protocol | `src/runtime/session.cpp` | Single mutation path: write-ahead, then commit of kernel-computed values only; fail-stop. | Proof, section 7; session tests including injected ledger faults; architecture test "only the session assigns the state" |
| Ledger encoding and hashing | `src/ledger/record.cpp`, `src/ledger/writer.cpp`, `src/json/canonical.cpp`, `src/core/sha256.cpp` | Tamper-evidence and replay rely on canonical bytes and SHA-256. | NIST test vectors; canonical round-trip; tamper drills; replay identity in e2e |
| Package verification | `src/package/package.cpp` | The runtime executes only IR whose hashes and bindings verify against the alignment evidence. | Around 50 checks per package; tamper tests |
| Logical time arithmetic | `src/core/logical_time.cpp` | Exact integer ticks, checked overflow, grid parsing. | Unit tests |

## Trusted, but outside this repository

| Component | Assumption |
|---|---|
| SemPTDTAlignmentICSE (aligner, Z3, UDBM, UTAP) | Its verdict `V_P ~Φ V_D` is correct for the documents it analyses. This repository uses it unmodified (source digest recorded in every package) and documents its known behaviours in [`existing-aligner-integration.md`](existing-aligner-integration.md). |
| C++ compiler, standard library, OS file system (`fsync`) | Standard assumptions. A ledger write is durable once `fsync` returns. |

## Outside the TCB (may be wrong without breaking the guarantee)

| Component | Why it is safe for it to be wrong |
|---|---|
| **Compiler** (`src/compiler`) | Every compilation is *translation-validated*: the IR is compared with the aligner's own reading of the same document (locations, edges, labels, resets; guards and invariants compared as DBMs with `dbm_areEqual`). A compiler bug can make compilation fail; it cannot silently produce an IR that differs from the verified model. |
| PT adapter (`pt_adapter.cpp`) | It produces *candidate* observations. A wrong translation is rejected by the kernel and recorded as non-conformance. |
| Mission controller and planner | They propose. Decisions are submitted to the kernel, which refuses anything the model does not admit. A bad planner can delay a mission or fail it safely. |
| Plan validator | It is a *second*, independent geometric check. It is not needed for behavioural safety. |
| Co-simulation driver and API server | They read copies of the state and submit inputs through the session. They hold no write path to semantic state (checked). |
| `twin-world`, `twin-pt-feed` | They are the physical side. The twin's guarantee is about its own behaviour, not the world's. |
| Studio (platform, ontology services, UI) | It displays and organises evidence. Formal verdicts come from the aligner and the ontology checker (Z3), and behavioural state comes from the runtime. The UI derives nothing. |

## Failure behaviour

- **Semantic refusals** (the model does not admit an input) are *answers*. They are recorded as
  `reject` records, and the state is unchanged.
- **Infrastructure failures** (the ledger cannot be written) are **fail-stop**. The session
  refuses all further inputs. The twin may stop, but it never runs ahead of its ledger.
- **A missed deadline** (time passes beyond a location invariant without the required event) is
  recorded as an `alarm` and makes the twin non-conformant.

## Explicitly not claimed

- Physical hard-real-time equivalence. All guarantees are stated in logical time; see
  [`logical-time-model.md`](logical-time-model.md).
- Immutability of the ledger file. The ledger is *tamper-evident*: changes are detectable, not
  impossible. Immutability would need WORM storage or external anchoring (`twin ledger anchor`).
- Mechanised proof. The proof in `proof/` is a handwritten mathematical argument. The tests
  validate the implementation against it; they do not prove it.
