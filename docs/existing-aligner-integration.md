# Integration with the existing semantic aligner (`SemPTDTAlignmentICSE/`)

This document records what the existing aligner is, how this project integrates
it **without modifying a single file of it**, and the behaviours found while
integrating it that matter for the correctness argument.
All line references are to `SemPTDTAlignmentICSE/` as shipped.

## 1. What the aligner is

| Aspect | Finding |
|---|---|
| Language / build | C++17 library `dtpta` + `semalign_lib` (Algorithm 1), CMake; upstream build hard-wired to `x86_64-linux` paths (`CMakeLists.txt`, `setup.sh`). |
| Dependencies | UPPAAL **UTAP** (XML/TA parser), UPPAAL **UDBM** (zones/DBMs), **Z3** (SMT), nlohmann/json; optional TBB/OpenMP. |
| Model input | UPPAAL XML timed automata, one view per file (`assets/*/V1_PT.xml`, `V2_DT*.xml`). |
| TA representation | `dtpta::TimedAutomaton` (`include/dtpta/timedautomaton.h`): `Location{id,name,invariants}`, `Transition{from,to,action,guards,resets,channel,is_sender}`; constraints are UDBM `constraint_t` (DBM entries `x_i - x_j ≤/< c`); clock map name→index (1-based, 0 = reference clock). |
| States / clocks / events | Locations indexed in document order; clocks from global then template declarations; an edge's action is its channel name (`a!`/`a?`), unsynchronised edges are `tau`. |
| Propositions / labelling | Implicit: the observable "label" of a state is its **location**; meaning is given by the interpretation. |
| Interpretations `I_P`, `I_D` | `.interp` text files: `Location : smt2-formula`, `label! : smt2-formula` (`src/semalign/domain_parser.cpp`), parsed into `dtpta::InterpretationMap` of Z3 expressions. |
| Ontology / domain theory | `.ont` text files (`sort`, `fun`, `rel`, `axiom`) parsed into `dtpta::Ontology` = Z3 context + solver loaded with the axioms Δ (`src/semalign/ontology.cpp`). JSON variant via `OntologyParser`. |
| Z3 usage | `Ontology::entails(φ)` checks `Δ ∧ ¬φ` UNSAT; `entails_iff` builds `(φ→ψ)∧(ψ→φ)`. |
| Alignment check | `SemanticAlignmentChecker::check_semantic_alignment(PT, DT, Φ, result)`: (1) label equivalence `E` by SMT, (2) BFS from the initial zone pair, (3) per pair: Conditions II/III over **weak observable successors in the zone graphs** (`src/semalign/semantic_checker.cpp`). Also `check_weak_timed_bisimulation` (syntactic labels, the baseline). |
| Strong vs weak | Only the (strong) semantic alignment of Definition 5 is implemented as a verdict; weak alignment (Definition 6) is not offered as a separate check. |
| Artefacts emitted | stdout reports and CSV rows (`results/cs_results/*.csv`); no machine-readable evidence document. |
| Examples | 8 case studies (`CS1`–`CS8`) + the paper's running example (`UseCase_Drone`, a crop-spraying drone), each with PT/DT XML, ontology v1/v2, interpretations and fault variants. |
| Tests | No unit tests; validation is the benchmark drivers (`benchmark/run_use_case.cpp` exits non-zero if the paper's checks fail). |

## 2. How this project integrates it (no aligner file is modified)

* `cmake/Aligner.cmake` compiles the aligner's **own, unmodified translation
  units** into the targets `aligner::dtpta` and `aligner::semalign`, against
  UDBM/UTAP built for the host by `scripts/bootstrap-deps.sh` (pinned
  revisions; UTAP pinned to `v2.1.1-rc`, the last revision exposing the
  `UTAP::Constants` namespace that the aligner's sources use).
* The aligner's own drivers are built too (`build/<preset>/aligner-bin/`).
  **Validation of the integration:** `run_use_case` reproduces all of the
  paper's Section VII results on macOS/arm64 (exit code 0, "ALL EXPECTED
  RESULTS REPRODUCED").
* The **compiler front-end is the aligner's parser stack** (UTAP), and every
  compilation is cross-checked against the aligner's `dtpta::TimedAutomaton`
  of the same file (translation validation, `src/compiler/crosscheck.cpp`).
  All 36 models of the aligner's corpus compile with translation validation
  passing (in `--legacy-system-declaration` mode, see §3.1).
* `twin align` runs `SemanticAlignmentChecker` (Z3) and records a
  machine-readable, hash-bound **alignment evidence** document that is shipped
  inside the Verified Twin Package.

## 3. Findings that matter for the correctness argument

None of these findings is fixed in the aligner (it is treated as an external,
unmodified artefact). The compiler and `twin align` either **reject** the
affected constructs or **report** them, so that the IR is always exactly the
model the aligner analysed.

### 3.1 Parsing and well-formedness

1. **UTAP type errors are ignored.** `TimedAutomaton(const std::string&)`
   checks only the return code of `parse_XML_file` (`src/timedautomaton.cpp:138`);
   UTAP reports type/semantic errors in `Document::get_errors()` while still
   returning 0. *Compiler:* every UTAP error is fatal (`TWC001`).
2. **All shipped models have an invalid `<system>` declaration** (e.g.
   `process DT = EnergyBudgetDT();` in `UseCase_Drone/V2_DT.xml`, or
   `CraneDT = CraneDT();` – instance named like its template – in CS2–CS8).
   UPPAAL rejects these documents; the aligner never notices because it only
   uses the first template (`src/timedautomaton.cpp:157`). *Compiler:* fatal by
   default; `--legacy-system-declaration` downgrades errors located only in
   `/nta/system` to recorded warnings (`TWC015`).
3. **Only the first template is analysed** (`templates.front()`, line 157).
   *Compiler:* exactly one template is required (`TWC003`).
4. **The initial location is assumed to be location #0**, not the `<init>`
   location (`TA_CONFIG.default_initial_location`, lines 1810, 1975, 1984).
   *Compiler:* requires `<init>` to be the first declared location (`TWC034`),
   so the verified and the executed initial configurations coincide.

### 3.2 Silent weakenings/strengthenings of constraints

5. **Disjunction is read as conjunction.** The guard and invariant extractors
   recurse into `AND` *and* `OR` identically (lines 321, 793, 965, 1118):
   `x < 2 || x > 5` becomes `x < 2 && x > 5`. *Compiler:* `||` rejected (`TWC024`).
6. **Unparseable guards are dropped** ("If we can't parse it, assume it's
   satisfied", line 655). This affects diagonal guards (`x - y < 3`), guards
   whose bound uses unary minus or division, negations, and `false`.
   *Compiler:* all rejected with the reason (`TWC020`, `TWC021`, `TWC025`, `TWC026`).
7. **Non-zero clock resets are ignored** (line 525). *Compiler:* `TWC052`.
8. **Data variables have no semantics**: guards on them are skipped (line 614)
   and updates mutate a parse-time context (line 529), i.e. they are not part
   of the analysed behaviour. *Compiler:* data variables rejected (`TWC014`,
   `TWC051`); the IR has no variables (see `docs/supported-model-fragment.md`).
9. **Edges without both endpoints are skipped** (line 408). *Compiler:* `TWC040`.

### 3.3 Semantics used by the alignment check

10. **Initial zone is the unconstrained zone, not the zero valuation.**
    `dbm_init` (UDBM) yields "all clocks ≥ 0" (`dbm/dbm.h`), although the
    comment at line 1807 says "zero zone". The zone graph therefore starts from
    every valuation of location #0 satisfying its invariant. The standard
    initial state `(l0, 0)` executed by the kernel is contained in that zone.
11. **LU-extrapolation** (`dbm_diagonalExtrapolateLUBounds`, line 1516) is
    applied in every time elapse. It is exact for reachability; the checker
    relies on it also for its bisimulation-style comparison.
12. **Condition I (state consistency) is not evaluated.** The state
    interpretations are parsed, but `formula_for_state` (semantic_checker.cpp:116)
    is never called; the BFS seeds only the initial pair. Condition IV (delay
    consistency) is not a separate check: delays are folded into zone-graph
    successors.
13. **Uninterpreted synchronised labels are skipped, not treated as τ.** τ-closures
    use only *unsynchronised* edges (`is_tau`, core.cpp:62), while Conditions
    II/III ignore labels outside `dom(I)`. A synchronised edge whose label has
    no interpretation (or a tautological one, semantic_checker.cpp:145) is
    neither matched nor traversed, so states reachable only through it are
    never compared. *`twin align`* reports such labels as lint errors; the
    project's models interpret every synchronised label and use unsynchronised
    edges for internal moves.

### 3.4 Observed on the paper's running example (`assets/UseCase_Drone`)

`twin align` on the shipped crop-spraying drone reproduces the aligner's verdict
**ALIGNED**, and its lint shows how much of the model that verdict covers:

* `spray!` (PT) and `apply!` (DT) have **tautological** interpretations under
  `domain.ont`: `I_PT(spray!) = (<= (consumption 1) 50)` is the axiom `cons_upper`
  itself. The aligner removes both labels from `dom(I)` (it also prints a warning)
  and then skips their transitions (finding 13).
* `return_base!`, `recharge_complete!`, `fault_detected!` and `fault_return!` (PT) are
  **uninterpreted**, so they too are skipped.
* Consequently the final alignment relation contains **3 state pairs** (`relation
  size: 3`). The BFS stops at the zone-entry pair, and spraying, return,
  recharging and the fault branch are never compared.

The verdict is therefore *vacuous for most of the model*. This does not
contradict the paper's Section VII entailment checks (Δ ⊨ I_PT(spray!) ↔
I_DT(apply!) does hold), but it means that the behavioural part of the alignment
was not exercised. The project's own models (`models/indoor_drone/`) are written
to be lint-clean: every synchronised label is interpreted, no interpretation is a
tautology, and internal moves are unsynchronised edges.

### 3.5 Consequence for this project

The correctness theorem of this project (`proof/`) is about the chain
`V_D ≅ IR(V_D) ≅ K_sem ~ K_prod`. The relation `V_P ~Φ V_D` is an **assumption**
discharged by the aligner; findings 10–13 delimit what the aligner's verdict
means in practice and are repeated as threats to validity in the proof's
limitations section. They do not affect the `V_D ~ D` result.
