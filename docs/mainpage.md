# Verified Twin Studio — C++ engine {#mainpage}

This is the API reference of the C++ code: the formal toolchain, the semantic kernel, the
production runtime, the tamper-evident ledger, the Physical Twin simulators and the Studio
platform backend. For the system-level view start with
[Architecture](architecture.md); the correctness argument is in `proof/` (`make proof`).

**Guarantee (logical time):** `V_D ≅ IR(V_D) ≅ K_sem(IR(V_D)) ~weak K_prod(IR(V_D))` — the
verified DT view, its compiled IR, the semantic kernel executing it and the production runtime
are related by isomorphism and weak bisimulation. See [Trusted computing base](trusted-computing-base.md)
for what must be correct for this to hold.

## Modules

| Group | Namespace | Role | Trusted? |
|---|---|---|---|
| @ref core | `twin` | errors, results, logical time, SHA-256 | yes |
| @ref kernel | `twin::kernel` | the semantic kernel K_sem | **yes** |
| @ref ledger | `twin::ledger` | hash-chained execution ledger, verification, replay | yes (encoding) |
| @ref runtime | `twin::runtime` | session (TCB) and the production shell (untrusted) | session only |
| @ref compiler | `twin::compiler` | V_D → canonical Twin IR, translation-validated | no (validated) |
| @ref planner | `twin::planner` | A* route proposals | no |
| @ref world | `twin::world` | drone Physical Twin, building-information service | no (physical side) |
| @ref ptfeed | `twin::ptfeed` | scripted Physical Twin feeds | no |
| @ref studio | `twin::studio` | Verified Twin Studio application layer and HTTP API | no |

## One observation, end to end

@msc
  hscale="1.6";
  PT [label="Physical Twin"], Adapter [label="PtAdapter (E)"], Session [label="TwinSession"],
  Kernel [label="kernel"], Ledger [label="LedgerWriter"], Hub [label="EventHub / SSE"];
  PT -> Adapter [label="poi_arrived! @ t"];
  Adapter -> Session [label="submit(target_reached! @ t)"];
  Session -> Kernel [label="evaluate_input(state, input)  (pure)"];
  Kernel -> Session [label="successor or refusal"];
  Session -> Ledger [label="append(step|reject)  write-ahead, fsync"];
  Ledger -> Session [label="receipt(seq, hash)"];
  Session -> Session [label="commit kernel-computed state"];
  Session -> Hub [label="ledger event (seq, hash, from, to)"];
@endmsc

## A planning episode (decisions are proposals)

@msc
  hscale="1.6";
  Ctl [label="MissionController"], Planner [label="AStarPlanner"], Val [label="validate_route"],
  Session [label="TwinSession"], Kernel [label="kernel"], Ledger [label="ledger"];
  Ctl -> Planner [label="plan(known map) x profiles"];
  Planner -> Ctl [label="candidates A, B, C"];
  Ctl -> Val [label="geometric feasibility (known map)"];
  Ctl -> Session [label="simulate(schedule) on a copy"];
  Session -> Kernel [label="simulate"];
  Kernel -> Ctl [label="admissible / refused (step k)"];
  Ctl -> Session [label="record_context(planning episode)"];
  Session -> Ledger [label="context record"];
  Ctl -> Session [label="submit(plan_accepted!)"];
  Session -> Kernel [label="the kernel decides"];
@endmsc

## Runtime hosts

@dot
digraph hosts {
  rankdir=BT; node [shape=box, fontname="Helvetica", fontsize=10];
  RuntimeHost [label="RuntimeHost\n(interface)"];
  CoSimDriver [label="CoSimDriver\nco-simulation master"];
  MonitorHost [label="MonitorHost\nexternal PT pushes events"];
  TwinSession [label="TwinSession\nsingle mutation path", style=filled, fillcolor="#e5f3eb"];
  Kernel [label="kernel::*\npure functions", style=filled, fillcolor="#e5f3eb"];
  CoSimDriver -> RuntimeHost [arrowhead=empty];
  MonitorHost -> RuntimeHost [arrowhead=empty];
  CoSimDriver -> TwinSession [label="submit", style=dashed];
  MonitorHost -> TwinSession [label="submit", style=dashed];
  TwinSession -> Kernel [label="evaluate_input", style=dashed];
}
@enddot
