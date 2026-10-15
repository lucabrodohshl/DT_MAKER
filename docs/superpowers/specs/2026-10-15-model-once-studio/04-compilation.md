# 4. Compilation architecture

```
            Studio canvas, inspector, keyboard
                         │  operations = named JSON patches
                         ▼
           ┌───────────────────────────────┐
           │ CANONICAL MODEL  twin-model/1 │  model section of the Blueprint version
           └───────────────────────────────┘
              │ resolve + validate (TML diagnostics)
     ┌────────┴──────────────┬──────────────────────────┬────────────────────────┐
     ▼                       ▼                          ▼                        ▼
 SEMANTIC COMPILER      DOMAIN-THEORY COMPILER     BEHAVIOUR COMPILER       CONTRACT DERIVATION
 OWL 2 + SHACL schema   K (.ont, SMT-LIB2)         twin-ta/1 V_D            data contract, monitors,
 RDF knowledge graph    I_D (and I_P from the      UPPAAL XML (toolchain)   presentation, structure
 (Turtle, JSON-LD)      reference mapping)              │                   (Blueprint sections)
     │                       │                          ▼
     │                       └──────────▶ aligner ◀── compiler (translation-validated IR)
     ▼                                      │                │
 KG explorer, export                 alignment evidence   Twin IR ─▶ Verified Core Package
                     every output carries a trace: generated element → model element ids + rule
```

All compilers live in one C++ library, `twin::model` (`include/twin/model/`, `src/model/`), with
no I/O and no clock: `compile(document) → {artefacts, diagnostics, trace}`. The server calls it on
every save of the `model` section; the CLI exposes it as `twin model check|compile|lift`.

## 4.1 Determinism

- Input is the canonical JSON of the document (`twin::json` canonical form); layout and labels are
  excluded from the *semantic digest*.
- Every output is a pure function of the semantic content and the compiler version: no timestamps,
  random ids, hash-map iteration or locale. Ordering rules are stated with each transformation.
- Turtle is written without blank nodes, triples sorted by subject, predicate, object; JSON-LD is
  derived from the same sorted triples.
- Tests compile every example twice and compare bytes, and compare against golden files.
- AI may suggest edits in the editor; it never participates in compilation.

## 4.2 Naming of generated identifiers

| Generated | Rule | Example |
|---|---|---|
| asset path | part names from the root, joined by `__`; the root asset has the empty path | `pump_01__motor` |
| qualified member | `path__member`, or `member` at the root | `pump_01__bearing_temp`, `temp_limit` |
| channel of an event/action | qualified member | `tank_01__level_high` |
| timeout channel | qualified `<State>_timeout` | `pump_01__Starting_timeout` |
| clock | qualified timer | `pump_01__t` |
| constant | `<Type>__<NAME>`, or `NAME` for the root type | `CentrifugalPump__T_START` |
| product location | per machine `<path>_<State>` (just `<State>` for the root), joined by `__` | `pump_01_Running__tank_01_High` |
| ontology symbol | qualified property | `pump_01__motor__current` |
| generated relation | `<channel>_requested` (action), `<channel>_reported` (reported event without meaning), `<channel>_elapsed` (timeout) | `pump_01__reset_requested` |
| IRI | `<base><Type>`, `<base><Type>.<member>`, `<base>asset/<path with "/">` | `urn:vts:pump_station:CentrifugalPump.bearing_temp` |

Generated names must be unique; a collision is error TML090 naming both sources.

## 4.3 Semantic compiler: OWL 2, SHACL, RDF

**Ontology schema vs knowledge graph.** The *schema* (T-box) is the vocabulary: classes, properties
and their constraints, identical for every twin of the model version. The *knowledge graph*
(A-box) is the individuals: this model's assets, their parts, relationships and fixed values; at
deployment, the same rules run with the twin's id as base, adding per-twin values and
`owl:sameAs` links of external parts to estate assets.

**Why these targets.** RDF for the graph (standard, queryable, mergeable across twins); RDFS/OWL 2
for the vocabulary (classes, sub-classes, object/datatype properties, inverses, symmetry) —
open-world, used for description and inference; **SHACL** for constraints (datatypes, required
values, ranges, cardinalities, relationship end types) — closed-world, used for validation. OWL
`rdfs:domain/range` are inference axioms, not checks, so every restriction the model *enforces*
is also emitted as a SHACL shape. Units use QUDT unit IRIs; measured properties are typed as
`sosa:ObservableProperty`. The platform vocabulary `tw:` (`urn:vts:tw#`) defines `tw:Asset`,
`tw:hasPart`, `tw:partOf`, `tw:State`, `tw:Event`, `tw:Action`, `tw:Transition` and a few
annotation properties.

Rules (`S*` schema, `G*` graph):

| Rule | Model | Triples |
|---|---|---|
| S1 | asset type `T` | `:T a owl:Class ; rdfs:subClassOf tw:Asset ; rdfs:label "label"` |
| S2 | `T extends U` | `:T rdfs:subClassOf :U` |
| S3 | property `p` of `T` (`fixed`, `per-twin`) | `:T.p a owl:DatatypeProperty ; rdfs:domain :T ; rdfs:range xsd:{decimal,integer,boolean,string}` + shape `:T.p-shape sh:path :T.p ; sh:datatype … ; sh:minCount 1 ; sh:maxCount 1 [; sh:minInclusive ; sh:maxInclusive]`, `sh:in` for enums |
| S4 | property `p` of `T` (`measured`) | `:T.p a owl:ObjectProperty ; rdfs:domain :T ; rdfs:range sosa:ObservableProperty` + annotations `qudt:hasUnit`, `tw:period`, `tw:minimum/maximum` |
| S5 | relationship type `r` | `:r a owl:ObjectProperty`; `owl:inverseOf :inv`; `a owl:SymmetricProperty`; `rdfs:domain/range` when `from`/`to` are given; SHACL `sh:class` on the property shapes of the end types |
| S6 | part `q : U` of `T` | `:T.q rdfs:subPropertyOf tw:hasPart ; rdfs:range :U` + shape `sh:path :T.q ; sh:class :U ; sh:minCount 1 ; sh:maxCount 1` |
| S7 | event / action / state / transition of `T` | individuals `:T.e a tw:Event ; tw:origin "condition" ; tw:condition "…"`, `:T.s a tw:State ; tw:meaning "…"`, `:T.r a tw:Transition ; tw:from ; tw:to ; tw:trigger` (behaviour is described in the graph for queries and trace; its formal meaning is K/V_D) |
| G1 | asset at path `a` of type `T` | `asset:a a :T ; rdfs:label ; tw:path "a" ; tw:element "id"` |
| G2 | part `q` of asset `a` | `asset:a :T.q asset:a/q` and `asset:a tw:hasPart asset:a/q` |
| G3 | relationship `x r y` | `asset:x :r asset:y` |
| G4 | fixed value (part value or type default) | `asset:a :T.p "90"^^xsd:decimal` |
| G5 | measured property | `asset:a :T.p asset:a/p . asset:a/p a sosa:ObservableProperty, tw:Signal ; tw:channel "a__p"` |
| G6 | the model | `<base> a owl:Ontology ; owl:versionInfo "v"` (schema) |

The KG never stores live values; it names the observable property each time series belongs to.

## 4.4 Domain-theory compiler: K, I_D, I_P

| Rule | Model | `.ont` / `.interp` |
|---|---|---|
| K1 | measured/fixed `real`/`integer` property at path `a` | `fun a__p : Sort` — `Sort` from the unit's quantity (`degC → Temperature`, `bar → Pressure`, `% → Ratio` …), else `Real`. Integers are typed as reals; equivalences that hold only over integers are therefore not found (conservative, reported as info TML201) |
| K2 | measured/fixed `boolean` | `rel a__p :` |
| K3 | `enum` with literals `L₀…Lₙ` | `fun a__p : Real` + `axiom a__p_domain : (or (= a__p 0) … (= a__p n))`; `p == Lᵢ` compiles to `(= a__p i)` |
| K4 | fixed value | `axiom a__p_value : (= a__p v)` (booleans: `a__p` or `(not a__p)`) |
| K5 | range of a measured property | `axiom a__p_min : (>= a__p min)`, `axiom a__p_max : (<= a__p max)` |
| K6 | assumption | `axiom assume_<name> : φ` |
| K7 | action / reported event without meaning / timeout | `rel <channel>_requested :` / `_reported` / `_elapsed` |
| I1 | product location `(s₁,…,sₙ)` | `L : (and m(s₁) … m(sₙ))` where `m(s)` is the state meaning conjoined with its groups' meanings; `true` if none |
| I2 | channel of a condition event | `ℓ! : when` |
| I3 | channel of a reported event | `ℓ! : meaning` or `ℓ! : ℓ_reported` |
| I4 | channel of an action | `ℓ! : (and ℓ_requested meaning)` or `ℓ! : ℓ_requested` |
| I5 | timeout channel | `ℓ! : ℓ_elapsed` |
| I6 | linked channel (event `e` drives action `a`) | the event's interpretation (the link states that they are one occurrence) |
| P1 | reference location `L ↦ [states]` | `L : (and m(…))`; `L ↦ {meaning}` → that meaning |
| P2 | reference label `c ↦ trigger` | `c! : I_D(trigger)`; `internal` → no entry |

Expression translation: `a and b → (and a b)`, `not a → (not a)`, `a != b → (not (= a b))`,
`a == b → (= a b)`, `a + b → (+ a b)`, `-a → (- a)`, numbers as exact decimals with negatives as
`(- n)`, booleans as relation symbols. Ordering: symbols in part-tree pre-order then member order;
axioms in the same order; interpretation entries: locations in automaton order, then channels in
declaration order.

## 4.5 Behaviour compiler: twin-ta/1 and UPPAAL XML

1. **Instantiate** one machine per asset whose type has a behaviour (pre-order), qualifying
   timers, constants and channels (4.2).
2. **Expand** group sources (member order), timeouts (edge + invariant atom `x <= d`; the label is
   declared as a channel), `restart` (state-change policy: reset on edges with source ≠ target;
   explicit timers where listed), `ignore` self-loops (for each state without a transition on the
   trigger, in state order).
3. **Link**: for each link `e → a`, duplicate every transition on `a` with `e`'s label (link
   order); keep the original unless `a` is used only through links and is not requestable
   (`requestable: false`).
4. **Product** (more than one machine): BFS from the initial tuple; product edges for each label in
   channel order, combinations in machine order; unsynchronised labels interleave. Location
   invariants are conjunctions; edge ids `e<k>` in emission order.
5. **Emit** `twin-ta/1`: clocks (machine order), constants, channels (declaration order),
   locations (BFS order, initial first), edges; notes carry the trace key.
6. **Render** with the existing `render_toolchain_xml` (`P = T(); system P;`); the existing
   compiler validates and translates it into the IR with translation validation.

The output is in the supported fragment by construction: one template; only clocks; plain
channels; guards are conjunctions of `x ⋈ c`; invariants conjunctions of `x ⋈ c` / `x − y ⋈ c`;
resets to zero; initial location first. The existing validator (`twin::authoring::validate`) runs
on the result as a second check; any finding there is a compiler bug (TML099).

## 4.6 Contract derivation (existing Blueprint sections)

| Derived section | From |
|---|---|
| `structure` | types → asset types (fixed values as properties); assets → assets with parents from the part tree (root = the twin; external parts → `scope: context`); relationships |
| `data.telemetry` | measured properties: id = channel, unit, range, period → `expectedPeriodMs`, `ontologySymbol` = symbol |
| `data.events` | events (reported and condition): id = channel, `formal.dt = ℓ!`, `formal.pt` from the reference mapping |
| `data.commands` | actions: id = channel |
| `data.properties` | `per-twin` properties |
| `presentation.states` | state labels and tones per product location (tone = the most severe component) |
| `assurance.monitors` | conformance (always), data quality: stale (3 × period) and out-of-range per measured property, requirements |

Derived sections carry `"derivedFrom": "model"`; the server refuses direct edits to them while the
`model` section exists.

## 4.7 Validation

Diagnostics have a stable code, severity, message, hint and the element path
(`types[CentrifugalPump].behavior.transitions[r3]`). Codes:

| Code | Sev | Meaning |
|---|---|---|
| TML001 | E | not a `twin-model/1` document / unknown key / wrong type |
| TML002 | E | invalid or reserved name |
| TML003 | E | duplicate name in scope |
| TML004 | E | unknown type / part / member / timer / state reference |
| TML005 | E | part or inheritance cycle |
| TML006 | E | root type missing |
| TML010 | E | property: missing value for `fixed`, value outside range, bad decimal, enum without literals |
| TML011 | E | string property used in an expression |
| TML020 | E | expression syntax error (with column) |
| TML021 | E | expression type error |
| TML022 | E | expression references something out of scope |
| TML023 | E | non-linear product |
| TML030 | E | condition event without `when`; `when` on a reported event |
| TML031 | E | link from something that is not an event, or to something that is not an action |
| TML040 | E | relationship end type not allowed by the relationship type |
| TML050 | E | behaviour without exactly one initial state |
| TML051 | E | bound not a non-negative integer or a known constant |
| TML052 | E | window references a timer of another behaviour; diagonal atom in a window |
| TML053 | E | two transitions from a state on one trigger may be enabled together (nondeterminism) |
| TML054 | E | more than one timeout from a state / timeout cut off by a smaller limit |
| TML055 | W | state unreachable from the initial state (untimed) |
| TML056 | W | redundant limit (implied by a timeout) |
| TML057 | W | event never used by any behaviour |
| TML060 | E | product exceeds 2 000 locations |
| TML070 | E | reference mapping: unknown PT location/label, unmapped label, mapped trigger missing |
| TML080 | E | external part with behaviour |
| TML090 | E | generated name collision |
| TML099 | E | internal: generated automaton rejected by the authoring validator |
| TML201 | I | integer treated as real in the domain theory |

## 4.8 Trace

`trace.json` maps every generated element to its sources: `{kind: "location", name:
"pump_01_Running__tank_01_High", sources: [{id: "s3", rule: "B4.product"}, {id: "ts2", rule:
"B4.product"}]}`. The Generated view uses it in both directions: select a model element → its
generated elements highlight; select a generated element → "Produced by" with links.

## 4.9 Unsupported (refused, never approximated)

| Wanted | Why not | Express it as |
|---|---|---|
| "only if pressure < max" on a transition | no data variables in the fragment | a condition event, or split the state by a condition event |
| counters ("after 3 retries") | no data variables | explicit states (`Retry1`, `Retry2`) |
| priorities between transitions | the aligner ignores priorities | disjoint windows |
| broadcast / "whoever can, reacts" | broadcast channels are outside the fragment | link + `unexpected: ignore` |
| urgent / committed states | the aligner ignores urgency | a timeout `after 0` |
| actions that set property values | the twin observes, it does not assign | expected consequence event with a window |
| disjunction in windows | the aligner reads `||` as `&&` | two transitions |
