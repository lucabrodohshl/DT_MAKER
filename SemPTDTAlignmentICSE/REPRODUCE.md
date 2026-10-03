# Reproduction Guide

Step-by-step instructions to reproduce the paper's results on a clean Ubuntu
x86_64 machine. Run every command from the repository root unless told otherwise.
Total time is roughly 20–35 minutes, most of it the one-time dependency build.

## 0. Prerequisites

A Linux x86_64 machine with `sudo` access and internet access (to fetch the
UPPAAL libraries and the nlohmann/json header). Everything else is installed by
the steps below.

## 1. Install dependencies and build the third-party libraries

```bash
./setup.sh
```

This installs the apt packages (`build-essential cmake git libz3-dev
libomp-dev libtbb-dev`) and clones + builds `UDBM/` and `utap/` locally. If the
apt packages are already present, use `./setup.sh --no-apt`.

Verify it produced the two static libraries:

```bash
ls UDBM/build-x86_64-linux-release/src/libUDBM.a
ls utap/build-x86_64-linux-release/src/libUTAP.a
```

## 2. Build the library and benchmarks

```bash
make release
```

Verify the ten benchmark binaries exist:

```bash
ls release/benchmarks/      # run_CS1 .. run_CS8, run_use_case, run_all_semantic
```

## 3. Set up the Python environment

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
```

## 4. RQ3 & RQ4 — Table III and Figure 4

```bash
for n in 1 2 3 4 5 6 7 8; do ./release/benchmarks/run_CS$n; done
python3 scripts/plot_benchmarks.py
```

Check the outputs:

```bash
ls results/cs_results/      # CS1_results.csv .. CS8_results.csv
ls assets/time_vs_statespace.pdf assets/memory_vs_statespace.pdf assets/time_memory_combined.pdf
```

The `smt_calls` column in each CSV gives the Table III "SMT" counts; each runner
also prints the PT/DT zone counts (Table III |A_P|, |A_D|) to stdout. The three
PDFs are Figure 4.

## 5. RQ1 & RQ2 — drone study (Table I, Table II, Figures 2 and 3)

```bash
cd simulation
python3 main.py
python3 create_table_sweep.py
cd ..
```

Check the outputs:

```bash
ls simulation/results/sweep/table1_summary.tex          # Table I
ls simulation/results/table2.tex                        # Table II
ls simulation/results/canonical_7643/fig1_canonical.pdf # Figure 2 (Mission 1)
ls simulation/results/canonical_46763/fig1_canonical.pdf# Figure 2 (Mission 2)
ls simulation/results/sweep/fig_battery_gap_prob_comparison.pdf  # Figure 3
```

The simulation is deterministic (fixed seeds in `simulation/config.py`); a second
run reproduces identical numbers.

## 6. Formal validation of the running example (optional)

```bash
./release/benchmarks/run_use_case      # exit code 0 == all Section VII checks pass
echo $?
```

## Summary of generated artifacts

| Location | Paper artifact |
|----------|----------------|
| `results/cs_results/CS{1..8}_results.csv` (+ runner stdout) | Table III |
| `assets/time_vs_statespace.pdf`, `assets/memory_vs_statespace.pdf`, `assets/time_memory_combined.pdf` | Figure 4 |
| `simulation/results/sweep/table1_summary.tex` | Table I |
| `simulation/results/table2.tex` | Table II |
| `simulation/results/canonical_*/fig1_canonical.pdf` | Figure 2 |
| `simulation/results/sweep/fig_battery_gap_prob_comparison.pdf`, `fig_battery_gap_line_plot.pdf` | Figure 3 |

If a step fails, see the Troubleshooting section in `README.md`.
