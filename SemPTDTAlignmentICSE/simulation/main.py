"""
main.py — Entry point for the ICSE case study simulation.

Runs:
  1. Canonical traces (one representative run per scenario)
  2. Statistical sweep across failure probabilities
  3. Generates all publication figures and summary table
"""

import numpy as np
import os
import pandas as pd

import config as config
import energy as energy
from sim_core import run_mission, run_sweep
import figures as figures


OUT = config.OUTPUT_FOLDER


def run_canonical(canonical_seed, motor_cell):
    """Run one representative mission per scenario and return traces + curves."""
    print(f'\n[1/3] Canonical traces  '
          f'(motor failure at cell {motor_cell})...')

    rng = lambda offset: np.random.default_rng(canonical_seed + offset)
    
    trace_a, viol_a = run_mission('A', motor_cell, rng(0))

    trace_b, viol_b = run_mission('B', motor_cell, rng(0))
    trace_c, viol_c = run_mission('C', motor_cell, rng(0))

    print(f'  Sc A violation: {viol_a}')
    print(f'  Sc B violation: {viol_b}')
    print(f'  Sc C violation: {viol_c}')

    # Key reportable numbers
    dep_t   = next((cp.sim_time for cp in trace_a if cp.depleted), None)
    mon_b_t = next((cp.sim_time for cp in trace_b if cp.monitor_b), None)
    abort_t = next((cp.sim_time for cp in trace_c
                    if cp.dt_decision == 'abort'), None)
    cells_a = sum(1 for cp in trace_a
                  if cp.dt_decision in ('resume', 'resume_fast')
                  and not cp.depleted)
    cells_b = sum(1 for cp in trace_b
                  if cp.dt_decision in ('resume', 'resume_fast'))
    cells_c = sum(1 for cp in trace_c
                  if cp.dt_decision in ('resume', 'resume_fast'))

    print(f'\n  Key numbers:')
    if dep_t:
        print(f'    Sc A depletion:         t = {dep_t:.0f} s')
    if mon_b_t:
        print(f'    Sc B monitor fires:     t = {mon_b_t:.0f} s')
    if abort_t and dep_t:
        print(f'    Sc C proactive abort:   t = {abort_t:.0f} s')
        print(f'    Prediction horizon:     {dep_t - abort_t:.0f} s')
    print(f'    Cells: A={cells_a}  B={cells_b}  C={cells_c} / {config.NUM_HEC}')

    # Continuous traces for plotting
    print('\n  Building continuous battery curves...')
    ct_a, cb_a = energy.build_continuous_trace(trace_a, np.random.default_rng(canonical_seed + 10))
    ct_b, cb_b = energy.build_continuous_trace(trace_b, np.random.default_rng(canonical_seed + 11))
    ct_c, cb_c = energy.build_continuous_trace(trace_c, np.random.default_rng(canonical_seed + 12))

    return (trace_a, trace_b, trace_c,
            ct_a, cb_a, ct_b, cb_b, ct_c, cb_c)


def run_statistical_sweep():
    """Run sweep across all failure probabilities. Returns list of result dicts."""
    print(f'\n[2/3] Statistical sweep  '
          f'({config.N_SWEEP} runs × {len(config.FAILURES_PROB)} probabilities)...')

    all_results = []

    for prob in config.FAILURES_PROB:
        res = run_sweep(config.N_SWEEP, seed=200, fail_prob=prob, capture_traces=True)
        n   = config.N_SWEEP

        viol_a    = sum(r['violated']  for r in res['A']) / n * 100
        viol_b    = sum(r['violated']  for r in res['B']) / n * 100
        viol_c    = sum(r['violated']  for r in res['C']) / n * 100
        abort_b   = sum(r['mid_abort'] for r in res['B']) / n * 100
        abort_c   = sum(r['mid_abort'] for r in res['C']) / n * 100
        cells_a   = float(np.mean([r['cells_done'] for r in res['A']]))
        cells_b   = float(np.mean([r['cells_done'] for r in res['B']]))
        cells_c   = float(np.mean([r['cells_done'] for r in res['C']]))
        compl_a   = sum(1 for r in res['A']
                        if r['cells_done'] == config.NUM_HEC
                        and not r['violated']) / n * 100
        compl_b   = sum(1 for r in res['B']
                        if r['cells_done'] == config.NUM_HEC
                        and not r['violated']) / n * 100
        compl_c   = sum(1 for r in res['C']
                        if r['cells_done'] == config.NUM_HEC
                        and not r['violated']) / n * 100


        print(f'  prob={prob:.0%}:  '
              f'viol A={viol_a:.1f}%  B={viol_b:.1f}%  C={viol_c:.1f}%  |  '
              f'mid_abort B={abort_b:.1f}%  |  '
              f'cells A={cells_a:.2f}  B={cells_b:.2f}  C={cells_c:.2f}')

        all_results.append({
            'prob':        prob,
            'viol_A':      viol_a,
            'viol_B':      viol_b,
            'viol_C':      viol_c,
            'mid_abort_B': abort_b,
            'mid_abort_C': abort_c,
            'cells_A':     cells_a,
            'cells_B':     cells_b,
            'cells_C':     cells_c,
            'complete_A':  compl_a,
            'complete_B':  compl_b,
            'complete_C':  compl_c,
            'raw_A':       res['A'],
            'raw_B':       res['B'],
            'raw_C':       res['C'],

        })

    return all_results


def save_csv(sweep_results, path: str):
    """Save numerical sweep results to CSV (excludes raw traces)."""
    rows = [{k: v for k, v in r.items()
             if k not in ('raw_A', 'raw_B', 'raw_C')}
            for r in sweep_results]
    pd.DataFrame(rows).to_csv(path, index=False)
    print(f'  [CSV] {path}')


def main():
    print('=' * 62)
    print('Smart Farming DT Simulation — ICSE Case Study')
    print('Three-scenario comparison: A / B / C')
    print('=' * 62)

    os.makedirs(OUT, exist_ok=True)
    
    
    for canonical_seed, motor_cell in zip(config.CANONICAL_SEEDS, config.MOTOR_FAILURE_CELLS):
        folder = OUT + f'canonical_{canonical_seed}/'
        os.makedirs(folder, exist_ok=True)
        # ── 1. Canonical ─────────────────────────────────────────────────────────
        (trace_a, trace_b, trace_c,
        ct_a, cb_a, ct_b, cb_b, ct_c, cb_c) = run_canonical(canonical_seed, motor_cell)

        figures.figure1_canonical(
            trace_a, trace_b, trace_c,
            ct_a, cb_a, ct_b, cb_b, ct_c, cb_c,
            folder + 'fig1_canonical.pdf'
        )

        figures.figure1b_cell_axis(
            trace_a, trace_b, trace_c,
            folder + 'fig1b_cell_axis.pdf'
        )
        figures.figure1b_cell_axis_old(
            trace_a, trace_b, trace_c, motor_cell,
            folder + 'fig1b_cell_axis_old.pdf'
        )

        figures.figure4_monitor_a(
            trace_a, trace_b, trace_c, 
            folder + 'fig4_monitor_a.pdf'
        )

        figures.figure5_monitor_a_comparison(
            trace_a, trace_c, 
            folder + 'fig5_monitor_a_comparison.pdf'
        )

    if not config.NO_SWEEP:
        # ── 2. Sweep ──────────────────────────────────────────────────────────────
        sweep_results = run_statistical_sweep()

        # ── 3. Figures ────────────────────────────────────────────────────────────
        print('\n[3/3] Generating figures...')

        folder = OUT + 'sweep/'
        os.makedirs(folder, exist_ok=True)
        
        figures.figure2_outcomes(
            sweep_results,
            folder + 'fig2_outcomes.pdf'
        )

        figures.figure3_horizon(
            sweep_results,
            folder + 'fig3_horizon.pdf'
        )

        figures.table1_summary(
            sweep_results,
            folder + 'table1_summary.pdf'
        )

        figures.table1_latex(
            sweep_results,
            folder + 'table1_summary.tex'
        )

        figures.figure2_mission_completion(
            sweep_results,
            folder + 'fig2_mission_completion.pdf'
        )

        # ── Battery Gap Analysis ──────────────────────────────────────────
        print('  Analyzing per-cell-boundary battery gaps...')
        gap_data = figures.extract_cell_boundary_gaps(sweep_results)

        figures.figure_battery_gap_violin_by_cell_prob(
            gap_data,
            folder + 'fig_battery_gap_cell_prob.pdf'
        )

        figures.figure_battery_gap_violin_pooled(
            gap_data,
            folder + 'fig_battery_gap_pooled.pdf'
        )

        figures.figure_battery_gap_prob_comparison(
            gap_data,
            folder + 'fig_battery_gap_prob_comparison.pdf'
        )

        figures.figure_battery_gap_line_plot(
            gap_data,
            folder + 'fig_battery_gap_line_plot.pdf'
        )

        figures.figure_battery_gap_boxplot(
            gap_data,
            folder + 'fig_battery_gap_boxplot.pdf'
        )

        figures.figure_battery_gap_relative_improvement(
            gap_data,
            folder + 'fig_battery_gap_relative_improvement.pdf'
        )

        figures.figure_battery_gap_cdf(
            gap_data,
            folder + 'fig_battery_gap_cdf.pdf'
        )

        figures.table_battery_gap_summary(
            gap_data,
            folder + 'table_battery_gap_summary.csv'
        )

        save_csv(sweep_results, OUT + 'sweep_results.csv')

    print('\n' + '=' * 62)
    print('Outputs in results/:')
    print('  fig1_canonical.pdf   — canonical three-scenario trace')
    print('  fig2_outcomes.pdf    — violation & coverage vs failure rate')
    print('  fig3_horizon.pdf     — prediction horizon distribution')
    print('  fig1b_cell_axis.pdf  — battery per cell (speed boost visible)')
    print('  fig4_monitor_a.pdf   — Monitor A fidelity comparison')
    print('  fig5_monitor_a_comparison.pdf — aligned vs non-aligned Monitor A')
    print('  table1_summary.pdf   — aggregated outcome table (visual)')
    print('  table1_summary.tex   — aggregated outcome table (LaTeX)')
    print('  fig_battery_gap_cell_prob.pdf   — gap by cell & probability')
    print('  fig_battery_gap_pooled.pdf      — pooled gap distribution')
    print('  fig_battery_gap_prob_comparison.pdf — violin plot: gap vs stress')
    print('  fig_battery_gap_line_plot.pdf   — line plot: gap trend across stress')
    print('  fig_battery_gap_boxplot.pdf     — boxplot: gap quartiles per stress level')
    print('  fig_battery_gap_relative_improvement.pdf — % improvement of DT3 over DT1')
    print('  fig_battery_gap_cdf.pdf         — cumulative distributions by probability')
    print('  table_battery_gap_summary.csv   — gap summary statistics')
    print('  sweep_results.csv    — full numerical results')
    print('=' * 62)


if __name__ == '__main__':
    main()
