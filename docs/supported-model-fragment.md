# Supported model fragment

The compiler accepts exactly the fragment of UPPAAL timed automata to which **the
semantic aligner and UPPAAL give the same meaning**. Inside this fragment, the model that
SemPTDTAlignmentICSE verifies, the IR, and the kernel's execution are the same transition system
(proof, sections 2–6). Outside it, the compiler stops with a diagnostic
(see [`compiler-diagnostics.md`](compiler-diagnostics.md)) rather than compiling something the
aligner reads differently.

## Accepted

- **One template, instantiated once**: `P = T(); system P;`. Legacy corpora with invalid
  `<system>` blocks compile with `--legacy-system-declaration`, mirroring the aligner.
- **Clocks**: any number, global or local.
- **Channels**: plain (not urgent, not broadcast). Edges carry `a!`, `a?`, or no synchronisation
  (an internal step, τ).
- **Integer constants** with exactly evaluable initialisers. They are used as bounds and scaled
  exactly to ticks.
- **Invariants**: conjunctions of `x ⋈ c` and `x − y ⋈ c`.
- **Guards**: conjunctions of `x ⋈ c`, where ⋈ is one of `<`, `≤`, `=`, `≥`, `>`, and `true`.
- **Updates**: clock resets `x := 0`.
- **Initial location**: the first declared location.
- **Interpretations** (`.interp`): `Location : φ` and `label! : φ`, in the aligner's SMT-LIB2
  syntax. They are carried into the IR as metadata (propositions `at(L)` with I_D(L), and
  I_D(label)). They are never evaluated by the kernel.

## Rejected (with the reason the aligner would diverge)

| Construct | Why it is refused |
|---|---|
| Data variables, functions, arrays, parameters | The aligner gives them no semantics (it ignores data guards and updates). |
| Urgent or committed locations, urgent or broadcast channels | The aligner ignores urgency and channel kinds. |
| Disjunction, negation and `false` in constraints | The aligner reads `\|\|` as `&&`, ignores `!`, and reads an unparseable guard as `true`. |
| Diagonal constraints in guards | The aligner's guard parser drops them (invariants are fine). |
| Non-zero clock resets, other updates | The aligner ignores them. |
| Select, probabilistic, priced or stochastic features | Not part of timed automata as aligned. |
| Several templates | The aligner analyses only the first template of a document. |

## Semantics recap

A configuration is (location, clock valuation, time). Delays let all clocks advance equally while
the invariant holds. A discrete step requires the guard, applies the resets, and requires the
target invariant. Time is discretised on the grid T_R
([`logical-time-model.md`](logical-time-model.md)). Monitoring over sets of configurations
preserves nondeterminism: an observation is explained by every enabled transition with that
label. Compilation reports *event determinism* (no two same-label transitions enabled together)
in the compilation manifest.

## Examples in this repository

| Model | Fragment features |
|---|---|
| `models/indoor_drone/V_D_mission_supervisor.xml` | 12 locations; invariants on 2 clocks (`t_mode`, `t_flight`); timed guards; 16 synchronised labels; event-deterministic |
| `models/indoor_drone/V_P_flight_controller.xml` | Flight-controller view, including internal τ steps (spin-up) |
| `examples/industrial-pump/models/*.xml` | Pump control system and reliability twin; 10 labels each |
| `SemPTDTAlignmentICSE/assets/*/*.xml` | All 36 corpus views compile (`--legacy-system-declaration`) and pass translation validation (`tests/integration`) |
