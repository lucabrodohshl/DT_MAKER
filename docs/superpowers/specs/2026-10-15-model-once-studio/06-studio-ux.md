# 6. Studio UX specification

## 6.1 Evaluating the suggested layout

The request sketched: header (Validate · Generate · Publish), Explorer left, canvas centre,
Inspector right, and a bottom bar *Structure | Behavior | Generated | Validation*. Kept: the three
columns and the header. Changed:

1. **No "Generate" button.** Generation is continuous: every save recompiles in well under a second
   for the example sizes, and the Generated view always shows the current result. A button would
   suggest the outputs can be stale or edited separately.
2. **No "Validate" button.** Structural validation runs on every change; the heavy formal checks
   (compile with translation validation, alignment, scenario runs) run automatically after a
   pause and show their state in the header. *Run checks now* stays in the Checks panel.
3. **Views at the top of the canvas, not at the bottom.** Structure / Behavior / Generated change
   the main content — they are primary navigation and belong where the eye starts. The bottom is
   for auxiliary, collapsible information: **Problems** and **Checks**.
4. **Validation is not a view.** It is a drawer that can be open over any view, because problems
   are fixed in the view where the element lives.

## 6.2 The Studio frame

```
┌──────────────────────────────────────────────────────────────────────────────────────────────┐
│ ◧ Models / Pump station   v3 DRAFT ▾   Saved            ⌕ Search ⌘K        ● 2 problems  Publish │
├───────────────┬────────────────────────────────────────────────────────────┬─────────────────┤
│ EXPLORER      │  Structure   Behavior   Generated                Graph│Map  │ INSPECTOR       │
│ ⌕ filter      │ ─────────────────────────────────────────────────────────  │                 │
│               │                                                            │ pump_01         │
│ Pump station  │   ┌──────────────┐  feeds   ┌──────────────┐              │ CentrifugalPump │
│  ├ pump_01    │   │ pump_01      │─────────▶│ tank_01      │              │ ─────────────── │
│  │  └ motor   │   │ CentrifugalP.│          │ StorageTank  │              │ Properties    + │
│  ├ tank_01    │   │ ◑ Idle  5 st.│◀╌╌╌╌╌╌╌╌╌│ ▸level_high  │              │ running   bool  │
│  └ site ⇢     │   └──────────────┘  stop()  └──────────────┘              │ bearing_temp °C │
│               │         ┆ locatedIn                                         │ temp_limit = 90 │
│ TYPES         │   ┌ ─ ─ ─ ─ ─ ─ ┐                                          │ Events        + │
│  CentrifugalP.│     site (ext.)                                            │ ▸ started       │
│  StorageTank  │   └ ─ ─ ─ ─ ─ ─ ┘                                          │ ▸ overheated    │
│  Motor        │                                                            │ Actions       + │
│  + New type   │                                                            │ ◦ start ◦ stop  │
│               │                                     [−][+][⤢]  minimap     │ Behavior  5 st. │
├───────────────┴────────────────────────────────────────────────────────────┴─────────────────┤
│ Problems 2   Checks ✓ compiled · alignment not checked (no reference)              ⌃ collapse │
└──────────────────────────────────────────────────────────────────────────────────────────────┘
```

- **Header**: breadcrumb (Models / model name), version menu (switch, compare, new draft,
  history), save state as plain text, search, the check summary (the only coloured element: a
  glyph and a count), **Publish** (the only primary button).
- **Explorer** (240 px, collapsible with `[`): the part tree of the model (what exists) and the
  model's types (what kinds exist); external parts marked `⇢`. Selecting here selects on the
  canvas. Drag a type to the canvas to add an asset.
- **Canvas**: the current view; view tabs at the top-left, view-specific toggles at the top-right
  (Structure: *Graph | Map*; Behavior: machine selector, *Simulate*; Generated: sub-views).
- **Inspector** (320 px, collapsible with `]`): everything about the selection. With nothing
  selected it shows the model (name, description, time unit, relationship vocabulary).
- **Bottom drawer**: *Problems* (list, grouped by element, click = select and reveal) and *Checks*
  (compile, alignment, scenarios; evidence; *Run now*). Collapsed to one line by default.

## 6.3 Visual grammar

| Concept | Representation | Where |
|---|---|---|
| Asset | node: rectangle, 4 px radius, white, 1 px border; name (13/600), type (mono 11, muted); footer line: behaviour summary (`◑ Idle · 5 states`) | Structure |
| Part (nested) | child nodes inside the parent's frame (collapsible, `▸ 2 parts` when collapsed) | Structure |
| External part | dashed border, `⇢` after the name | Structure |
| Relationship | 1.25 px solid line with arrowhead; label chip (mono 10.5) with the relationship name; no arrowhead when symmetric | Structure |
| Event port | small filled triangle `▸` on the right edge; visible on hover/selection or when linked | Structure |
| Action port | small open circle `◦` on the left edge | Structure |
| Link (event → action) | 1 px dashed line from `▸` to `◦`, label chip `event → action()` | Structure |
| Property, event, action | rows in the inspector, never canvas nodes | Inspector |
| Asset type | not on the canvas; Explorer list; opening it shows *its* parts on the canvas with a breadcrumb `Pump station › CentrifugalPump` | Explorer |
| State | rounded rectangle (12 px radius); name (12/600); meaning on a second line (muted, truncated); limits as mono chip `t ≤ 3600` | Behavior |
| Initial state | small filled dot with a short arrow into the state | Behavior |
| Group | thin dotted frame with its name at the top-left | Behavior |
| Transition | curved 1.25 px edge; label chip: trigger glyph + name (`▸ overheated`, `◦ reset()`, `⧗ after 10 s`, `τ`), then window (`t ≥ 60`), then restarts (`⟲ t2`) | Behavior |
| Live overlay | current state 2 px accent border; enabled now: green edge; enabled after a delay: dashed; last taken: violet | Behavior, Twin |
| Error / warning on an element | border in error/warning colour + a small count at the top-right corner | all canvases |
| Selection | 2 px accent outline; selected edge drawn 2 px accent | all canvases |

The graph rendering of today is kept: white nodes with thin borders, dagre auto-layout with manual
positions saved per element, label chips on edges, dotted containment, the live legend.

## 6.4 Canvas behaviour

| Interaction | Result |
|---|---|
| Double-click empty canvas, or `N` | **Quick add** at the cursor: type to filter (assets by type, recently used first; `+ New type "Valve"` when nothing matches); Enter places it, focused for renaming |
| Drag a type from the Explorer | adds an asset of that type |
| Drag from a node's edge handle to another node | relationship; a popover lists the vocabulary filtered by allowed end types, last used first; Enter confirms |
| Drag from an event port `▸` to an action port `◦` | link |
| Drag a node into another asset's frame | makes it a part (only allowed for the type being edited; a confirmation explains that the change applies to the type) |
| Enter / F2 on a selection | inline rename |
| Delete | removes; if other elements depend on it, an inline confirmation lists them (no modal for leaf deletes; undo is always available) |
| `⌘Z` / `⇧⌘Z` | undo / redo of any model operation — one history for the whole model |
| `⌘D` | duplicate (assets get `_2` names) |
| `⌘C` / `⌘V` | copy/paste as `twin-model/1` fragment (works across models) |
| `1` `2` `3` | Structure / Behavior / Generated |
| `F` | fit; `Space`-drag pan; wheel zoom; minimap |
| `/` or `⌘K` | search & commands: elements by name, `is:error`, `type:Pump`, `uses:bearing_temp`, actions (*Add state*, *New type*, *Publish*) |
| Right-click | context menu with the same actions as the inspector |
| Shift-click / lasso | multi-select; inspector shows common fields; *Make type from selection* |

Behaviour canvas specifics: double-click → new state (named `State1`, renaming); drag from a
state's handle → new transition, trigger popover lists the type's events (▸), actions (◦), *after…*
(timeout) and, advanced, *internal*; a transition from a group is drawn from the group frame.

## 6.5 Selection and context

Selection is one model-wide value (`focus=<id>` in the URL) shared by all views:

- select `pump_01` in Structure → switch to Behavior → the machine selector shows `pump_01`'s
  behaviour; → Generated → Automaton highlights every location containing a `pump_01` state, Graph
  centres on `asset:pump_01`, Domain theory highlights `pump_01__*` symbols;
- select a state in Behavior → Generated highlights its product locations, its interpretation
  entry, its `tw:State` individual;
- select a generated element → the inspector shows **Produced by** with the source elements and
  the rule (`B2 timeout`, `K4 fixed value`), each a link that selects it.

## 6.6 Inspector

One component, sections by selection kind, each section a hairline-separated block (no cards):

- **Asset**: name, type (link: *Edit type*), values for fixed properties (inherited value shown
  greyed, override inline), relationships (in/out), links, part of.
- **Type**: name, label, description, *extends* (advanced); **Properties** as a compact table with
  a one-line entry row that parses `name: type unit kind range = value` (e.g.
  `bearing_temp: real degC measured -20..150`); **Events** (`▸ name`, origin, condition);
  **Actions** (`◦ name`); **Parts**; **Behavior** summary with *Open*; **Assumptions** (advanced).
- **State**: name, label, initial, tone, meaning (expression field with completion), limits
  ("at most [600] s"), groups.
- **Transition**: from, to, trigger, *Only if time…* (window: "not before", "not after", "exactly
  at"), restarts (advanced), the generated edges.
- **Relationship / link**: type, ends, *swap direction*.
- **Generated element**: Produced by, source text.

Expression fields: CodeMirror single-line with completion of properties in scope, inline
diagnostics (TML020–023) and a "means" preview (`(> bearing_temp temp_limit)`) shown only in
advanced disclosure.

## 6.7 Generated view

Sub-views (segmented control at the top-right, keys `g` `o` `t` `a` `x`):

| Sub-view | Shows | Source toggle |
|---|---|---|
| **Graph** | the knowledge graph (A-box) explored around the selection; schema toggle shows classes and properties | Turtle / JSON-LD |
| **Ontology** | classes, properties, relationship types, SHACL constraints as tables | Turtle |
| **Domain theory** | K (sorts, symbols, axioms) and I_D (and I_P) as tables with each entry's source | `.ont` / `.interp` |
| **Automaton** | V_D drawn with the existing TA rendering (locations, invariants, edges with guard/sync/reset chips) | TwinTA text / UPPAAL XML |
| **Contract** | derived signals, events, commands, monitors | JSON |

Every sub-view: read-only, *Download*, *Copy*, digest line, and trace highlighting. **Export XML**
is one click in Automaton (and in the version menu).

## 6.8 Simulate

*Simulate* (Behavior view toggle, `S`) turns the canvas live: the current state is highlighted,
transitions available now/later are drawn as in Operate, a bottom timeline records steps. Steps are
added by clicking an enabled transition, typing a property value (condition events fire as they
would at runtime), or *Wait* (to the next enabled instant). A run can be saved as a scenario with
expectations; saved scenarios run on every check. Refused steps show the kernel's reason ("too
early: earliest t = 130").

## 6.9 Feedback

- **Save**: plain text in the header: *Saved* · *Saving…* · *Not saved — retry* · *Changed elsewhere —
  reload*. No badge.
- **Problems**: count with glyph in the header (`● 2 problems` in the error colour, `▲ 1 warning`,
  or `No problems` in muted text); elements carry markers; the drawer lists them; each has a *Fix*
  action when one is mechanical (add missing initial state, rename collision).
- **Checks**: one line in the drawer header: `✓ compiled · ✓ aligned (strong) · 2 scenarios pass`,
  or what failed with a link.
- **Publish**: disabled with the reason as tooltip while errors exist; the dialog shows what will
  be published (version, digest, checks) and has one button.
- **Loading**: skeleton lines inside panels, never full-page spinners; the canvas keeps the last
  render while recompiling.
- **Empty states**: one sentence and the next action (*Double-click to add your first asset*).
- **Destructive operations**: undo first; confirmation only when dependents exist or the operation
  cannot be undone (deleting a published version is impossible; deleting a twin asks).

## 6.10 Wireframes

### Models home

```
┌────────────────────────────────────────────────────────────────────────────┐
│ ◧ Verified Twin Studio     Models  Twins  Connections            ⌕   user ▾ │
├────────────────────────────────────────────────────────────────────────────┤
│ Models                                                       New model     │
│                                                                            │
│ Name                 Version     Checks        Twins   Changed             │
│ ────────────────────────────────────────────────────────────────────────── │
│ Pump station         v3 draft    ● 2 problems  1       2 min ago           │
│ Thermal chamber      v1          ✓ verified    1       Oct 12              │
│ Indoor drone         v2 draft    ✓ verified    1       Oct 11              │
│                                                                            │
│ Start from                                                                 │
│ Blank model · Process equipment · Mobile robot · Import timed automaton ·  │
│ Import bundle                                                              │
└────────────────────────────────────────────────────────────────────────────┘
```

### Behavior view

```
┌──────────────────────────────────────────────────────────────────────────────────────────┐
│ ◧ Models / Pump station   v3 DRAFT ▾   Saved          ⌕ ⌘K       ● 2 problems    Publish │
├─────────────┬──────────────────────────────────────────────────────┬─────────────────────┤
│ EXPLORER    │ Structure  Behavior  Generated     pump_01 ▾  Simulate │ TRANSITION          │
│             │ ──────────────────────────────────────────────────── │ Fault → Idle        │
│ pump_01     │   ●→╭────────╮ ◦ start() ╭──────────╮ ▸ started      │ ─────────────────── │
│  states     │     │ Idle   │──────────▶│ Starting │─────────┐      │ Trigger  ◦ reset()  │
│   Idle      │     │not run.│           │          │ ⧗ 10 s  │      │ Only if  not before │
│   Starting  │     ╰────────╯           ╰──────────╯   ▼     ▼      │          [60] s     │
│   Running   │        ▲ ◦ reset() t≥60          ╭─────────╮ ╭──────╮│ Restart  —          │
│   Fault     │        └──────────────────────── │ Fault   │◀│Runn. ││ ─────────────────── │
│ ··········· │                                  │not run. │ │t≤3600││ Generated           │
│ ⧗ timer t   │                                  ╰─────────╯ ╰──────╯│ e9 Fault→Idle       │
│             │                                    ▸ overheated      │ guard t ≥ 60        │
└─────────────┴──────────────────────────────────────────────────────┴─────────────────────┘
```

### Generated → Automaton (with trace)

```
│ Structure  Behavior  Generated            Graph Ontology Theory [Automaton] Contract │
│ ─────────────────────────────────────────────────────────────────────────────────── │
│  pump_station  ·  15 locations · 31 edges · 1 clock · digest 3f9a…   TwinTA XML ⤓    │
│                                                                                     │
│   (existing TA diagram, product locations named pump_01_Idle__tank_01_Normal …;    │
│    locations derived from the selected state are outlined in the accent colour)     │
│                                                                                     │
│ Inspector → Produced by: pump_01.Running (state, rule B4 product), tank_01.High …   │
```

### New model

```
┌──────────────────────────────── New model ───────────────────────────────┐
│ Name            [Pump station                               ]           │
│ Start from      (•) Blank   ( ) Template ▾   ( ) Import timed automaton │
│                                                                          │
│                                              Cancel      Create model    │
└──────────────────────────────────────────────────────────────────────────┘
```

### Twin overview (Operate)

```
┌──────────────────────────────────────────────────────────────────────────────────────┐
│ ◧ Twins / Pump station P-1   ● Running · conformant      model v3 · Open model  ⋯   │
├──────────────────────────────────────────────────────────────────────────────────────┤
│ Overview  Behavior  Data  History  Connection  Settings                              │
│ ──────────────────────────────────────────────────────────────────────────────────── │
│ State            pump_01 Running (12 min) · tank_01 Normal                           │
│ Next possible    stop() now · Running_timeout at t=3600                              │
│                                                                                      │
│ pump_01                     tank_01                       Alerts                     │
│ bearing_temp   71.4 °C      level   63.2 %                none                       │
│ running        true                                                                  │
│ motor.current  11.2 A                                                                │
│                                                                                      │
│ (structure canvas in live mode: values under nodes, states on nodes)                 │
└──────────────────────────────────────────────────────────────────────────────────────┘
```
