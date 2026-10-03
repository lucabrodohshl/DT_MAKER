# Semantically Aligned Digital Twins — Artifact

This artifact accompanies the paper *"A Formal Software Engineering Methodology
for Semantically Aligned Digital Twins."* It implements the semantic alignment
check (Algorithm 1) between Physical-Twin (PT) and Digital-Twin (DT) timed-automata
views via weak timed bisimulation over a shared ontology, decided with the Z3 SMT
solver, and it reproduces the paper's empirical results.

The framework is provided as a C++17 library (`dtpta`, with the semantic-alignment
layer in `src/semalign/`) plus the case-study benchmarks, and a self-contained
Python simulation for the drone use case.

## What this reproduces

| Paper item | Produced by |
|------------|-------------|
| Table III (benchmark overview: zones, SMT calls) and Figure 4 (time/memory) | C++ benchmarks `run_CS1`..`run_CS8` + `scripts/plot_benchmarks.py` |
| Examples 1–4 / Section VII formal alignment of the drone running example | C++ benchmark `run_use_case` |
| Table I, Table II, Figure 2, Figure 3 (30,000-mission drone study) | Python package `simulation/` |

The eight benchmarks `run_CS1`..`run_CS8` correspond one-to-one to rows CS1–CS8 of
Table III (see the mapping table below).

## Directory structure

```
.
├── CMakeLists.txt, Makefile        # build (C++ library + benchmarks)
├── setup.sh                        # install deps + build UDBM/UTAP
├── requirements.txt                # Python deps (figures/tables)
├── LICENSE, THIRD_PARTY.md         # licensing and attributions
├── include/dtpta/ , include/dtpta.h# library headers
├── src/                            # library sources
│   ├── core.cpp, system.cpp, timedautomaton.cpp, context.cpp
│   ├── benchmarks/                 # benchmark helpers
│   └── semalign/                   # Algorithm 1 (ontology, interpretation, SMT checker)
├── benchmark/                      # one runner per case study
│   ├── run_CS1.cpp .. run_CS8.cpp  # RQ3 & RQ4 (Table III / Figure 4)
│   ├── run_use_case.cpp            # RQ1 & RQ2 formal validation (drone running example)
│   └── run_all_semantic.cpp        # generic PT/DT/ontology batch runner
├── assets/                         # case-study inputs (PT/DT automata + ontologies)
│   ├── CS1_ThreeTank .. CS7_Autopilot, CS8_Irrigation
│   └── UseCase_Drone
├── scripts/plot_benchmarks.py      # Figure 4 (time/memory vs state space)
├── simulation/                     # RQ1 & RQ2 drone mission simulation (Python)
│   ├── main.py, sim_core.py, config.py, energy.py, monitors.py, figures.py
│   └── create_table_sweep.py       # Table II generator
├── UDBM/ , utap/                   # third-party libs (fetched by setup.sh; not tracked)
└── results/ , simulation/results/  # generated outputs (not tracked)
```

Each `assets/CS*/` directory holds the PT automaton (`V1_PT.xml`), the DT
automaton (`V2_DT.xml` and fault/drift variants), the domain ontology
(`domain.ont`, `domain_v2.ont`) and the PT/DT interpretations (`*.interp`).

## System requirements

- Linux x86_64 (developed and tested on Ubuntu 22.04).
- GCC 11+ with C++17, CMake ≥ 3.16, `make`, `git`.
- Z3 (`libz3-dev`), OpenMP (`libomp-dev`), TBB (`libtbb-dev`).
- Python ≥ 3.8 with `numpy`, `matplotlib`, `pandas`.
- ~2 GB free disk for the third-party library builds.

The paper's measurements were taken on Ubuntu with 8 vCPUs (AMD EPYC 7452) and
125 GiB RAM. Alignment verdicts, zone counts and SMT-call counts are
deterministic and machine-independent; only wall-clock time and peak memory vary.

## Installation

Run from the repository root:

```bash
# 1. Install apt packages and fetch+build the UPPAAL libraries (UDBM, UTAP).
./setup.sh                       # use ./setup.sh --no-apt if packages already installed

# 2. Build the library and the benchmarks (binaries land in release/benchmarks/).
make release

# 3. Python environment for the figures/tables.
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
```

`setup.sh` clones `UDBM/` and `utap/` from the UPPAAL GitHub organization and
builds them locally; they are not redistributed with this artifact.

## Reproducing the results

All commands are run from the repository root. The benchmarks resolve their
inputs from `assets/` and write CSVs to `results/cs_results/` relative to the
current directory.

### RQ3 & RQ4 — Table III and Figure 4

```bash
# Run the eight case-study alignment checks (each writes results/cs_results/CSn_results.csv).
for n in 1 2 3 4 5 6 7 8; do ./release/benchmarks/run_CS$n; done

# Generate Figure 4 (writes the three PDFs into assets/).
python3 scripts/plot_benchmarks.py
```

Each `run_CSn` prints the PT/DT zone counts (Table III columns |A_P|, |A_D|) to
stdout and writes per-variant rows — including `smt_calls`, `final_pairs`,
`time_ms`, `peak_memory_kb` (Table III "SMT" column and Figure 4 axes) — to the CSV.

### RQ1 & RQ2 — drone use case (Table I, Table II, Figures 2 and 3)

```bash
cd simulation
python3 main.py                  # 30,000 missions/scenario -> Table I, Figures 2 & 3
python3 create_table_sweep.py    # -> Table II (results/table2.tex)
cd ..
```

The simulation is deterministic: seeds are fixed in `simulation/config.py`
(`CANONICAL_SEEDS=[7643, 46763]`, sweep `seed=200`) and re-running yields
identical numbers.

### Formal validation of the running example

```bash
./release/benchmarks/run_use_case
```

This reproduces the Section VII / Examples 1–4 SMT entailment checks for the
drone PT/DT pair (`assets/UseCase_Drone/`); it exits non-zero if any biconditional
fails.

## Expected runtime

| Step | Approximate time |
|------|------------------|
| `setup.sh` (clone + build UDBM/UTAP) | 5–15 min (one-time) |
| `make release` | 2–5 min |
| `run_CS1`..`run_CS8` (all eight) | < 1 min total |
| `scripts/plot_benchmarks.py` | a few seconds |
| `simulation/main.py` (30,000 missions × 3 scenarios) | 3–10 min |
| `simulation/create_table_sweep.py` | a few seconds |

## Expected outputs and mapping to the paper

| Paper artifact | Output file(s) |
|----------------|----------------|
| Table III (SMT calls, sizes) | `results/cs_results/CS{1..8}_results.csv` + each runner's stdout zone counts |
| Figure 4 (Total time / memory vs state space) | `assets/time_vs_statespace.pdf`, `assets/memory_vs_statespace.pdf`, `assets/time_memory_combined.pdf` |
| Table I (scenario outcomes; Stage 1/2/3 = scenario A/B/C) | `simulation/results/sweep/table1_summary.tex` |
| Table II (gap to PT, Wh per cell) | `simulation/results/table2.tex` |
| Figure 2 (battery level, Mission 1 & 2) | `simulation/results/canonical_7643/fig1_canonical.pdf`, `simulation/results/canonical_46763/fig1_canonical.pdf` (and `fig1b_cell_axis.pdf` per folder) |
| Figure 3 (PT–DT gap over all simulations) | `simulation/results/sweep/fig_battery_gap_prob_comparison.pdf`, `simulation/results/sweep/fig_battery_gap_line_plot.pdf` |

The simulation also emits additional supplementary figures and a
`sweep_results.csv` with the full numerical results.

### Repo case study → paper Table III

| Runner / assets | Paper CS | Domain | Standard |
|-----------------|----------|--------|----------|
| `run_CS1` / `CS1_ThreeTank` | CS1 | Three-tank liquid process control | IEC 61511-1 |
| `run_CS2` / `CS2_Crane` | CS2 | Overhead travelling crane | EN 13001-1 |
| `run_CS3` / `CS3_Rover` | CS3 | Autonomous inspection rover | NASA-STD-8739.8A |
| `run_CS4` / `CS4_Engine` | CS4 | Aircraft engine control | DO-178C |
| `run_CS5` / `CS5_Grasping` | CS5 | Robotic debris grasping arm | ISO 9283 |
| `run_CS6` / `CS6_Pump` | CS6 | GPCA infusion pump | IEC 60601-1 |
| `run_CS7` / `CS7_Autopilot` | CS7 | Autopilot flight safety system | DO-178C / ARP4754A |
| `run_CS8` / `CS8_Irrigation` | CS8 | Precision crop irrigation system | ISO 11783-7 / FAO-56 |
| `run_use_case` / `UseCase_Drone` | (running example) | Autonomous crop-spraying drone | — |

## Troubleshooting

- **`make release` reports "UDBM/UTAP library not built"**: run `./setup.sh`
  first; it must produce `UDBM/build-x86_64-linux-release/src/libUDBM.a` and
  `utap/build-x86_64-linux-release/src/libUTAP.a`.
- **CMake warns "Z3 not found — semalign targets will be skipped"**: install
  `libz3-dev`. The case-study benchmarks require Z3.
- **A `run_CSn` cannot find its inputs**: run it from the repository root so that
  `assets/` is in the working directory (e.g. `./release/benchmarks/run_CS1`).
- **`scripts/plot_benchmarks.py` produces an empty plot**: run all of
  `run_CS1`..`run_CS8` first so that `results/cs_results/` is populated.
- **`-march=native` build fails on a different CPU**: edit the
  `CMAKE_CXX_FLAGS_RELEASE` line in `Makefile` to drop `-march=native`.
- **Python import errors**: activate the virtualenv and reinstall with
  `pip install -r requirements.txt`.
