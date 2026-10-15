# 3. Canonical metamodel — the twin modelling language

The language is small on purpose: **eight first-class concepts**, two containers, and a handful of
advanced constructs that exist so that every model in the verified timed-automaton fragment can be
expressed (section 3.9, lifting).

## 3.1 Concepts

```
                         ┌──────────────────────────────────────────────┐
                         │ Model  (format twin-model/1, versioned)      │
                         │  root ──▶ AssetType (the system)             │
                         └──────────────────────────────────────────────┘
                                         │ types *
                                         ▼
  ┌───────────────── AssetType ───────────────────────────────────────────────────────────┐
  │ name, label, extends ─▶ AssetType (0..1)                                               │
  │                                                                                        │
  │  properties *  Property  { kind: measured | fixed | per-twin,                          │
  │                            type: real | integer | boolean | enum | string,             │
  │                            unit, range, value, period }                                │
  │  events *      Event     { origin: reported | condition, when, rearm, meaning }        │
  │  actions *     Action    { meaning }                                                   │
  │  parts *       Asset     { name, type ─▶ AssetType, external?, values }                │
  │  relationships * Relationship { type ─▶ RelationshipType, from ─▶ Asset, to ─▶ Asset } │
  │  links *       Link      { from ─▶ Event (of a part), to ─▶ Action (of a part) }       │
  │  assumptions * Assumption { expr }                                         (advanced)  │
  │  behavior 0..1 Behavior                                                                │
  │     timers *      Timer      { restart: on-state-change | explicit }                   │
  │     constants *   Constant   { value: integer duration }                   (advanced)  │
  │     states 1..*   State      { initial, meaning, limits *, tone }                      │
  │     groups *      Group      { states *, meaning }                                     │
  │     transitions * Transition { from ─▶ State|Group *, to ─▶ State,                     │
  │                                trigger: event | action | timeout | internal,           │
  │                                window *, restart ─▶ Timer * }                          │
  └────────────────────────────────────────────────────────────────────────────────────────┘

  RelationshipType { name, inverse, from ─▶ AssetType *, to ─▶ AssetType *, symmetric }
  Model.reference 0..1  PhysicalReference { automaton (twin-ta/1), state map, label map }  (advanced)
  Model.requirements *  Requirement { kind: never | max-time | property, … }               (advanced)
```

| Concept | Is | Example |
|---|---|---|
| **Asset type** | a class of physical or logical things | `CentrifugalPump`, `StorageTank` |
| **Asset** | a named occurrence of a type inside another type (a part); the model's assets are the parts of the root type | `pump_01 : CentrifugalPump` |
| **Property** | an attribute or observable variable of a type | `bearing_temp: real °C measured −20..150` |
| **Relationship** | a typed, directed semantic association between two assets | `pump_01 feeds tank_01` |
| **Event** | something that happens to an asset, reported by the physical system or derived from a condition on properties | `overheated when bearing_temp > temp_limit` |
| **Action** | something an asset can be asked to do | `start()`, `stop()` |
| **State** | a behavioural mode of an asset type, with an optional meaning and time limits | `Running` (meaning `running`, at most 3600 s) |
| **Transition** | a change of state triggered by an event, an action, a timeout or (advanced) internally, allowed in a time window | `Fault → Idle on reset() not before 60 s` |

### What was evaluated and not made a concept

| Candidate | Decision | Why |
|---|---|---|
| Instance | = **Asset** (occurrence in a model). A *twin* (deployment) is runtime, not modelling | one word for one thing |
| Component | merged into **Asset type + parts** | TwinMaker components bundle data access; DTDL components are composition. Parts cover composition; data access is deployment configuration |
| Guard / condition | split: **time windows** on transitions; **conditions on data** only in condition events and state meanings | the verified fragment has no data variables; a data guard could not be compiled faithfully |
| Timing constraint | attribute of **State** (limits) and **Transition** (window, timeout) | timing always qualifies something |
| Behavior | a **container** on an asset type | not a thing a user reasons about separately |

## 3.2 Identity, names, versions

- Every element has an immutable `id` (generated, unique in the document) used for traceability,
  layout and references across renames; and a `name` (identifier `[A-Za-z][A-Za-z0-9_]*`, no
  `__`, not a keyword: `and or not true false self`) that is unique in its scope and used in
  generated artefacts; and an optional free-text `label`.
- Scopes: type names in the model; member names (property, event, action, part) in a type
  (including inherited members); state, group, timer and constant names in a behaviour.
- References inside a document use **names** (readable JSON, stable diffs); the server rewrites
  references on rename in one operation (the rename is one undo step).
- The document carries `format: "twin-model/1"`. Versions are Blueprint versions (draft /
  published, immutable once published). A future `twin-model/2` is reached by a pure migration
  function with golden tests.

## 3.3 Types, parts and inheritance

- **Root type.** `model.root` names the type that *is* the system. Its parts are the assets shown
  on the structure canvas. The root type may have properties, events, actions and a behaviour of
  its own (the thermal chamber's behaviour belongs to the chamber, which is the root).
- **Parts.** `type.parts[]` are assets: `{name, type, external, values}`. A part instantiates the
  whole member tree of its type (properties, events, actions, behaviour, nested parts). Part
  cycles are an error.
- **External parts** (`external: true`) are references to assets that exist outside the twin
  (the lab, the site). They take part in relationships and in the knowledge graph, are bound to an
  estate asset when a twin is created, and are never instantiated per twin (they replace today's
  `scope: context`). They have no behaviour.
- **Values.** `part.values` overrides `fixed` property values of the part's type for this
  occurrence (`pump_01` may have `temp_limit = 85` while the type says 90). Values are design-time
  constants and part of the verified semantics. `per-twin` values are entered when a twin is
  created and never appear in conditions.
- **Inheritance** (advanced). `extends` names one supertype. The subtype inherits properties,
  events, actions and parts, may add members, may override `fixed` values, and may not declare a
  behaviour if the supertype has one (no behavioural refinement in v1). Cycles are an error.

## 3.4 Properties

| Field | Values | Notes |
|---|---|---|
| `kind` | `measured` | live data from the physical system (telemetry) |
|        | `fixed` | a design-time value, may be used in conditions (thresholds, setpoints) |
|        | `per-twin` | metadata entered per deployed twin (serial number); descriptive only |
| `type` | `real`, `integer`, `boolean`, `enum` (with `values[]`), `string` | `string` cannot appear in expressions |
| `unit` | unit symbol from the unit catalogue (`degC`, `bar`, `%`, `A`, `rpm`, `m3/h`, …) | maps to a QUDT unit and a quantity sort |
| `range` | `{min, max}` exact decimals | the physically possible values: becomes axioms and the out-of-range data-quality check |
| `value` | exact decimal / `true` / `false` / enum literal | required for `fixed` |
| `period` | seconds, exact decimal | expected update period of `measured`: becomes the staleness check (stale after 3 periods unless `staleAfter` is given) |

Numbers that carry meaning are exact decimal strings, never floats (the canonical hashing rule of
the code base).

## 3.5 Expressions

Conditions, meanings, re-arm conditions and assumptions use one small, typed expression language:

```
expr    := or
or      := and ( "or" and )*
and     := not ( "and" not )*
not     := "not" not | cmp
cmp     := sum ( ( "<" | "<=" | ">" | ">=" | "==" | "!=" ) sum )?
sum     := product ( ( "+" | "-" ) product )*
product := unary ( "*" unary )*            -- at least one side of "*" is a number (linear)
unary   := "-" unary | atom
atom    := NUMBER | "true" | "false" | path | "(" expr ")"
path    := IDENT ( "." IDENT )*            -- a property of this type or of a part; an enum literal
```

Typing: arithmetic on `real`/`integer`; comparisons produce booleans; a `boolean` property is
itself a condition; `==`/`!=` compare numbers, booleans or an enum property with one of its
literals. Scope: a type's expressions see the type's own properties and its parts' properties
(`motor.current`), never siblings or parents — so types stay reusable. Expressions compile to
SMT-LIB2 in the QF_LRA fragment (section 4.4).

## 3.6 Events and actions

**Event**: `{name, origin, when?, rearm?, meaning?, unexpected}`.

- `origin: reported` — the physical system (PLC, gateway) reports it. Its optional `meaning` is a
  condition that holds when it is reported (e.g. the PLC's `overheat` means `temperature >
  temperature_limit`).
- `origin: condition` — the platform derives it from data: it occurs at the first sample where
  `when` becomes true, and re-arms when `rearm` holds (default: `not when`). Its meaning is `when`.
  Derivation is deterministic (section 3.8.6).
- `unexpected: nonconformance` (default) — if the event occurs when no transition accepts it, the
  runtime records a conformance violation (today's behaviour). `ignore` — the event is accepted
  and changes nothing in states that have no transition on it (compiled as explicit self-loops).

**Action**: `{name, meaning?, unexpected}`. An action is a controllable input: an operator, a
schedule or a link requests it; the kernel admits it only if the behaviour allows it now
(otherwise it is refused with the reason and *not* forwarded to the physical system). Its meaning
is "requested" plus the optional `meaning` (a precondition claim, e.g. `reset_latch` means
`temperature <= temperature_limit`).

**Link**: `{from: "<part>.<event>", to: "<part>.<action>"}` inside a type. Whenever the event
occurs, the action occurs in the same step (synchronisation, section 3.8.5). Links are how
assets are composed behaviourally on the structure canvas, by wiring an event port to an action
port.

## 3.7 Relationships

`RelationshipType {name, label, inverse?, from[]?, to[]?, symmetric}` — a vocabulary entry; the
model starts with a standard vocabulary (`feeds`, `controls`, `measures`, `monitors`, `powers`,
`connectedTo` (symmetric), `locatedIn`, `heats`, `cools`, `drives`) and users add their own.
`Relationship {type, from, to}` connects two assets in the scope of a type (part paths such as
`pump_01` or `pump_01.motor`, or `self` for the type itself). Containment is **not** a
relationship: it is the part tree (`hasPart` / `partOf` in the graph). `from[]`/`to[]` restrict the
asset types at each end; violations are errors.

## 3.8 Behaviour and timing semantics

### 3.8.1 Machines

Each asset whose type has a behaviour is a **machine** `M_a = (S, s₀, T, X, Inv)`:
states `S` with exactly one initial `s₀`; transitions `T`; timers (clocks) `X`; invariants `Inv`.
A configuration is `(s, v)` with `v : X → ℝ≥0`; time is logical time on the grid of
`ticksPerUnit` (`docs/logical-time-model.md`).

### 3.8.2 States

- `limits[]`: atoms `timer ⋈ bound` (`⋈ ∈ {<, <=}`) and, advanced, `timer − timer ⋈ bound` — "at
  most 600 s in Heating". They form the invariant: the machine may stay only while it holds.
- `meaning`: a condition on properties that holds while the asset is in the state. It is the
  state's interpretation (I_D) and is checked against live data by the semantic evaluator; it never
  drives transitions.
- `tone`: `normal | warning | critical` — presentation in Operate; no formal effect.
- **Groups** name sets of states. A transition `from` a group means "from each member" (expanded
  in member order). A group meaning is conjoined to its members' meanings.

### 3.8.3 Transitions

`{from[], to, trigger, window[], restart[]}`:

| Trigger | Means | Compiles to |
|---|---|---|
| `event e` | e occurs (own event or a part's event) | edge labelled with e's channel |
| `action a` | a is requested and admitted | edge labelled with a's channel |
| `timeout after d [on timer x]` | the asset has been in the source state for d | edge labelled `<state>_timeout`, guard `x >= d`, and `x <= d` added to the source invariant |
| `internal` (advanced) | a spontaneous step of the system | unlabelled edge (τ) |

- `window[]`: atoms `timer ⋈ bound` with `⋈ ∈ {<, <=, ==, >=, >}` — "not before 30 s", "within 5 s".
  A window is the guard; it may only constrain time.
- **Timers.** A behaviour normally has one timer `t` with `restart: on-state-change`: it is reset
  on every transition whose target differs from its source. Self-transitions do not reset it unless
  they list it in `restart`. Advanced: further timers with `restart: explicit`, reset only where a
  transition lists them (e.g. total flight time).
- **Constants** (advanced) name integer durations used as bounds (`T_START = 10`).

### 3.8.4 Well-formedness (enforced; codes in section 4.7)

1. Exactly one initial state per behaviour.
2. Bounds are non-negative integers in model time units (or constants).
3. **Determinism**: for each state, after group expansion, two transitions with the same trigger
   must have disjoint windows (checked per timer as intervals, conservatively). Otherwise error —
   unless `model.allowNondeterminism` is set (advanced), in which case it is a warning and the
   kernel tracks both possibilities, as it does today.
4. At most one timeout per state; a timeout must be reachable (not cut off by a smaller limit).
5. No transition priorities exist; conflicts are resolved by making windows disjoint.
6. A trigger references an event/action in scope; a window references timers of the same behaviour.

### 3.8.5 Composition

The behaviour of a model is the **synchronous product** of its machines (assets in part-tree
pre-order) with **synchronisation on shared labels**:

- every event and action has one channel label (qualified by its asset path);
- a link `e → a` relabels every transition triggered by `a` with `e`'s label (once per link, plus
  the original if `a` is also requested directly), so `e` moves both assets in one step;
- a product edge exists for a label ℓ when every machine whose alphabet contains ℓ has an edge on ℓ
  from its current state: guards are conjoined, resets united, target invariants conjoined.
  Machines without ℓ in their alphabet stay.

This is strict (CSP) synchronisation: if a linked action is not admissible in the target's state,
the event is not admissible either and the observation is non-conformant — unless the action is
declared `unexpected: ignore`, which adds self-loops where it has no transition. The product is
built from the initial tuple by breadth-first search, so only discretely reachable tuples are
emitted and the initial location is first. Sizes above 2 000 locations are refused with the
breakdown per machine.

### 3.8.6 Time, simultaneity, derivation

- **Simultaneous inputs** are processed in arrival (ledger) order at the same logical time.
- **Condition events** are evaluated on each telemetry sample in timestamp order, then sequence
  number; several condition events firing on one sample are emitted in declaration order (model
  pre-order, then member order). An event re-arms only after its re-arm condition holds. Missing
  values make a condition *unknown*: no event, never a false one.
- **Timeouts** are proposed by the runtime's timer service at the earliest instant their edge is
  enabled when no other input arrives at or before that instant; an observed input at the same
  instant is processed first. The kernel decides, as for every other input.
- **Deadlines**: a state limit that expires without any admissible input is reported as
  `deadline_missed` (existing monitoring verdict). No real-time claim is made.

### 3.8.7 What the language deliberately does not have

Data guards on transitions, variable assignments or counters, priorities, urgent/committed
states, broadcast, hierarchical states with history, event payloads that change behaviour,
probabilities. Each is either outside the verified fragment or would need it to be approximated.
Studio says so where the user would look for it (e.g. "Only if…" offers only time conditions and
explains how to express a data condition as an event or a meaning).

## 3.9 Lifting: from a timed automaton to the language

`lift(TA, K?, I?) → Model` produces the model whose compilation is the given automaton.

| TA element | Lifted to |
|---|---|
| template name | root type name |
| location (in order; initial first) | state; invariant atoms → limits |
| clock | timer; `restart: on-state-change` if it is reset on exactly the edges whose source ≠ target (self-loop resets become `restart` entries), else `explicit` |
| constant | behaviour constant |
| edge `a!` | transition triggered by event `a` (root type, `origin: reported`) |
| edge `a?` | transition triggered by event `a` with `channelDirection: "?"` (advanced) |
| unlabelled edge | `internal` transition |
| guard atoms | window |
| resets | `restart` (beyond the timer's policy) |
| edge with label ℓ used once, guard exactly `x >= d`, source invariant containing `x <= d`, and ℓ interpreted as `ℓ_elapsed` (our own output) | `timeout after d` |
| several edges identical but for their source | one transition from those states (grouped) |
| K: `fun s : S` with axiom `(= s v)` | `fixed` real property `s` with value `v` |
| K: other `fun s : S` | `measured` real property |
| K: `rel s :` | `measured` boolean property |
| K: other axioms | assumptions |
| I(location) | state meaning |
| I(label!) | event meaning (`origin: reported`) |

Formulas outside the expression language (quantifiers, non-linear terms) are kept verbatim as
`smt:` meanings (advanced) instead of being rewritten.

**Round-trip guarantee.** For every automaton in the fragment, `compile(lift(TA))` equals TA up to
(a) the order of edges and declarations, (b) layout and notes; with K and I, the generated K and
I are equivalent formula by formula (the formulas are printed back from the parsed AST). For every
model in *lift normal form* (one machine, names as generated), `lift(compile(M)) = M`. A product
automaton lifts to one machine (factorisation is not attempted) unless it carries our compiler's
trace, in which case the source model is restored exactly.

## 3.10 Advanced constructs (all present so that lifting is total)

Named timers with explicit restart, diagonal limits, behaviour constants, internal transitions,
receive-direction channels, raw `smt:` meanings, assumptions, inheritance, external parts,
allow-nondeterminism, the physical reference and requirements. None of them is visible until used.

## 3.11 Physical reference (alignment input)

`reference = {automaton: twin-ta/1, states: {PT location → [state paths] | {meaning}},
labels: {PT channel → trigger path | "internal"}}`. Generated I_P: a PT location means the
conjunction of the mapped states' meanings (or its own meaning); a PT label means exactly what the
mapped trigger means in I_D; an `internal` label gets no interpretation (τ for the aligner).
Mapping is suggested by equal names, never assumed silently.

## 3.12 Example (abridged)

```jsonc
{
  "format": "twin-model/1",
  "name": "pump_station", "label": "Pump station",
  "time": {"unit": "s", "ticksPerUnit": 1000},
  "root": "PumpStation",
  "types": [
    {"id": "t_pump", "name": "CentrifugalPump",
     "properties": [
       {"id": "p1", "name": "running",      "kind": "measured", "type": "boolean"},
       {"id": "p2", "name": "bearing_temp", "kind": "measured", "type": "real", "unit": "degC",
        "range": {"min": "-20", "max": "150"}, "period": "1"},
       {"id": "p3", "name": "temp_limit",   "kind": "fixed",    "type": "real", "unit": "degC", "value": "90"}],
     "events": [
       {"id": "e1", "name": "started", "origin": "reported"},
       {"id": "e2", "name": "overheated", "origin": "condition",
        "when": "bearing_temp > temp_limit", "rearm": "bearing_temp < temp_limit - 5"}],
     "actions": [{"id": "a1", "name": "start"}, {"id": "a2", "name": "reset"}],
     "behavior": {
       "timers": [{"id": "x1", "name": "t", "restart": "on-state-change"}],
       "states": [
         {"id": "s1", "name": "Idle", "initial": true, "meaning": "not running"},
         {"id": "s2", "name": "Starting"},
         {"id": "s3", "name": "Running", "meaning": "running", "limits": [{"timer": "t", "op": "<=", "bound": 3600}]},
         {"id": "s4", "name": "Fault", "meaning": "not running", "tone": "critical"}],
       "transitions": [
         {"id": "r1", "from": ["Idle"], "to": "Starting", "trigger": {"action": "start"}},
         {"id": "r2", "from": ["Starting"], "to": "Running", "trigger": {"event": "started"}},
         {"id": "r3", "from": ["Starting"], "to": "Fault", "trigger": {"timeout": 10}},
         {"id": "r4", "from": ["Running"], "to": "Fault", "trigger": {"event": "overheated"}},
         {"id": "r5", "from": ["Fault"], "to": "Idle", "trigger": {"action": "reset"},
          "window": [{"timer": "t", "op": ">=", "bound": 60}]}]}}
  ]
}
```

The complete example is in [10-worked-example.md](10-worked-example.md).
