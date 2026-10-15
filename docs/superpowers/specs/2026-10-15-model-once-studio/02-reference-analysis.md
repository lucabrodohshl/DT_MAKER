# 2. Reference analysis

Sources: nodered.org and its editor guide (Node-RED 5); the `@digitaltwins/node-red-contrib-digital-twin`
package (v0.1.4) and its repository; the AWS IoT TwinMaker guide and API reference (entities,
component types, knowledge graph, scenes); the report *Digital Twin Concepts, Architectures, and
Tool Comparison* (Xu, 2026) supplied with the request; UPPAAL; DTDL as described in that report.

## 2.1 Node-RED

**Model.** Flows of nodes with at most one input and many outputs, wired by untyped wires; nodes
are configured in an edit dialog; reusable configuration (an MQTT broker) lives in *config nodes*
shared by many nodes; *subflows* package a selection as a reusable node; *groups* frame nodes
visually; one tab per flow.

**Interaction patterns worth adopting**

| Pattern | Why it works | Adopted as |
|---|---|---|
| Quick-add at the cursor (Ctrl/⌘-click), filtered list, recently used first | creation without leaving the canvas | double-click / `N` on the canvas opens Quick add |
| Ports that say what can connect | wiring is self-explanatory | event ports (out) and action ports (in) on asset nodes |
| Config nodes shared by many nodes, with a usage count | one broker definition, many users | **Connections**: one broker/endpoint, many twins |
| Subflows: selection → reusable node | reuse without copy-paste | "Make type from selection" (parts become a new asset type) |
| Search across every property (`is:invalid`, `uses:`) | find by meaning, not by page | search with filters (`is:error`, `type:Pump`, `uses:temperature`) |
| Node status line under the node | runtime state in place | live value / state under each asset in Operate |
| Blue dot = undeployed change, red triangle = invalid | state visible on the element | changed / invalid markers on nodes |
| Import/export JSON of a selection | portable fragments | copy/paste of model fragments as `twin-model/1` |

**To avoid.** Node-RED's semantics *is* message passing: behaviour is hidden in Function nodes and
Switch thresholds, so a flow cannot be verified or compiled into anything formal. Everything is a
node, so an asset with ten properties becomes eleven nodes; the canvas does not scale with model
size. The edit dialog is modal.

## 2.2 The Node-RED Digital Twin nodes

Seven nodes — `dt-asset`, `dt-property`, `dt-event`, `dt-action`, `dt-relation`, `dt-graph`,
`dt-model`. Wires are untyped; the relation is inferred from the node type at the other end
(asset→property = has-property, asset→event = has-event…); `dt-relation` nodes stand between
assets. On deploy, the editor collects every `dt-*` node and the runtime emits the graph — the
published code emits **Neo4j Cypher statements** and a JSON model object (the methodology paper,
Steinmetz et al., ETFA 2022, describes JSON-LD). No units, datatypes, state machines or timing
appear in the package; the help text is template text; it has had no update for about four years.

**Adopt**: the vocabulary (asset, property, event, action, relation) — it matches how engineers
describe a system, and the report's five-step method (identify observable assets → model → add
semantics → communication interfaces → deploy) is the natural journey; the graph generated *as a
consequence of* modelling, never edited by hand.

**Avoid**: properties, events and actions as separate canvas nodes (the request warns against
exactly this); untyped wires whose meaning depends on the far end; a graph emitted on deploy
instead of continuously; semantics without datatypes, units or behaviour.

## 2.3 AWS IoT TwinMaker

**Model.** *Entities* (with one parent → `isChildOf`) carry *components*; a *component type*
declares `propertyDefinitions` (data type incl. `RELATIONSHIP`, unit, default, time-series flag),
data-connector `functions` (Lambda readers/writers), `extendsFrom` (up to 10 parents) and
composite components. Relationships are properties of type `RELATIONSHIP` on a component; the
knowledge graph is built automatically from entities and relationship properties and queried with
PartiQL `MATCH` patterns. *Scenes* bind 3D nodes and tags to entity properties with JEXL colour
rules. Alarms are an abstract component type whose status a Lambda supplies.

**Adopt**

- The **type/instance split** with typed property definitions (data type, unit, default) and
  inheritance (`extendsFrom`) → asset types with properties and single `extends`.
- **Knowledge graph built automatically** from the model, queried, explored around a focus.
- **Separation of concerns**: entities (what exists), components (capabilities), scenes
  (presentation), data connectors (integration). In our model the separation is: structure and
  behaviour (model), layout (arrangement), connections (integration).

**Avoid**

- Relationships as *properties of a component* (they have no type of their own; the edge label is
  the property name; target constraints are optional and unenforced) → relationships get a
  vocabulary entry with an inverse, domain and range, and are validated.
- Data connectors inside the type (Lambda ARNs in component types) → integration is per
  deployment.
- **No behaviour**: alarms are values a Lambda returns; rules only recolour tags. Nothing is
  checked or executable. Our states, transitions and timing are the differentiator.
- JSON request bodies as the editing surface; configuration spread over consoles (the report could
  not complete the TwinMaker or Azure Digital Twins implementations because of configuration
  complexity — Node-RED was the only one finished).

## 2.4 UPPAAL

Templates of locations (invariants), edges (guards, synchronisation, updates), declarations,
system line; editor with a drawing canvas and a separate declarations text. The notation — rounded
locations, initial marker, guard/sync/update labels on edges — is the *lingua franca* of our
formal users and is kept for the Generated view. **Avoid** as the authoring level: clocks, resets
and channel syntax are implementation detail for most twin builders.

## 2.5 DTDL / Azure Digital Twins

Interfaces (JSON-LD) with properties, telemetry, commands, relationships and components; twin
graph; queries; Logic Apps for behaviour. **Adopt**: the distinction telemetry (measured) /
property (configured) / command (action) — our `measured` / `fixed` / `per-twin` property kinds and
actions. **Avoid**: behaviour outside the model (Logic Apps), JSON-LD authoring.

## 2.6 Principles taken

1. Name things the way engineers do: asset, property, event, action, state.
2. Few node kinds on the canvas; members live in the inspector and on ports.
3. Every derived representation (graph, ontology, automaton) is a *view*, regenerated continuously
   and traceable to the element that produced it.
4. Integration is configured once (Connections) and bound per twin.
5. Behaviour is part of the model and is formal.
6. Direct manipulation first (quick add, drag to connect, inline rename); forms only in the
   inspector; dialogs only for destructive or multi-step operations.

## 2.7 Principles rejected

1. Everything-is-a-node (Node-RED DT).
2. Relationships as untyped wires or as untyped properties.
3. Behaviour in scripts or external workflow engines.
4. JSON as the primary editing surface.
5. A single giant canvas with every concept visible.
