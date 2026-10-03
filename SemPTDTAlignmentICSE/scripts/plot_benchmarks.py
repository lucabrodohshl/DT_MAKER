#!/usr/bin/env python3
"""
Benchmark analysis plots for semantic alignment (CS1-CS8).

Three plots, all restricted to **aligned pairs only**:
  1. plot_time()          – time vs. state-space size
  2. plot_memory()        – peak memory vs. state-space size
  3. plot_time_memory()   – dual-y-axis: time (left) + memory (right)

X-axis layout (shared across all plots):
  bottom  — state-space product  |Z_P| × |Z_D|  (all variants, sorted)
  top     — case-study label

Data sources:
  Zone sizes : hardcoded in the VARIANTS table below (matches paper Table III)
  Time / Mem : results/cs_results/CS*_results.csv

Usage:
  python3 scripts/plot_benchmarks.py
"""

import csv
import os
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

# ── Paths ─────────────────────────────────────────────────────────────────────
_HERE        = os.path.dirname(os.path.abspath(__file__))
RESULTS_DIR  = os.path.join(_HERE, "..", "results", "cs_results")
OUTPUT_DIR   = os.path.join(_HERE, "..", "assets")

FIG_W  = 3.3
FIG_H  = 1.2
FONT_SIZE = 9

# ── Aesthetics per case study ─────────────────────────────────────────────────
CS_ORDER = ["CS1", "CS2", "CS3", "CS4", "CS5", "CS6", "CS7", "CS8"]

CS_LABEL = {
    "CS1": "$\mathbf{CS1}$",#\nThreeTank",
    "CS2": "$\mathbf{CS2}$",#\nCrane",
    "CS3": "$\mathbf{CS3}$",#\nRover",
    "CS4": "$\mathbf{CS4}$",#\nEngine",
    "CS5": "$\mathbf{CS5}$",#\nArm",
    "CS6": "$\mathbf{CS6}$",#\nInfusion",
    "CS7": "$\mathbf{CS7}$",#\nAutopilot",
    "CS8": "$\mathbf{CS8}$",#\nIrrigation",
}

COLORS = {
    "CS1": "#1f77b4",
    "CS2": "#ff7f0e",
    "CS3": "#2ca02c",
    "CS4": "#d62728",
    "CS5": "#9467bd",
    "CS6": "#8c564b",
    "CS7": "#e377c2",
    "CS8": "#7f7f7f",
    "CS9": "#17becf",
}

MARKERS = {
    "CS1": "o",
    "CS2": "s",
    "CS3": "^",
    "CS4": "D",
    "CS5": "v",
    "CS6": "P",
    "CS7": "*",
    "CS8": "X",
    "CS9": "h",
}

# ── Variant table (matches paper Table III) ────────────────────────────
# (cs_id, csv_model_name, pt_zones, dt_zones)
VARIANTS = [
    # CS1 – ThreeTank (IEC 61511-1)
    ("CS1", "CS1_aligned",           1_000,    1_347),
    ("CS1", "CS1_thresh_drift",      1_000,    1_347),
    ("CS1", "CS1_evo_v2",            1_000,    1_347),
    # CS2 – Overhead Crane (EN 13001-1)
    ("CS2", "CS2_aligned",          15_153,   35_553),
    ("CS2", "CS2_thresh_drift",     15_153,   39_152),
    ("CS2", "CS2_missing_estop",    15_153,   35_553),
    ("CS2", "CS2_evo_v2",           15_153,   35_553),
    # CS3 – Autonomous Rover (NASA-STD-8739.8A)
    ("CS3", "CS3_aligned",          10_314,    2_004),
    ("CS3", "CS3_thresh_drift",     10_314,  124_898),
    ("CS3", "CS3_timing_violation", 10_314,  124_898),
    ("CS3", "CS3_evo_v2",           10_314,    2_004),
    # CS4 – Aircraft Engine (DO-178C)
    ("CS4", "CS4_aligned",             182,      132),
    ("CS4", "CS4_egt_drift",           182,       80),
    ("CS4", "CS4_missing_flameout",    182,      132),
    ("CS4", "CS4_evo_v2",              182,      132),
    # CS5 – Robotic Arm (ISO 9283)
    ("CS5", "CS5_aligned",              43,       31),
    ("CS5", "CS5_timing_violation",     43,       60),
    ("CS5", "CS5_thresh_drift",         43,       60),
    ("CS5", "CS5_evo_v2",               43,       31),
    # CS6 – GPCA Infusion Pump (IEC 60601-1)
    ("CS6", "CS6_aligned",          12_052,    3_922),
    ("CS6", "CS6_missing_warning",  12_052,    3_922),
    ("CS6", "CS6_thresh_drift",     12_052,    3_922),
    ("CS6", "CS6_evo_v2",           12_052,    3_922),
    # CS7 – Autopilot (DO-178C / ARP4754A)
    ("CS7", "CS7_aligned",           2_576,    1_154),
    ("CS7", "CS7_thresh_drift",      2_576,    1_154),
    ("CS7", "CS7_timing_violation",  2_576,    1_462),
    ("CS7", "CS7_missing_override",  2_576,    1_154),
    # CS8 – Precision Irrigation (ISO 11783-7 / FAO-56)
    ("CS8", "CS8_aligned",         303_531,   47_055),
    ("CS8", "CS8_thresh_drift",    303_531,   47_055),
    ("CS8", "CS8_evo_v2",          303_531,   47_055),
]


# ── Data loading ──────────────────────────────────────────────────────────────

def _load_csv_rows():
    """Return {model_name: {col: val}} for all CS CSV files."""
    rows = {}
    for cs_num in range(1, 9):
        path = os.path.join(RESULTS_DIR, f"CS{cs_num}_results.csv")
        if not os.path.exists(path):
            continue
        with open(path, newline="") as f:
            for row in csv.DictReader(f):
                rows[row["model_name"]] = row
    return rows


def load_data():
    """
    Returns a list of dicts for every VARIANT that has CSV data.
    Keys: cs, model, pt_zones, dt_zones, state_space, aligned, time_s,
          memory_mb (None if column absent or zero).
    """
    csv_rows = _load_csv_rows()
    records = []
    for (cs, model, pt, dt) in VARIANTS:
        row = csv_rows.get(model)
        if row is None:
            continue
        aligned = row["aligned"].strip().lower() == "true"
        time_s  = float(row["time_ms"]) / 1000.0

        mem_mb = None
        if "peak_memory_kb" in row:
            kb = float(row["peak_memory_kb"])
            mem_mb = kb / 1024.0 if kb > 0 else None

        records.append({
    "cs":          cs,
    "model":       model,
    "pt_zones":    pt,
    "dt_zones":    dt,
    "state_space": pt * dt,
    "final_pairs": int(row.get("final_pairs", 0)),   # ADD THIS
    "aligned":     aligned,
    "time_s":      time_s,
    "memory_mb":   mem_mb,
})
    return records


# ── Shared x-axis builder ─────────────────────────────────────────────────────

def _format_ss(ss):
    """Human-readable state-space label."""
    if ss >= 1_000_000_000:
        return f"{ss / 1e9:.1f}B"
    if ss >= 1_000_000:
        return f"{ss / 1e6:.1f}M"
    if ss >= 1_000:
        return f"{ss / 1e3:.0f}K"
    return str(ss)


def _build_xaxis(records):
    """
    Sort records by state_space, assign integer x-positions.
    Returns (sorted_records, xs, bottom_labels, top_ticks, top_labels).
    """
    records = sorted(records, key=lambda r: r["final_pairs"])
    xs = list(range(len(records)))
    bottom_labels = [_format_ss(r["final_pairs"]) for r in records]

    # Top axis: one label per CS at its median x-position.
    top_ticks, top_labels = [], []
    for cs in CS_ORDER:
        idxs = [i for i, r in enumerate(records) if r["cs"] == cs]
        if not idxs:
            continue
        top_ticks.append(float(np.median(idxs)))
        top_labels.append(CS_LABEL[cs])
    print(top_labels)
    return records, xs, bottom_labels, top_ticks, top_labels


def _apply_xaxis(ax, xs, bottom_labels, top_ticks, top_labels):
    ax.set_xticks(xs)
    ax.set_xticklabels(bottom_labels, fontsize=FONT_SIZE - 2,
                       rotation=45, ha="right")
    ax.set_xlabel(r"State-space explored",
                  fontsize=FONT_SIZE, labelpad=-2)
    axtop = ax.twiny()
    axtop.set_xlim(ax.get_xlim())
    axtop.set_xticks(top_ticks)
    axtop.set_xticklabels(top_labels, fontsize=FONT_SIZE - 2, ha="center", rotation = 45)
    axtop.tick_params(axis="both", pad=-4)
    return axtop


def _save(fig, name):
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    path = os.path.join(OUTPUT_DIR, name)
    fig.savefig(path, bbox_inches="tight", dpi=300)
    plt.close(fig)
    print(f"  Saved → {path}")


# ── Plot 1: Time ──────────────────────────────────────────────────────────────

def plot_time(records):
    """Time (s) vs. state-space — aligned pairs only."""
    aligned = [r for r in records if r["aligned"]]
    if not aligned:
        print("[WARN] No aligned records for time plot.")
        return

    records_s, xs, bot_labels, top_ticks, top_labels = _build_xaxis(aligned)

    fig, ax = plt.subplots(figsize=(FIG_W, FIG_H))

    # Separate CS3 from the rest
    main_records = [(i, r) for i, r in enumerate(records_s) if r["cs"] != "CS3"]
    cs3_records  = [(i, r) for i, r in enumerate(records_s) if r["cs"] == "CS3"]

    # Single trend line through all non-CS3 points
    main_xs = [i for i, r in main_records]
    main_ys = [r["time_s"] for i, r in main_records]
    ax.plot(main_xs, main_ys,
            color="#1f77b4", linewidth=1.2, linestyle="-", zorder=2)
    ax.scatter(main_xs, main_ys,
               color="#1f77b4", s=35, zorder=3)

    # CS3 as outlier — different color, annotated
    for i, r in cs3_records:
        ax.scatter(i, r["time_s"],
                   color="#d62728", s=50, zorder=4, marker="^")
        #ax.annotate("CS3\n(final rel.=1)",
        #            xy=(i, r["time_s"]),
        #            xytext=(i + 0.4, r["time_s"] + 0.3),
        #            fontsize=FONT_SIZE - 2, color="#d62728",
        #            arrowprops=dict(arrowstyle="->", lw=0.8, color="#d62728"))
#
    _apply_xaxis(ax, xs, bot_labels, top_ticks, top_labels)
    ax.set_ylabel("Total Time (s)", fontsize=FONT_SIZE)
    all_t = [r["time_s"] for r in records_s]
    ax.set_ylim(bottom=0, top=max(all_t) * 1.2)
    ax.grid(True, alpha=0.25, linestyle="--")
    ax.set_axisbelow(True)
    ax.tick_params(axis="both", labelsize=FONT_SIZE - 1)

    plt.tight_layout()
    _save(fig, "time_vs_statespace.pdf")

# ── Plot 2: Memory ────────────────────────────────────────────────────────────

def plot_memory(records):
    """Peak memory (MB) vs. state-space — aligned pairs only."""
    aligned = [r for r in records if r["aligned"] and r["memory_mb"] is not None]
    if not aligned:
        print("[WARN] No memory data for aligned pairs. "
              "Re-run benchmarks after rebuilding to populate peak_memory_kb.")
        return

    records_s, xs, bot_labels, top_ticks, top_labels = _build_xaxis(aligned)

    fig, ax = plt.subplots(figsize=(FIG_W, FIG_H))

    main_xs = [i for i, r in enumerate(records_s) if r["cs"] != "CS3"]
    main_ys = [r["memory_mb"] for i, r in enumerate(records_s) if r["cs"] != "CS3"]

    ax.plot(main_xs, main_ys,
            color="#1f77b4", linewidth=1.2, linestyle="-", zorder=2)
    ax.scatter(main_xs, main_ys,
               color="#1f77b4", s=35, zorder=3)

    _apply_xaxis(ax, xs, bot_labels, top_ticks, top_labels)
    ax.set_ylabel("Peak Memory (MB)", fontsize=FONT_SIZE)
    all_m = [r["memory_mb"] for r in records_s]
    ax.set_ylim(bottom=0, top=max(all_m) * 1.2)
    ax.grid(True, alpha=0.25, linestyle="--")
    ax.set_axisbelow(True)
    ax.tick_params(axis="both", labelsize=FONT_SIZE - 1)

    plt.tight_layout()
    _save(fig, "memory_vs_statespace.pdf")

# ── Plot 3: Time + Memory (dual y-axis) ───────────────────────────────────────

def plot_time_memory(records):
    """
    Dual-y-axis plot (aligned pairs only).
      Left  axis (solid lines)  — Total Time (s)
      Right axis (dashed lines) — Peak Memory (MB)
    CS3 shown as outlier on time axis only.
    Both axes share [0, 1] normalised scale but display real values as tick labels.
    """
    has_mem = any(r["memory_mb"] is not None for r in records if r["aligned"])
    aligned = [r for r in records if r["aligned"]]
    if not aligned:
        print("[WARN] No aligned records for combined plot.")
        return

    if has_mem:
        plot_set = [r for r in aligned if r["memory_mb"] is not None]
    else:
        print("[WARN] No memory data — combined plot will show time only.")
        plot_set = aligned

    records_s, xs, bot_labels, top_ticks, top_labels = _build_xaxis(plot_set)

    fig, ax1 = plt.subplots(figsize=(FIG_W, FIG_H))
    ax2 = ax1.twinx()

    # ── Split CS3 from the rest ──
    main_idx = [(i, r) for i, r in enumerate(records_s) if r["cs"] != "CS3"]
    cs3_idx  = [(i, r) for i, r in enumerate(records_s) if r["cs"] == "CS3"]

    all_t = [r["time_s"] for r in records_s]
    max_t = max(all_t)
    def norm_t(v): return v / max_t

    if has_mem:
        all_m = [r["memory_mb"] for r in records_s if r["memory_mb"] is not None]
        max_m = max(all_m)
        def norm_m(v): return v / max_m

    # ── Time — blue solid line ──
    main_xs = [i for i, r in main_idx]
    main_ys = [norm_t(r["time_s"]) for i, r in main_idx]

    ax1.plot(main_xs, main_ys,
             color="#1f77b4", linewidth=1, linestyle="-", zorder=3,
             label="Time (s)")
    ax1.scatter(main_xs, main_ys,
                color="#1f77b4", s=20, zorder=4)

    # ── CS3 outlier — time only ──
    for i, r in cs3_idx:
        ax1.scatter(i, norm_t(r["time_s"]),
                    color="#d62728", s=40, zorder=5, marker="s")
        #ax1.annotate("CS3\n(rel.=1)",
        #             xy=(i, norm_t(r["time_s"])),
        #             xytext=(i + 0.4, norm_t(r["time_s"]) + 0.05),
        #             fontsize=FONT_SIZE - 2, color="#d62728",
        #             arrowprops=dict(arrowstyle="->", lw=0.8, color="#d62728"))
#
    # ── Memory — green dashed line ──
    if has_mem:
        mem_idx = [(i, r) for i, r in enumerate(records_s)
                   if r["memory_mb"] is not None and r["cs"] != "CS3"]
        mem_xs = [i for i, r in mem_idx]
        mem_ys = [norm_m(r["memory_mb"]) for i, r in mem_idx]

        ax2.plot(mem_xs, mem_ys,
                 color="#2ca02c", linewidth=1, linestyle="-", zorder=2,
                 label="Memory (MB)")
        ax2.scatter(mem_xs, mem_ys,
                    color="#2ca02c", s=20, zorder=3)

    # ── X-axis ──
    _apply_xaxis(ax1, xs, bot_labels, top_ticks, top_labels)

    # ── Y-axis left: normalised scale, real time tick labels ──
    ax1.set_ylabel("Total Time (s)", fontsize=FONT_SIZE, labelpad=0)
    ax1.set_ylim(bottom=0, top=1.25)
    # Pick ~5 evenly spaced normalised ticks and label with real values
    time_ticks_norm = [0.0, 0.25, 0.5, 0.75, 1.0]
    ax1.set_yticks(time_ticks_norm)
    ax1.set_yticklabels([f"{v * max_t:.1f}" for v in time_ticks_norm],
                        fontsize=FONT_SIZE - 1)

    # ── Y-axis right: normalised scale, real memory tick labels ──
    if has_mem:
        ax2.set_ylabel("Peak Memory (MB)", fontsize=FONT_SIZE, labelpad=0)
        ax2.set_ylim(bottom=0, top=1.25)
        mem_ticks_norm = [0.0, 0.25, 0.5, 0.75, 1.0]
        ax2.set_yticks(mem_ticks_norm)
        ax2.set_yticklabels([f"{v * max_m:.0f}" for v in mem_ticks_norm],
                            fontsize=FONT_SIZE - 1)
    else:
        ax2.set_yticks([])
    # ── Legend ──
    lines1, labels1 = ax1.get_legend_handles_labels()
    lines2, labels2 = ax2.get_legend_handles_labels()

    #add one red square for CS3 outlier
    from matplotlib.lines import Line2D
    red_square = Line2D([0], [0], marker='s', color='w', label='CS3 (Time)', markerfacecolor='#d62728', markersize=8)
    lines1.append(red_square)
    labels1.append('CS3')

    ax1.legend(lines1 + lines2, labels1 + labels2,
                       ncols=2,
        loc='upper left',
        fontsize=FONT_SIZE,
        framealpha=0.8,
        labelspacing=0.2,
        handletextpad=0.2,
        columnspacing=0.2,
        borderpad=0.2,
        borderaxespad=0.1,
        handlelength=0.8)
    ax1.tick_params(axis="both", pad=0)
    ax2.tick_params(axis="y", pad=0)
    ax1.grid(True, alpha=0.25, linestyle="--")
    ax1.set_axisbelow(True)
    ax1.tick_params(axis="x", labelsize=FONT_SIZE - 1)



    #plt.tight_layout()
    _save(fig, "time_memory_combined.pdf")
# ── Entry point ───────────────────────────────────────────────────────────────

def main():
    print("Loading benchmark data …")
    records = load_data()
    n_aligned = sum(1 for r in records if r["aligned"])
    n_mem     = sum(1 for r in records if r["aligned"] and r["memory_mb"] is not None)
    print(f"  Total variants loaded : {len(records)}")
    print(f"  Aligned pairs         : {n_aligned}")
    print(f"  With memory data      : {n_mem}")
    print()

    

    print("Generating plots …")
    plot_time(records)
    plot_memory(records)
    plot_time_memory(records)
    print("\nDone.")


if __name__ == "__main__":
    main()
