# Compiler diagnostics

`twin compile` (and every package build) reports problems as located, machine-readable
diagnostics:

```
error[TWC024] template T, edge #3 (A -> B), guard: disjunction 'x < 2 || x > 4' is not supported
  (hint: the aligner reads '||' as '&&', which would strengthen the constraint; split the edge into one edge per disjunct)
```

Any error aborts compilation. A recurring theme: **wherever the semantic aligner would
*silently* give a construct a different meaning than UPPAAL, the compiler refuses it.** If it
did not, the IR could execute a model other than the one that was verified. See
[`existing-aligner-integration.md`](existing-aligner-integration.md) for the aligner behaviours
behind each rule, and [`supported-model-fragment.md`](supported-model-fragment.md) for the
accepted fragment.

Severity: **E** = error, **W** = warning.

## Document and options

| Code | Sev. | Meaning | Fix |
|---|---|---|---|
| TWC000 | E | The source cannot be opened or UTAP cannot parse it. | Check the path and the XML. |
| TWC001 | E | UPPAAL syntax or type error. The aligner would ignore it. | Fix the expression. For an invalid `<system>` block in a legacy corpus, see TWC015. |
| TWC002 | W | UPPAAL warning. | Review it. |
| TWC003 | E | The document must contain exactly one template. The aligner analyses only the first one. | Split the views into separate documents. |
| TWC004 | E | The template is not a timed automaton. | Use a TA template. |
| TWC005 | E | The system must instantiate the single template exactly once. | `P = T(); system P;` |
| TWC006 | E | Channel or process priorities. | Remove them. |
| TWC015 | W | Invalid `<system>` declaration ignored (`--legacy-system-declaration`, as the aligner does). | Only for legacy corpora; prefer a valid declaration. |
| TWC080 | E | No model id was given. | `--id <model-id>` |
| TWC081 | E | Invalid time base (R must be a power of ten). | `--ticks-per-unit 1000` |

## Template shape and declarations

| Code | Sev. | Meaning |
|---|---|---|
| TWC007 | E | Template parameters. The aligner treats them as uninitialised variables. Inline the values. |
| TWC008 | E | User-defined functions. The aligner does not evaluate function bodies. |
| TWC009 | E | Branchpoints. |
| TWC010 | E | The template has no locations. |
| TWC011 | E | Arrays. The aligner names array clocks and channels textually. |
| TWC012 | E | Urgent or broadcast channels. The aligner ignores channel kinds. |
| TWC013 | E | A constant initialiser that cannot be evaluated exactly. |
| TWC014 | E | Data variables. The aligner gives them no semantics, so the verified model would not constrain them. |

## Constraints (guards and invariants)

| Code | Sev. | Meaning |
|---|---|---|
| TWC020 | E | A diagonal constraint (`x − y ⋈ c`) in a guard. The aligner's guard parser drops it. Diagonals are accepted in invariants. |
| TWC021 | E | The bound is not an exact integer constant. |
| TWC022 | E | The constant is outside the range of UDBM bounds. The aligner would treat it as infinity. |
| TWC023 | E | Any other constraint form. |
| TWC024 | E | Disjunction. The aligner reads `\|\|` as `&&`. Split the edge. (UTAP 2.1 already rejects clock disjunctions as TWC001.) |
| TWC025 | E | Negation. The aligner ignores it. |
| TWC026 | E | A constant `false` constraint. The aligner reads an unparseable guard as `true`. |

## Locations and edges

| Code | Sev. | Meaning |
|---|---|---|
| TWC030 | E | Duplicate location name. |
| TWC031 | E | Urgent or committed locations. The aligner ignores urgency. |
| TWC032 | E | Rate expressions (stochastic or priced extensions). |
| TWC033 | E | No initial location. |
| TWC034 | E | The initial location must be the first declared location. The aligner always starts from the first. |
| TWC040 | E | An edge that does not connect two locations. |
| TWC041 | E | Select bindings. |
| TWC042 | E | Probabilistic edges. |
| TWC043 / TWC044 / TWC045 | E | An unsupported synchronisation, a synchronisation on an undeclared channel, or an unsupported synchronisation kind. |
| TWC050 | E | An update other than `x := 0`. |
| TWC051 | E | An update of a non-clock. |
| TWC052 | E | A clock reset to a value other than 0. The aligner ignores it. |

## Translation validation and IR emission

| Code | Sev. | Meaning |
|---|---|---|
| TWC060 | E | The compiler and the aligner read the model differently (locations, edges, labels, resets, or guards and invariants compared as DBMs). This always indicates a bug or an unsupported construct; compilation stops. |
| TWC090 | E | The emitted IR is not well-formed (a compiler bug). |
| TWC091 | E | The IR cannot be canonicalised. |

## Interpretations (`--interp`)

| Code | Sev. | Meaning |
|---|---|---|
| TWC070 | E | The interpretation file cannot be opened. |
| TWC071 | W | A line without `:` is ignored, as the aligner does. |
| TWC072 | E | Receive label `a?` interpreted. The `.interp` format only recognises send labels. |
| TWC073 | E | Duplicate interpretation. The aligner would keep only the last one. |
| TWC074 | W | Interpretation of an unknown location (ignored). |
| TWC075 | W | Interpretation of a label that no transition carries. |
| TWC076 | W | A synchronised label without interpretation. The aligner neither matches nor explores it. |

`tests/unit/compiler/compiler_test.cpp` contains one minimal malformed fixture for each code
that can be reached through UTAP 2.1, and checks that the compiler refuses it with that code.
