You are acting as the principal software architect, formal-methods engineer, and senior implementation engineer for this repository.

Your task is to BUILD, not merely design, a production-quality prototype of a formally grounded Digital Twin execution architecture around the existing semantic-alignment implementation in:

    SemPTDTalignmentICSE/

Do not create a toy proof-of-concept. The result must be structured like software that could realistically evolve into an industrial product.

The core idea is a VERIFIED TWIN EXECUTION KERNEL.

The existing project implements the semantic alignment work for PT/DT views. Treat that project as the existing formal verification foundation. Inspect it thoroughly before implementing anything. Understand its model representations, semantic alignment algorithm, input/output formats, example models, build system, APIs/CLIs, and tests.

Do NOT unnecessarily rewrite the semantic aligner.

Build the new architecture around it.

============================================================
1. FUNDAMENTAL GOAL
============================================================

We have a DT view V_D represented as a timed behavioral model.

We want to construct the deployed Digital Twin implementation D in such a way that:

    D ~ V_D

is not an informal engineering assumption.

It must follow by construction from the compiler/runtime architecture.

The intended chain is:

    V_D
      ≅
    IR(V_D)
      ≅
    K_sem(IR(V_D))
      ~weak
    K_prod(IR(V_D))

where:

    V_D
        = source Digital Twin timed-automaton view

    IR(V_D)
        = canonical executable Twin Intermediate Representation

    K_sem
        = minimal semantic execution kernel

    K_prod
        = production runtime containing K_sem plus logging,
          APIs, telemetry adapters, persistence, metrics, etc.

The first two relations should be designed to be as close to
ISOMORPHISMS as is reasonably possible.

The production runtime may use weak timed bisimulation because internal implementation actions such as:

    logging
    metrics
    serialization
    API handling
    trace persistence

must be semantically invisible τ-actions.

The important result is:

    V_D ~ K_prod(IR(V_D))

under the logical-time semantics defined by this project.

DO NOT claim physical hard-real-time equivalence.

This project explicitly focuses on:

    monitoring
    simulation
    prediction
    planning
    soft-real-time Digital Twin applications

Hard-real-time controller realization, WCET, scheduler latency,
clock synchronization, jitter bounds, RTOS behavior, etc. are
OUT OF SCOPE for this iteration.

============================================================
2. FIRST ACTION: INSPECT THE EXISTING ALIGNER
============================================================

Before choosing architecture or implementation details:

Inspect:

    SemPTDTalignmentICSE/

Determine:

    - how PT and DT timed automata are represented
    - how states, clocks, events, propositions and transitions are encoded
    - how interpretations I_P and I_D are represented
    - how the ontology/domain theory is represented
    - how Z3 is invoked
    - how semantic alignment is checked
    - whether strong and weak semantic alignment are already represented
    - what artifacts are currently emitted
    - what example models exist
    - what reusable libraries/interfaces exist
    - how the current implementation is tested and built

Document your findings briefly in:

    docs/existing-aligner-integration.md

Do not invent a second incompatible timed-automata representation unless technically necessary.

Prefer adapting the existing representation into the new compiler pipeline.

Do not modify SemPTDTalignmentICSE unless necessary. If changes are necessary,
keep them narrow, documented and backward compatible.

============================================================
3. TARGET ARCHITECTURE
============================================================

Create a new architecture roughly corresponding to:

    Existing Semantic Aligner
               |
               v
         Verified DT View V_D
               |
               v
        Twin Model Compiler
               |
               v
        Canonical Twin IR
               |
               v
     Verified Semantic Kernel
               |
       +-------+----------+
       |       |          |
       v       v          v
   Monitor  Predictor   Simulation API
       |
       v
 Production Runtime Shell
       |
 +-----+-------+----------+-----------+
 |             |          |           |
 v             v          v           v
Ledger       REST       MQTT       Visualization
                                        |
                                        v
                                  Demo / Planner

The planner must NOT be part of the trusted semantic kernel.

The runtime is the semantic authority.

The planner queries the runtime for possible/valid behavior and proposes
plans.

============================================================
4. KEEP THE TRUSTED SEMANTIC CORE SMALL
============================================================

The trusted semantic kernel must be deliberately small.

It should contain ONLY what is needed to execute the formal behavioral
semantics:

    locations
    current location
    clocks
    logical time
    finite model variables if supported by the source formalism
    invariants
    guards
    transitions
    clock resets
    state updates
    events
    observable propositions
    enabled-transition computation
    logical-delay transitions
    discrete transitions
    semantic snapshots
    successor computation

Do NOT put inside the semantic core:

    networking
    MQTT
    OPC UA
    web servers
    databases
    visualization
    planners
    authentication
    cloud-specific logic
    logging backends
    message brokers
    persistence engines

Those belong to the production shell.

Make the separation visible in both source-code layout and dependencies.

The semantic-core package should ideally have no network or database
dependencies.

============================================================
5. TWIN INTERMEDIATE REPRESENTATION
============================================================

Implement a canonical Twin IR.

The IR must be intentionally boring.

Avoid inventing a general-purpose programming language.

It should essentially be an executable canonicalization of the DT timed
automaton.

A conceptual IR looks like:

    Model
      id
      version
      locations[]
      initial_location
      clocks[]
      variables[]
      propositions[]
      transitions[]

    Location
      id
      invariant

    Transition
      id
      source
      target
      event
      guard
      clock_resets
      updates

    Proposition
      id
      expression

The exact schema must reflect what the existing semantic aligner actually
supports.

Use a deterministic canonical serialization.

Prefer a readable source representation plus canonical serialized form.

For example:

    JSON / canonical JSON

or an equivalent deterministic format.

Two semantically identical compiled outputs from the same source artifact
must serialize identically.

Give every semantic object a stable identifier.

============================================================
6. COMPILER
============================================================

Implement:

    V_D -> IR(V_D)

The compiler must:

    validate the source model
    reject malformed models
    preserve stable identifiers where possible
    preserve locations
    preserve the initial configuration
    preserve clocks
    preserve invariants
    preserve guards
    preserve resets
    preserve supported variable updates
    preserve events
    preserve observable propositions
    preserve logical-time semantics

The compiler is NOT allowed to silently weaken or strengthen guards,
invariants or transition structure.

Fail loudly on unsupported constructs.

Produce useful compiler diagnostics.

Produce a compilation manifest containing at least:

    source model hash
    IR hash
    compiler version
    compilation timestamp
    source model identifier
    source model version

Compilation must be deterministic.

============================================================
7. LOGICAL TIME SEMANTICS
============================================================

For this project, TIME IS LOGICAL MODEL TIME.

Do not use OS scheduling behavior as formal timed-automata semantics.

A runtime semantic state should conceptually include:

    current location
    logical time
    clock valuation
    model-variable valuation

Incoming observations/events may carry logical timestamps.

For example:

    event:
        obstacle_detected
        logical_time: 31.5

If the previous logical time was:

    28.0

the kernel must first determine whether a logical delay of:

    3.5

is valid according to the model invariants.

Then it may process the discrete event.

Reject events that require an invalid logical-time evolution.

Make ordering behavior explicit.

Define deterministic behavior for events sharing a timestamp.

Do not claim that logical timestamps correspond exactly to wall-clock
physical time.

Document this limitation in:

    docs/logical-time-model.md

and in the formal proof.

============================================================
8. SEMANTIC KERNEL
============================================================

Implement a semantic kernel that executes the IR directly.

The kernel must provide operations equivalent to:

    initialize(model)

    current_state()

    advance_time(delta)

    enabled_transitions()

    apply_event(event)

    evaluate_propositions()

    successors(state)

    simulate(event_sequence)

    clone_state()

    restore_state(snapshot)

The exact API may differ, but all these capabilities must exist.

Prediction must operate by cloning semantic state and exploring successors,
not by modifying the authoritative live state.

The semantic state representation should map almost trivially onto the IR
semantic state.

Design this deliberately so that a clean abstraction function α can be
defined from runtime semantic configurations to formal IR configurations.

============================================================
9. ISOMORPHISM / BISIMULATION DESIGN REQUIREMENT
============================================================

Do NOT write the runtime first and then attempt to invent a proof around it.

Develop implementation and proof together.

If an implementation decision makes the semantic proof unnecessarily
complicated, simplify the implementation.

The semantic kernel should ideally admit a state mapping:

    α : KernelSemanticState -> IRSemanticState

such that semantic configurations correspond one-to-one.

For each semantic source state s and runtime state k:

    α(k) = s

The implementation should make the following facts structurally obvious:

INITIAL STATE

    source initial state
        <->
    runtime initial semantic state

DISCRETE TRANSITIONS

If:

    s --a--> s'

then there exists exactly the corresponding semantic kernel execution:

    k --a--> k'

with:

    α(k)  = s
    α(k') = s'

and conversely.

TIME TRANSITIONS

If:

    s --δ--> s'

then the semantic kernel can advance by the same logical δ:

    k --δ--> k'

with the corresponding states.

And conversely.

PROPOSITIONS

For every observable proposition p:

    p ∈ L(s)

iff:

    kernel.evaluate(p, k) = true

for corresponding s and k.

The production shell may contain internal τ-transitions.

============================================================
10. PRODUCTION RUNTIME
============================================================

Around the semantic kernel, implement a production-oriented runtime.

Responsibilities include:

    model/package loading
    package verification
    execution lifecycle
    input adapters
    event ordering
    prediction API
    monitoring API
    snapshots
    replay
    ledger writing
    diagnostics
    structured logging
    REST API
    websocket/SSE or equivalent live updates for visualization

The production shell must never independently implement semantic
transition logic.

There must be ONE semantic authority:

    the kernel.

No duplicated:

    if state == ...
    if battery < ...
    if event == ...

business-state-machine logic may appear in adapters, UI, planner, etc.

Those components invoke the kernel.

============================================================
11. VERIFIED TWIN PACKAGE
============================================================

Define a deployable Verified Twin Package.

Conceptually:

    package/
        model source/reference
        compiled IR
        ontology reference/artifact
        interpretation reference/artifact
        alignment evidence
        manifest
        hashes

The manifest should include:

    package format version
    model identifier
    model version
    source hash
    IR hash
    ontology hash if applicable
    interpretation hash if applicable
    semantic-aligner version
    compiler version
    kernel compatibility version

Use SHA-256 or an equally standard cryptographic content hash.

Do not pretend hashes alone prove correctness.

Their purpose is provenance and integrity.

At startup, the runtime must reject a package whose content hashes no
longer match its manifest.

============================================================
12. TAMPER-EVIDENT EXECUTION LEDGER
============================================================

This is a first-class product feature.

Implement an append-only, tamper-evident execution ledger.

Do NOT casually call filesystem data mathematically immutable.

Call it a:

    cryptographically tamper-evident append-only ledger

unless backed by storage providing actual WORM guarantees.

Each semantic step should generate a canonical record containing useful
information such as:

    schema version
    execution/session id
    sequence number
    package hash
    model hash
    IR hash
    kernel version
    logical time before
    logical time after
    source semantic state
    event
    selected transition id
    guard evaluation result
    clock resets
    semantic state after
    observable propositions
    input-event digest
    previous record hash
    current record hash

Use hash chaining:

    H_n = H(H_(n-1) || CanonicalRecord_n)

The ledger must detect:

    modification
    reordering
    insertion
    deletion where detectable from continuity/anchors
    broken chain
    wrong package/model identity

Implement a verification command/API.

For example:

    twin ledger verify <ledger>

Also implement deterministic replay:

    verified package + ledger/input trace
          ->
    replayed semantic execution

The replay should reproduce the semantic states and transition sequence.

The ledger must not influence formal transition semantics.

============================================================
13. MONITORING MODE
============================================================

Implement a monitoring mode.

The runtime receives observations from a simulated Physical Twin.

It updates logical time and semantic state.

It should answer:

    What formal DT state are we in?

    Which observable propositions currently hold?

    Which transitions are currently enabled?

    Was the received event compatible with the DT model?

    What transition was taken?

    What model/package generated this conclusion?

Expose this through the runtime API and visualization.

============================================================
14. PREDICTION MODE
============================================================

Implement predictive exploration.

Prediction starts from a CLONE of the current semantic state.

It must not mutate the authoritative live execution.

Support at least bounded successor exploration such as:

    successors(state)

    reachable_states(state, depth/horizon)

    simulate_candidate(state, events)

A planner should be able to use this interface.

The runtime should return semantic trajectories containing:

    locations
    logical timing
    transitions
    propositions
    semantic state snapshots

============================================================
15. DRONE USE CASE
============================================================

Build a serious demonstration around an indoor autonomous drone.

Do NOT make it merely a dot following a static predefined path.

The scenario:

A small inspection drone must navigate through a partially known building
from a start position to one or more inspection targets and eventually to
a destination/return point.

The Physical Twin is a simulator.

The Digital Twin initially possesses only PARTIAL knowledge of the building.

As the simulated drone moves, new environmental information is revealed.

Examples:

    previously unknown wall
    blocked corridor
    newly discovered room geometry
    closed door
    temporary obstacle
    hazardous/no-fly area
    alternate corridor
    inspection target update

The DT receives these changes through a fake but realistic external
environment/map API.

The path planner must replan when its current path becomes invalid or a
meaningfully better safe route becomes available.

The drone must actually execute the newly planned trajectory in the
Physical Twin simulator.

Therefore the demo loop is:

    PT simulator moves drone

        -> telemetry/event

    DT runtime receives observation

        -> logical time advances

    semantic kernel updates V_D state

        -> map knowledge changes

    planner queries current DT state/environment model

        -> planner proposes new path

    runtime checks behavioral admissibility

        -> accepted plan

    PT simulator receives path/waypoint command

        -> drone executes

    environment reveals new information

        -> repeat

============================================================
16. REALISTIC SEPARATION OF BEHAVIOR AND GEOMETRY
============================================================

Do not attempt to encode an entire building occupancy grid as locations
in the timed automaton unless the existing formalism genuinely makes that
reasonable.

The formal behavioral DT should govern modes such as:

    INITIALIZING
    READY
    TAKING_OFF
    NAVIGATING
    REPLANNING
    HOVERING
    INSPECTING
    RETURNING
    LANDING
    LANDED
    MISSION_FAILED

with events such as:

    start_mission
    takeoff_complete
    waypoint_reached
    map_updated
    path_invalidated
    obstacle_detected
    replan_requested
    plan_available
    plan_accepted
    target_reached
    battery_low
    return_requested
    landing_complete

Adapt these to the formalism supported by the existing aligner.

The geometric world model belongs outside the semantic kernel.

The kernel governs the verified BEHAVIORAL state machine.

The planner operates over the evolving geometric world.

This separation must remain explicit.

============================================================
17. PATH PLANNER
============================================================

Implement a real path planner.

A* on an occupancy grid is acceptable for the first correct implementation.

If the architecture cleanly supports it, incremental replanning using
D* Lite or another suitable algorithm is desirable, but correctness and
clarity are more important than algorithmic novelty.

The planner is NOT trusted.

It proposes paths.

The runtime decides whether the behavioral action corresponding to using
that path is permitted in the current formal DT state.

Design the planner behind an interface so another planning algorithm can
later replace it.

The planner should optimize a realistic cost function including some
combination of:

    distance
    obstacle proximity
    hazard penalties
    estimated energy consumption

Keep the objective understandable.

Avoid unnecessary ML.

============================================================
18. FAKE MAP / ENVIRONMENT API
============================================================

Build a local fake external service representing a building-map /
environment-information provider.

It should behave like an external Digital Twin data source.

Provide endpoints or message interfaces conceptually like:

    GET /map/known
    GET /map/updates?since=...
    GET /mission
    POST /telemetry
    GET /hazards

or a cleaner equivalent.

The important property is that the DT does NOT directly access the
simulator's complete ground-truth map.

Maintain:

    ground_truth_world

separately from:

    twin_known_world

Information must flow through the simulated sensor/environment API.

The demo should therefore make visible the epistemic difference:

    what physically exists
    versus
    what the twin currently knows.

This is essential to the demonstration.

============================================================
19. DRONE PHYSICAL-TWIN SIMULATOR
============================================================

Implement a deterministic/reproducible 2D indoor drone simulator.

It does not need aerodynamic fidelity.

It SHOULD have credible mission-level behavior:

    position
    velocity or fixed movement rate
    logical time
    battery / energy state
    current waypoint
    obstacle sensing radius
    mission state

Use a fixed random seed where randomness is used.

The simulator must maintain the complete building ground truth.

The DT must never obtain the complete map by bypassing the simulated
sensor/map interface.

Implement at least one carefully designed scenario where:

    the initial DT path appears valid

    the drone starts following it

    new map information reveals that the route is blocked

    the DT receives the new information

    the behavioral twin transitions through REPLANNING

    a new route is generated

    the drone starts following the new route

    later another update causes either another replan or a mission-level
    decision such as return/inspection rerouting

The simulation should terminate successfully in a repeatable demo.

============================================================
20. COOL LIVE VISUALIZATION
============================================================

Build a polished web visualization.

Do not settle for terminal output.

The visualization should run locally and make the architecture understandable
to someone who knows nothing about the source code.

At minimum show:

LEFT / MAIN AREA:

    building floor plan / occupancy grid
    known free space
    unknown space
    known walls
    discovered obstacles
    hazards / no-fly cells
    inspection targets
    destination
    drone position
    executed trajectory
    current planned trajectory
    previous invalidated trajectory

Clearly distinguish:

    GROUND TRUTH

from:

    DIGITAL TWIN KNOWLEDGE

Prefer a toggle, split view, overlay or opacity control.

A particularly good view would show:

    left: actual simulated building
    right: current DT belief

or allow switching between them.

SIDE PANEL:

    logical time
    physical simulator mission time if separately represented
    current formal automaton state
    current transition
    enabled transitions
    observable propositions
    battery
    current target
    planner status
    package/model hash abbreviated
    semantic alignment status
    ledger chain status

EVENT STREAM:

    Map update received
    Unknown corridor discovered
    Current path invalidated
    DT: NAVIGATING -> REPLANNING
    Planner produced path #4
    DT accepted plan
    Drone resumed navigation

LEDGER:

Show recent tamper-evident records with:

    sequence
    logical time
    transition
    previous hash
    record hash

Include a visible:

    VERIFIED / LEDGER VALID

status derived from actual verification, not hard-coded text.

Controls should include:

    Start
    Pause
    Step
    Reset
    Simulation speed

Optionally:

    inject obstacle
    inject map update

but only if these controls work cleanly.

Make the re-planning visually obvious.

Animate the drone movement and path changes smoothly.

The visualization must consume runtime APIs/events.

It must not reimplement the semantic state machine in JavaScript.

============================================================
21. FORMAL PROOF DOCUMENT
============================================================

Create:

    proof/

containing a professional LaTeX document.

Use proper:

    definitions
    notation
    lemmas
    theorems
    proofs
    assumptions
    limitations

The resulting document should compile into a coherent technical report.

Suggested organization:

    proof/main.tex
    proof/notation.tex
    proof/source-semantics.tex
    proof/ir-semantics.tex
    proof/compiler-correctness.tex
    proof/kernel-semantics.tex
    proof/kernel-isomorphism.tex
    proof/production-bisimulation.tex
    proof/composition.tex
    proof/limitations.tex

You may choose another clean organization.

DO NOT claim that the proof has been mechanically verified.

This iteration uses a rigorous pen-and-paper style mathematical proof
written in LaTeX.

Future mechanization in Lean/Coq/Isabelle is explicitly future work.

============================================================
22. PROOF 1: COMPILER CORRECTNESS
============================================================

Formalize the relevant semantics of V_D and IR(V_D).

Define an explicit mapping:

    C : Config(V_D) -> Config(IR(V_D))

Preferably show it is bijective over reachable semantic configurations.

Prove preservation and reflection of:

    initial configuration
    discrete transitions
    logical delay transitions
    labels / observable propositions

The intended theorem should be equivalent to:

    V_D ≅ IR(V_D)

if the actual representations permit strict isomorphism.

If not, precisely explain why and prove the strongest valid equivalence,
preferably strong timed bisimulation.

Do NOT weaken the theorem merely because implementation shortcuts were
taken. Prefer changing the implementation to recover the cleaner theorem.

============================================================
23. PROOF 2: SEMANTIC KERNEL CORRECTNESS
============================================================

Define the kernel semantic transition system.

Define:

    α : KernelSemanticState -> IRSemanticState

Prove that the semantic kernel realizes the IR.

Prefer:

    IR(V_D) ≅ K_sem(IR(V_D))

through a state isomorphism.

At minimum prove both directions:

SOUNDNESS

Every semantic transition executed by the kernel corresponds to a valid
IR transition.

The kernel cannot invent behavior.

COMPLETENESS

Every valid IR transition can be represented/executed by the semantic
kernel.

The kernel does not silently remove formal behavior.

Also prove logical-delay correspondence and observable proposition
preservation.

============================================================
24. PRODUCTION WRAPPER COROLLARY
============================================================

Model runtime-only activities as τ-actions.

Examples:

    ledger append
    metrics update
    API serialization
    trace buffering
    websocket publication
    snapshot persistence

State clearly which actions are semantic and which are internal.

Prove or rigorously argue that:

    K_sem(IR(V_D))
        ~weak
    K_prod(IR(V_D))

provided runtime bookkeeping does not mutate semantic state except through
the kernel interface.

============================================================
25. COMPOSITION THEOREM
============================================================

The proof document should conclude with a result of the form:

    V_D
      ≅
    IR(V_D)
      ≅
    K_sem(IR(V_D))
      ~weak
    K_prod(IR(V_D))

therefore:

    V_D ~weak K_prod(IR(V_D))

under the project's logical-time semantics.

Connect this result explicitly to the existing semantic-alignment framework:

    P ≼ V_P
    V_P ~Φ V_D
    V_D ~ D

where this project provides a constructive implementation mechanism for
the final relation.

Do not make claims stronger than those assumptions permit.

============================================================
26. LIMITATIONS SECTION
============================================================

Be explicit.

The current proof DOES NOT establish:

    hard-real-time execution
    exact wall-clock equivalence
    WCET guarantees
    OS scheduler guarantees
    network latency guarantees
    distributed-clock synchronization
    physical plant/model equivalence
    correctness of arbitrary planners
    correctness of the building sensor model
    mechanized proof correctness

It DOES establish the relationship between:

    formal DT view
    compiled IR
    logical semantic execution kernel

subject to the clearly stated model/compiler/runtime assumptions.

============================================================
27. TESTING
============================================================

Tests are additional validation.

They are NOT substitutes for the proof.

Create serious automated tests covering:

    compiler determinism
    malformed-model rejection
    source/IR trace correspondence
    kernel transition semantics
    invariants
    guards
    resets
    logical-time behavior
    proposition evaluation
    nondeterministic transitions if supported
    snapshot/restore
    deterministic replay
    prediction not mutating live state
    production-shell semantic isolation
    package hash checking
    ledger chain validation
    ledger tampering detection
    drone replanning
    ground-truth / twin-knowledge separation
    end-to-end simulation

Where appropriate, implement property-based/generated tests.

If practical, generate small legal automata and compare bounded source
semantics against kernel traces.

Again: call this testing/validation, NOT proof.

============================================================
28. NONDETERMINISM
============================================================

Do not silently destroy model nondeterminism.

If multiple transitions are formally enabled, represent this honestly.

For prediction:

    expose all valid successors.

For monitoring:

    retain a set of possible states if the observation is insufficient to
    distinguish them, if the underlying formalism requires this.

For execution requiring a choice:

    make the choice policy explicit and treat the policy-constrained
    execution as a refinement/policy layer rather than pretending that the
    original model was deterministic.

Document the chosen strategy.

============================================================
29. TECHNOLOGY CHOICES
============================================================

Inspect the existing repository before deciding.

Prefer a language and architecture suitable for:

    a small trusted semantic core
    deterministic behavior
    strong typing
    testability
    production deployment
    predictable serialization

For the kernel, C++. 
Do not introduce a polyglot architecture without a reason.

For the visualization, TypeScript plus a modern lightweight web stack is
acceptable.

Do not create unnecessary infrastructure.

The entire demonstration should run locally.

Provide containerization only if it helps reproducibility.

============================================================
30. API DESIGN
============================================================

Expose a clean runtime API.

Suggested conceptual endpoints:

    GET  /runtime/state

    GET  /runtime/enabled-transitions

    POST /runtime/event

    POST /runtime/advance

    POST /runtime/predict

    GET  /runtime/propositions

    GET  /runtime/ledger

    POST /runtime/ledger/verify

    GET  /runtime/package

    GET  /simulation/state

    POST /simulation/start

    POST /simulation/pause

    POST /simulation/step

    POST /simulation/reset

    GET  /planner/current-plan

The exact design may improve on this.

Use WebSocket/SSE for live visualization if appropriate.

============================================================
31. PRODUCT QUALITY
============================================================

Treat errors as first-class values.

Use structured errors.

Use deterministic serialization where hashes depend on serialization.

Do not leave TODOs in critical semantic paths.

Avoid global mutable state.

Keep semantic execution reproducible.

Use clear module boundaries.

Write useful developer documentation.

Keep the Trusted Computing Base identifiable.

Document which modules are trusted for the V_D ~ D argument.

EVERYTHING HAS TO BE MAINTAINALBLE and documented using doxygen or equivalent. THe idea is that a new developer should be able to understand the architecture and the code without having to ask questions.

The doxygen documentation should include:

    class diagrams
    sequence diagrams
    module descriptions
    function/method descriptions
    parameter explanations
    return value explanations
    exception/error handling explanations

Create:

   the doxygen documentation. 

Explain exactly:

    what must be trusted
    what does not need to be trusted
    what could fail without invalidating semantic correctness
    what would invalidate the correctness claim

============================================================
32. PROJECT README
============================================================

Produce a professional README explaining:

    problem
    architecture
    relation to SemPTDTalignmentICSE
    formal correctness story
    logical-time limitation
    how to build
    how to run tests
    how to run the proof document build
    how to start the drone demonstration
    how to verify a ledger
    repository structure
    trusted computing base
    future work

Include an architecture diagram.

============================================================
33. DEMONSTRATION STORY
============================================================

Design the default demo so a customer/researcher can understand the value
in approximately two minutes.

Example narrative:

    1. Drone receives inspection mission.

    2. DT knows only part of the building.

    3. Planner generates initial route.

    4. Kernel transitions READY -> NAVIGATING.

    5. Drone starts moving.

    6. New environment data reveals blocked corridor.

    7. Twin map changes.

    8. Runtime processes MAP_UPDATED / PATH_INVALIDATED.

    9. Formal state becomes REPLANNING.

    10. Planner asks the DT for the relevant current state and computes a
        new admissible route.

    11. Kernel processes PLAN_AVAILABLE / PLAN_ACCEPTED.

    12. Runtime transitions back to NAVIGATING.

    13. Visualization clearly shows old path becoming invalid and new path
        replacing it.

    14. Every transition is added to the tamper-evident execution ledger.

    15. The drone reaches the target.

    16. Ledger verification succeeds.

Make this scenario deterministic and reproducible.

============================================================
34. IMPORTANT ARCHITECTURAL RULE
============================================================

The Digital Twin is NOT:

    the visualization
    the planner
    the database
    the occupancy grid
    the REST server

The Digital Twin behavioral authority is:

    the verified executable behavioral model running in the semantic kernel.

Everything else either:

    feeds observations into it
    queries it
    proposes actions to it
    consumes its output

Do not blur this boundary.

============================================================
35. DELIVERABLES
============================================================

At the end I expect a RUNNING repository, not an architecture proposal.

The repository must contain:

    existing semantic aligner integration
    Twin IR
    compiler
    semantic kernel
    production runtime
    prediction support
    monitoring support
    verified package format
    tamper-evident ledger
    ledger verifier
    deterministic replay
    fake environment/map API
    Physical Twin drone simulator
    external path planner
    live web visualization
    example DT/PT models
    meaningful automated tests
    documentation
    complete professional LaTeX proof document

The default use case must execute end-to-end.

============================================================
36. EXECUTION STRATEGY
============================================================

Work incrementally but continue until the end-to-end system works.

Start by understanding SemPTDTalignmentICSE.

Then establish the formal semantic boundary.

Then define the IR.

Then write the first version of the proof definitions.

Then implement the compiler.

Then implement K_sem.

Then complete the compiler/kernel correctness arguments while the
architecture is still easy to change.

Then build packaging and the ledger.

Then build monitoring/prediction APIs.

Then implement the simulator, fake environment API and planner.

Then implement the visualization.

Then integrate the complete example with semantic alignment.

Then harden tests, docs and proof.

Do not postpone the proof until the end.

============================================================
37. FINAL VALIDATION
============================================================

Before considering the task complete, demonstrate all of the following:

    semantic aligner successfully checks the chosen PT/DT example

    DT source model compiles deterministically

    package validates

    runtime loads only the verified package

    semantic kernel starts in the corresponding formal initial state

    logical transitions execute through the kernel

    prediction operates on cloned semantic state

    drone simulation follows a generated route

    map knowledge is genuinely incomplete initially

    new environment information arrives through the fake external API

    the initial route is invalidated

    planner generates a new route

    behavioral model enters and exits replanning state

    drone executes the updated route

    live UI displays this change

    execution ledger records it

    ledger verification passes

    intentionally modifying a ledger record causes verification to fail

    deterministic replay reproduces the semantic execution

    automated tests pass

    LaTeX proof document builds

    README instructions reproduce the demo from a clean checkout

============================================================
38. DO NOT FAKE RESULTS
============================================================

No hard-coded:

    "VERIFIED"
    "ALIGNED"
    "LEDGER VALID"
    "BISIMILAR"

UI labels unless they are backed by actual system state/checks.

Do not claim that running tests proves bisimulation.

Do not claim that the handwritten LaTeX proof is mechanized.

Do not claim hard-real-time guarantees.

Do not call a normal mutable logfile immutable.

Do not bypass the kernel for convenience.

Do not let the planner or frontend implement a shadow copy of the state
machine.

If a desired theorem cannot actually be established under the implemented
semantics, STOP, identify the discrepancy, modify the architecture if
possible, and document the mathematically correct result.

The mathematical claim drives the architecture, not the other way around.

============================================================
39. CORE PRODUCT PRINCIPLE
============================================================

The final product should make this statement defensible:

    "A verified DT view is compiled into a canonical executable
     representation and executed by a small trusted semantic kernel whose
     logical behavior realizes the verified view by construction. Runtime
     decisions and observations are recorded in a cryptographically
     tamper-evident execution ledger, while planners and industrial
     integrations remain outside the trusted semantic core."

Build that system.