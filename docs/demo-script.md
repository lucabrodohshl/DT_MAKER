# Demo script: Verified Twin Studio (3–5 minutes)

A presenter's script for someone who did not build the system. **Bold** marks what to click;
quoted text is what to say. All data is real: the drone runs in a simulator, but its twin,
planner, ledger and evidence are the production components.

## Before you start (1 minute, off-screen)

```bash
make demo-fresh        # or: make demo   (keeps earlier executions)
```

Open http://127.0.0.1:8080 in a browser window about 1600 px wide. The drone mission is paused
and not yet started; the pump twin is already running. Keep a second tab open on
**Assets → Inspection fleet → Drone-01 → Mission map**.

If something looks stale, reload the page: every screen refetches the authoritative state.

---

### 1. The asset estate (20 s)

**Overview.**
> "This is Verified Twin Studio. The estate holds 35 assets and two digital twins: an inspection
> drone and an industrial pump. Each twin's mode, alignment and package integrity come from
> evidence and from the twin's own verified runtime, not from the UI."

**Knowledge graph**, focused on **Drone-01**.
> "Drone-01 belongs to the inspection fleet. Its sensors feed the building-information service,
> and mission INS-042 is assigned to it to inspect the main switchboard and a sprinkler valve on
> floor 1."

### 2. Open Drone-01 (15 s)

**Asset explorer → Inspection fleet → Drone-01.**
> "Here is the drone and its twin. The mode badge at the top right is the semantic state of the
> twin, as computed by the kernel."

### 3. Telemetry and semantic state (20 s)

**Telemetry** tab (pick Battery), then **Behaviour → Current state**.
> "Telemetry is stored with three distinct times: when it was observed, when it was ingested, and
> its logical time. The behavioural state, its clocks and the admissible next actions come from
> the semantic kernel."

### 4. Start the mission (10 s)

**Mission map → Start mission** (speed 2×).
> "Left is the physical world, the simulator's ground truth. The twin never reads it. Right is
> what the twin knows: the facility plan, plus whatever it has observed. The plan is outdated.
> It shows fire door FD-2 open, and it doesn't know the refurbished office, which is hatched."

### 5–6. Initial route, execution (15 s)

> "The planner proposed a route through the corridor and FD-2, because that is what the twin
> believes. The flight controller is now following it. The green line is the flown trajectory."

### 7–8. Discovery: from observation to semantic meaning (30 s)

When FD-2 turns red on the right-hand map (about 15 s of mission time), press **Pause**.
> "The lidar has just seen that FD-2 is closed. The environment API published that observation,
> and the twin's map changed. The red-outlined cells are the discovery. That is an observation.
> Its *meaning* is that the current route now crosses a closed door, so the twin proposes
> `path_invalidated!`. The kernel checks that this event is admissible in the current state and
> accepts it."

### 9. NAVIGATING → REPLANNING (15 s)

Point to the mode badge and the **Mission timeline**.
> "The twin is now in REPLANNING. The timeline shows the transition, linked to its ledger record.
> In this state the model requires an admissible plan within five seconds."

### 10–11. Planner candidates, selected route (30 s)

**Step 0.1 s** until the **Planner candidates** table shows episode 2, then point at its columns.
> "The planner is untrusted. It proposed three candidates. Each one is checked twice:
> *geometrically*, by an independent validator against the twin's known map, and
> *behaviourally*, by the kernel, which simulates the plan's event schedule on a copy of the
> state. Candidate C has the lowest common objective among those that pass both checks, so it is
> selected. The selection is written to the ledger before the decision is proposed."

### 12. The drone executes the new plan (15 s)

**Resume.**
> "`plan_accepted!` takes the twin back to NAVIGATING, and the drone flies the new route through
> the storage and server rooms. Later, a facility notice declares a no-fly zone in the server
> room, which forces a second replan, this time through the office the twin is still mapping."

Let it run to completion (about 45 s at 2×), or move on and come back.

### 13. The behavioural model (20 s)

**Behaviour → Behavioural graph.**
> "This is the verified model the kernel executes. The current location is highlighted, the
> enabled transitions are green, and the edge just taken is marked. Click a transition to see its
> guard, resets, ontology meaning and every ledger occurrence."

### 14–15. Ledger and verification (25 s)

**History & audit → the execution →** select a `path_invalidated!` record, then **Verify chain
now**.
> "Every semantic step is a hash-chained ledger record: the input, the transition, the guard
> values, and the state before and after. Verification recomputes the chain on the server: ledger
> valid. The tamper drill alters one field in a copy and shows the first invalid record. The real
> ledger is never touched."

### 16. Replay the incident (25 s)

**Replay**, then **Jump to event** → the first `path_invalidated!`.
> "Replay re-executes the recorded inputs with a fresh kernel and the exact package recorded in
> the ledger, never the current one. It is identical to the recorded execution. The map shows
> what the twin knew at that moment: the discovered door, the invalidated route, the candidates."

### 17–18. Engineering and assurance (30 s)

**Engineering → Assurance overview**, then the drone twin's **package**.
> "This is exactly what is executing: the PT view of the flight controller, the DT view of the
> mission supervisor, the domain ontology, both interpretations, the alignment evidence from
> SemPTDTAlignmentICSE, the compiled Twin IR, and the package with its hashes. These are the same
> hashes the runtime recorded in every ledger record."

Optionally, open **Ontologies → indoor-drone-domain** to show a versioned, published (and
therefore immutable) ontology.

### 19. Generality: the pump (20 s)

**Asset explorer → Line 4 → Pump P-101.**
> "The same screens serve an industrial pump, without any pump-specific UI code: telemetry from
> its control system, a behavioural model whose state follows the PLC's events, semantic facts,
> prediction, and its own ledger. The drone was just the most visual example."

---

## Recovery tips

- **The mission already ran.** Press **Reset (new session)** on the Mission map. Earlier
  executions stay in the ledger list and remain replayable.
- **No runtime connected.** `make demo` was not started, or a port was busy. Check
  `var/demo/logs/`.
- **To show the exact moments without timing pressure**, keep the mission paused and use
  **Step 0.1 s**. The discovery happens at about t = 15.5 s, and the candidates appear at
  t = 16.8 s.
