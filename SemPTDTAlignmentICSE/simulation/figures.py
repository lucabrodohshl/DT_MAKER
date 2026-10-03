"""
figures.py — Publication-quality figures for the ICSE case study.

  fig1_canonical  — three-scenario PT battery curves, canonical run
  fig2_outcomes   — sweep outcome rates and cells completed
  fig3_horizon    — prediction horizon: aligned DT vs Monitor B
  fig4_monitor_a  — Muñoz-style fidelity monitor comparison
  table1_summary  — LaTeX-style outcome summary table
"""

import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import matplotlib.lines as mlines
import matplotlib.patches as mpatches
from matplotlib.gridspec import GridSpec
from typing import List, Dict

import config as config
import energy as energy
import monitors as monitors

# ─────────────────────────────────────────────────────────────────────────────
# Style
# ─────────────────────────────────────────────────────────────────────────────

FIG_W  = 2.4
FIG_H  = 1.5
CA     = '#C62828'
CB     = '#E65100'
CC     = '#1565C0'
CMA    = '#6A1B9A'   # Monitor A — purple
CGRID  = '#E0E0E0'
CANNOT = '#546E7A'

FONT_SIZE = 8

plt.rcParams.update({
    'font.family':        'serif',
    'font.size':          FONT_SIZE,
    'axes.titlesize':     FONT_SIZE,
    'axes.labelsize':     FONT_SIZE,
    'xtick.labelsize':    FONT_SIZE,
    'ytick.labelsize':    FONT_SIZE,
    'legend.fontsize':    FONT_SIZE,
    'lines.linewidth':    1.3,
    'axes.linewidth':     0.8,
    'axes.grid':          True,
    'grid.color':         CGRID,
    'grid.linewidth':     0.5,
    'savefig.dpi':        300,
    'savefig.bbox':       'tight',
    'savefig.pad_inches': 0.05,
})


LABEL_A = "Stage 1"
LABEL_B = "Stage 2"
LABEL_C = "Stage 3"


# ─────────────────────────────────────────────────────────────────────────────
# Figure 1 — Canonical trace
# ─────────────────────────────────────────────────────────────────────────────

def figure1_canonical(trace_a, trace_b, trace_c,
                       ct_a, cb_a, ct_b, cb_b, ct_c, cb_c,
                       path: str):
    fig, ax = plt.subplots(figsize=(FIG_W, FIG_H))

    safe_thresh = config.CONS_DEGRADED + config.RET_COST
    #ax.axhspan(0, safe_thresh, alpha=0.05, color='#FFCDD2', zorder=0)
    #ax.axhline(safe_thresh, color='#EF9A9A', lw=0.8, ls='--', alpha=0.6, zorder=1)
    ##ax.text(ct_a[0] + 8, safe_thresh + 3,       r'$\phi_{\rm safe}$', fontsize=FONT_SIZE, color='#C62828')

    ax.plot(ct_a, cb_a, color=CA, lw=1.2, alpha=0.85, zorder=3,
            label=LABEL_A)
    ax.plot(ct_b, cb_b, color=CB, lw=1.2, alpha=0.85, zorder=3, ls='--',
            label=LABEL_B)
    ax.plot(ct_c, cb_c, color=CC, lw=1.5, alpha=0.90, zorder=4,
            label=LABEL_C)

    # Motor failure
    fail_t = next((cp.sim_time for cp in trace_a
                   if cp.motor_degraded and cp.dt_decision != 'observe'), None)
    if fail_t:
        if fail_t>400:
            a = -260
        else:
            a = 10
        ax.axvline(fail_t, color='#5D4037', lw=0.9, ls=':', alpha=0.75, zorder=2)
        ax.text(fail_t + a, config.TOT_BAT * 0.3,
            'battery\nstress', fontsize=FONT_SIZE, color='#5D4037', va='top')
        
        


    # Sc A depletion
    dep = next((cp for cp in trace_a if cp.depleted), None)
    if dep:
        ax.scatter([dep.sim_time], [dep.pt_bat],
                   color=CA, marker='o', s=40, zorder=8, clip_on=False)


    # Sc B monitor fires + mid-spray abort
    mon_b_cp = next((cp for cp in trace_b if cp.monitor_b), None)
    abort_b  = next((cp for cp in trace_b if 'monitor_b_abort' in cp.event), None)

    if abort_b:
        ax.scatter([abort_b.sim_time], [abort_b.pt_bat],
                   color=CB, marker='v', s=40, zorder=7)

    # Sc C speed boost start
    boost_cp = next((cp for cp in trace_c
                     if cp.speed_factor > 1.0
                     and cp.dt_decision == 'resume_fast'), None)
  

    # Sc C proactive abort
    abort_c = next((cp for cp in trace_c if cp.dt_decision == 'abort'), None)
    if abort_c:

        ax.scatter([abort_c.sim_time], [abort_c.pt_bat],
                   color=CC, marker='^', s=55, zorder=7)



    ax.set_xlabel('Simulation time (s)',labelpad=0)
    ax.set_ylabel('Battery level (Wh)',labelpad=0)
    ax.tick_params(axis="both", pad=0)
#    ax.set_title(
#        'Canonical run — same mission, same motor failure, three DT strategies',
#        pad=4, fontweight='bold')
    ax.legend(loc='upper right', ncol=1, framealpha=0.95, edgecolor=CGRID, ncols=1,
                 labelspacing=0.2,
    handletextpad=0.2,
    columnspacing=0.2,
    borderpad=0.2,
    borderaxespad=0.1,handlelength=1.2)
    ax.set_ylim(-28, config.TOT_BAT + 20)
    #ax.set_xlim(ct_a[0] - 10, max(ct_a[-1], ct_b[-1], ct_c[-1]) + 20)
    ax.tick_params(axis='both', pad=0)

    fig.savefig(path)
    plt.close(fig)
    print(f'  [Fig 1] {path}')


# ─────────────────────────────────────────────────────────────────────────────
# Figure 2 — Sweep outcomes
# ─────────────────────────────────────────────────────────────────────────────

def figure2_outcomes_ab_combined(sweep_results, path):
    probs   = [r['prob']        for r in sweep_results]
    viol_a  = np.array([r['viol_A']      for r in sweep_results])
    abort_b = np.array([r['mid_abort_B'] for r in sweep_results])

    # Normalize independently for visual comparison
    viol_norm  = viol_a  / max(viol_a)  if max(viol_a)  > 0 else viol_a
    abort_norm = abort_b / max(abort_b) if max(abort_b) > 0 else abort_b

    fig, ax1 = plt.subplots(figsize=(FIG_W, FIG_H))
    ax2 = ax1.twinx()

    xfmt = matplotlib.ticker.FuncFormatter(lambda x, _: f'{x:.0%}')

    # Left axis (normal direction)
    l1 = ax1.plot(
        probs, viol_norm,
        color=CA, lw=1.5, marker='o', ms=4,
        label='A - Safety Violations'
    )

    # Right axis (reversed direction)
    l2 = ax2.plot(
        probs, abort_norm,  # reverse for opposite y-axis direction
        color=CB, lw=1.5, marker='s', ms=4,
        label='B - Mid-Spray Aborts'
    )

    # Cosmetic tick labels show actual rates
    ax1.yaxis.set_major_formatter(
        matplotlib.ticker.FuncFormatter(
            lambda y, _: f'{y * max(viol_a):.0f}'
        )
    )

    ax2.yaxis.set_major_formatter(
        matplotlib.ticker.FuncFormatter(
            lambda y, _: f'{y * max(abort_b):.0f}'
        )
    )


    ax1.set_xlabel('Probability of Battery Stress', labelpad=0)
    ax1.set_ylabel('Violation rate (%)', labelpad=0)
    ax2.set_ylabel('Abort rate (%)', labelpad=0)

    ax1.xaxis.set_major_formatter(xfmt)
    ax1.tick_params(axis='both', pad=0)
    ax2.tick_params(axis='y', pad=0)
    ax1.set_xlim(-0.02, max(probs) + 0.02)

    lines = l1 + l2
    labels = [l.get_label() for l in lines]
    #ax1.legend(lines, labels, fontsize=FONT_SIZE, framealpha=0.9,ncols =1,
    #             labelspacing=0.2,
    #handletextpad=0.2,
    #columnspacing=0.2,
    #borderpad=0.2,
    #borderaxespad=0.1,)

    fig.tight_layout()
    fig.savefig(path, bbox_inches='tight')
    plt.close(fig)
    


def create_battery_over_cells_sweep_table(cells_a, cells_b, cells_c):
    pass




def figure2_outcomes(sweep_results: List[Dict], path: str):
    probs   = [r['prob']        for r in sweep_results]
    viol_a  = [r['viol_A']      for r in sweep_results]
    abort_b = [r['mid_abort_B'] for r in sweep_results]
    cells_a = [r['cells_A'] for r in sweep_results] 
    cells_b = [r['cells_B'] for r in sweep_results] 
    cells_c = [r['cells_C'] for r in sweep_results]

    # Normalize each series to [0,1] for visual comparability
    viol_norm  = np.array(viol_a)  / max(viol_a)  if max(viol_a)  > 0 else np.array(viol_a)
    abort_norm = np.array(abort_b) / max(abort_b) if max(abort_b) > 0 else np.array(abort_b)
    fmt = matplotlib.ticker.FuncFormatter(lambda x, _: f'{x:.0f}%')
    fmt_x = matplotlib.ticker.FuncFormatter(lambda x, _: f'{x:.0%}')

    fig, ax1 = plt.subplots(figsize=(FIG_W, FIG_H))
    ax1.plot(
        probs, viol_norm,
        color=CA, lw=1.5, marker='o', ms=2,
    )

    ax1.yaxis.set_major_formatter(
        matplotlib.ticker.FuncFormatter(
            lambda y, _: f'{y * max(viol_a):.0f}%'
        )
    )
    ax1.set_ylabel('Violation rate (%)', labelpad=0)
    ax1.tick_params(axis='both', pad=0)
    ax1.set_xlabel('Probability of Battery Stress', labelpad=0)
    fig.tight_layout()
    fig.savefig(path.replace('.pdf', '_violations.pdf'))
    plt.close(fig)
    


    fig, ax1 = plt.subplots(figsize=(FIG_W, FIG_H))
    ax1.plot(
        probs, abort_norm,
        color=CB, lw=1.5, marker='o', ms=2,
    )

    ax1.yaxis.set_major_formatter(
        matplotlib.ticker.FuncFormatter(
            lambda y, _: f'{y * max(abort_b):.0f}%'
        )
    )
    ax1.set_ylabel('Abort rate (%)', labelpad=0)
    ax1.set_xlabel('Probability of Battery Stress', labelpad=0)
    ax1.tick_params(axis='both', pad=0)
    fig.tight_layout()
    fig.savefig(path.replace('.pdf', '_aborts.pdf'))
    plt.close(fig)


    #create_battery_over_cells_sweep_table(probs, cells_a, cells_b, cells_c)

    fig, ax1 = plt.subplots(figsize=(FIG_W, FIG_H))


    ax1.plot(probs, cells_a, color=CA, lw=1.4, marker='o', ms=3, label='Stage 1')
    ax1.plot(probs, cells_b, color=CB, lw=1.4, marker='s', ms=3, ls='--', label='Stage 2')
    ax1.plot(probs, cells_c, color=CC, lw=1.6, marker='^', ms=3, label='Stage 3')
    ax1.set_xlabel('Probability of Battery Stress', labelpad=0)
    ax1.set_ylabel(f'Cells Completed', labelpad=0)
    ax1.tick_params(axis='both', pad=0)
    ax1.legend( framealpha=0.7,ncols =1,
                 labelspacing=0.2,
    handletextpad=0.2,
    columnspacing=0.2,
    borderpad=0.2,
    borderaxespad=0.1,handlelength=0.8)
    ax1.set_xlim(-0.02, max(probs) + 0.02)
    ax1.set_ylim(min(min(cells_a), min(cells_b)) - 0.3, config.NUM_HEC + 0.3)
    ax1.xaxis.set_major_formatter(fmt_x)


    fig.tight_layout()
    fig.savefig(path.replace('.pdf', '_coverage.pdf'))
    plt.close(fig)

    figure2_outcomes_ab_combined(sweep_results, path.replace('.pdf', '_combined.pdf'))


    print(f'  [Fig 2] {path}')



def figure2_mission_completion(sweep_results: List[Dict], path: str):
    probs     = [r['prob']               for r in sweep_results]
    compl_a   = [r['complete_A']         for r in sweep_results]
    compl_b   = [r['complete_B']         for r in sweep_results]
    compl_c   = [r['complete_C']         for r in sweep_results]
    fmt_x = matplotlib.ticker.FuncFormatter(lambda x, _: f'{x:.0%}')
    fmt_y = matplotlib.ticker.FuncFormatter(lambda y, _: f'{y:.0f}%')

    fig, ax1 = plt.subplots(figsize=(FIG_W, FIG_H))

    ax1.plot(probs, compl_a, color=CA, lw=1.4, marker='o', ms=4, label='Stage 1')
    ax1.plot(probs, compl_b, color=CB, lw=1.4, marker='s', ms=4, ls='--', label='Stage 2')
    ax1.plot(probs, compl_c, color=CC, lw=1.6, marker='^', ms=4, label='Stage 3 (ours)')
    ax1.set_xlabel('Probability of Battery Stress', labelpad=0)
    ax1.set_ylabel('Mission Completion Rate (%)', labelpad=0)
    ax1.tick_params(axis='both', pad=0)
    ax1.legend( framealpha=1, ncols=1,
                 labelspacing=0.2,
    handletextpad=0.2,
    columnspacing=0.2,
    borderpad=0.2,
    borderaxespad=0.1, handlelength=0.8)
    ax1.set_xlim(-0.02, max(probs) + 0.02)
    ax1.set_ylim(-2, 105)
    ax1.xaxis.set_major_formatter(fmt_x)
    ax1.yaxis.set_major_formatter(fmt_y)

    fig.tight_layout()
    fig.savefig(path.replace('.pdf', '_mission_completion.pdf'))
    plt.close(fig)

    print(f'  [Fig 2 - Mission Completion] {path}')



# ─────────────────────────────────────────────────────────────────────────────
# Figure 3 — Prediction horizon
# ─────────────────────────────────────────────────────────────────────────────

def figure3_horizon(sweep_results: List[Dict], path: str):
    """
    For each violated Sc A run, shows two quantities:
      - Aligned DT prediction horizon: how many seconds BEFORE the actual
        depletion the aligned DT (Sc C) issued abort_cmd. Positive means
        the aligned DT acted before the violation would have occurred.
      - Monitor B lead time: how many seconds before depletion Monitor B
        fired in the same run. Monitor B fires mid-spray after commitment;
        aligned DT fires at arrive before commitment.
    The gap between the two distributions is the argument: aligned DT
    catches the problem earlier, proactively, before the drone is committed.
    """
    horizons   = []
    mon_b_lead = []

    for r in sweep_results:
        for ra, rc, rb in zip(r['raw_A'], r['raw_C'], r['raw_B']):
            if not ra['violated']:
                continue
            viol_t = ra['viol_t']
            if viol_t is None:
                continue
            # Aligned DT prediction horizon
            if rc['abort_t'] is not None:
                adv = viol_t - rc['abort_t']
                if adv > 0:
                    horizons.append(adv)
            # Monitor B lead time (positive = fired before depletion)
            if ra['mon_b_t'] is not None:
                lead = viol_t - ra['mon_b_t']
                mon_b_lead.append(lead)

    if not horizons:
        print('  [Fig 3] skipped — no violated runs with prediction data')
        return

    fig, ax = plt.subplots(figsize=(FIG_W * 0.65, 2.8))

    all_vals = horizons + (mon_b_lead if mon_b_lead else [])
    lo = min(0, min(all_vals) - 10)
    hi = max(all_vals) + 20
    bins = np.linspace(lo, hi, 16)

    ax.hist(horizons, bins=bins, color=CC, alpha=0.80,
            label=f'Aligned DT — proactive abort\n(mean {np.mean(horizons):.0f} s before violation)',
            edgecolor='white', lw=0.4)
    if mon_b_lead:
        ax.hist(mon_b_lead, bins=bins, color=CB, alpha=0.55,
                label=f'Monitor B — reactive fire\n(mean {np.mean(mon_b_lead):.0f} s before violation)',
                edgecolor='white', lw=0.4, hatch='//')

    ax.axvline(np.mean(horizons), color=CC, lw=1.2, ls='--')
    if mon_b_lead:
        ax.axvline(np.mean(mon_b_lead), color=CB, lw=1.2, ls='-.')
    ax.axvline(0, color='#B71C1C', lw=1.0, ls='-', alpha=0.7,
               label='Violation moment')

    ax.set_xlabel('Seconds before violation (positive = earlier detection)')
    ax.set_ylabel('Count')
    ax.set_title('Detection timing across violated runs\n'
                 'Aligned DT (proactive) vs Monitor B (reactive)', pad=3)
    ax.legend(framealpha=0.95, edgecolor=CGRID, fontsize=FONT_SIZE)

    fig.savefig(path)
    plt.close(fig)
    print(f'  [Fig 3] {path}')


# ─────────────────────────────────────────────────────────────────────────────
# Figure 4 — Monitor A (Muñoz fidelity monitor) comparison
# ─────────────────────────────────────────────────────────────────────────────


def figure4_monitor_a(trace_a, trace_b, trace_c, path: str):
    """
    Monitor A reframed as prediction error monitor (Muñoz spirit).

    With ALPHA=1.0 both DTs always know the true battery, so |PT-DT_budget|
    is trivially near zero. The meaningful divergence is in PREDICTIONS:
    dt_predicted_end = pt_bat_now - cons_est
    After motor failure:
      Non-aligned: cons_est_wrong is too low -> dt_predicted_end too HIGH
                   -> signed error = dt_predicted_end - actual_next > 0 (DANGEROUS)
      Aligned:     cons_est_correct is conservative -> dt_predicted_end too LOW
                   -> signed error < 0 (SAFE — conservative)

    Top panel:  PT battery and DT one-step-ahead predictions per cell.
                Non-aligned predictions consistently above actual (optimistic).
                Aligned predictions consistently below actual (conservative).
    Bottom panel: signed prediction error per cell.
                  Above zero = dangerous optimism (non-aligned after failure).
                  Below zero = safe conservatism (aligned always).
                  Monitor A threshold shown — fires only on dangerous side.
    """
    def _pred_errors(trace):
        cps = [cp for cp in trace
               if cp.dt_decision in ('resume','resume_fast','abort')
               and cp.event.startswith(('spray_done','abort_cmd'))]
        times, preds, actuals, signed, cells = [], [], [], [], []
        for i in range(len(cps)-1):
            c = cps[i]; nxt = cps[i+1]
            times.append(c.sim_time)
            preds.append(c.dt_predicted_end)
            actuals.append(nxt.pt_bat)
            signed.append(c.dt_predicted_end - nxt.pt_bat)
            cells.append(c.cell_idx)
        return times, preds, actuals, signed, cells

    times_a, preds_a, acts_a, signed_a, cells_a = _pred_errors(trace_a)
    times_c, preds_c, acts_c, signed_c, cells_c = _pred_errors(trace_c)

    # Key events
    fail_t    = next((cp.sim_time for cp in trace_a
                      if cp.motor_degraded and cp.dt_decision != 'observe'), None)
    dep_t     = next((cp.sim_time for cp in trace_a if cp.depleted), None)
    abort_c_t = next((cp.sim_time for cp in trace_c if cp.dt_decision == 'abort'), None)
    mon_b_t   = next((cp.sim_time for cp in trace_a if cp.monitor_b), None)

    fig, ax1 = plt.subplots(figsize=(FIG_W, FIG_H))

    # ── Top: actual battery vs predictions ───────────────────────────────────
    ax1.plot(times_a, acts_a, color=CA, lw=1.3, marker='o', ms=1,
             label='PT1', ls='-')
    ax1.plot(times_c, acts_c, color=CC, lw=1.3, marker='o', ms=1,
             label='PT3', ls='-')
    ax1.plot(times_a, preds_a, color=CA, lw=1.1, marker='^', ms=1,
             label='DT1 pred.', ls='--')
    ax1.plot(times_c, preds_c, color=CC, lw=1.1, marker='^', ms=1,
             label='DT3 pred.', ls='--')


    

#
    if fail_t:
        if fail_t>400:
            a = -260
        else:
            a = 10
        ax1.axvline(fail_t, color='#5D4037', lw=0.9, ls=':', alpha=0.75, zorder=2)
        ax1.text(fail_t + a, config.TOT_BAT * 0.3,
            'battery\nstress', fontsize=FONT_SIZE, color='#5D4037', va='top')

    if abort_c_t:
        ax1.axvline(abort_c_t, color=CC, lw=1.0, ls='--', alpha=0.8)

    if dep_t:
        ax1.axvline(dep_t, color=CA, lw=1.0, ls='-', alpha=0.6)


    ax1.set_ylabel('Battery level (Wh)', labelpad=0)
    ax1.set_xlabel('Simulation time (s)', labelpad=0)
    ax1.tick_params(axis="both", pad=0)
    #ax1.set_title(
    #    'Monitor A (Munoz, reframed as prediction error monitor)\n'
    #    'Non-aligned: optimistic predictions after failure (dangerous). '
    #    'Aligned: conservative predictions (safe).',
    #    pad=3, fontweight='bold', fontsiz e=7.5)
    from matplotlib.lines import Line2D
    # Add an invisible dummy entry before the last label
    dummy = Line2D([], [], linestyle='', alpha=0)
    h, l = ax1.get_legend_handles_labels()

    ax1.legend(
        h, l,
        ncols=2,
        loc='upper right',
        fontsize=FONT_SIZE,
        framealpha=0.8,
        edgecolor=CGRID,
        labelspacing=0.2,
        handletextpad=0.2,
        columnspacing=0.2,
        borderpad=0.2,
        borderaxespad=0.1,
        handlelength=0.8
    )
    ax1.set_ylim(-10, max(max(acts_a), max(preds_a))*1.15 if acts_a else 100)


    t_lo = min(min(times_a), min(times_c)) - 20 if times_a and times_c else 0
    t_hi = max(max(times_a), max(times_c)) + 20 if times_a and times_c else 200
    ax1.set_xlim(t_lo, t_hi)

    fig.savefig(path)
    plt.close(fig)
    print(f'  [Fig 4] {path}')



def figure4_monitor_a_old(trace_a, trace_b, trace_c, path: str):
    """
    Monitor A reframed as prediction error monitor (Muñoz spirit).

    With ALPHA=1.0 both DTs always know the true battery, so |PT-DT_budget|
    is trivially near zero. The meaningful divergence is in PREDICTIONS:
    dt_predicted_end = pt_bat_now - cons_est
    After motor failure:
      Non-aligned: cons_est_wrong is too low -> dt_predicted_end too HIGH
                   -> signed error = dt_predicted_end - actual_next > 0 (DANGEROUS)
      Aligned:     cons_est_correct is conservative -> dt_predicted_end too LOW
                   -> signed error < 0 (SAFE — conservative)

    Top panel:  PT battery and DT one-step-ahead predictions per cell.
                Non-aligned predictions consistently above actual (optimistic).
                Aligned predictions consistently below actual (conservative).
    Bottom panel: signed prediction error per cell.
                  Above zero = dangerous optimism (non-aligned after failure).
                  Below zero = safe conservatism (aligned always).
                  Monitor A threshold shown — fires only on dangerous side.
    """
    def _pred_errors(trace):
        cps = [cp for cp in trace
               if cp.dt_decision in ('resume','resume_fast','abort')
               and cp.event.startswith(('spray_done','abort_cmd'))]
        times, preds, actuals, signed, cells = [], [], [], [], []
        for i in range(len(cps)-1):
            c = cps[i]; nxt = cps[i+1]
            times.append(c.sim_time)
            preds.append(c.dt_predicted_end)
            actuals.append(nxt.pt_bat)
            signed.append(c.dt_predicted_end - nxt.pt_bat)
            cells.append(c.cell_idx)
        return times, preds, actuals, signed, cells

    times_a, preds_a, acts_a, signed_a, cells_a = _pred_errors(trace_a)
    times_c, preds_c, acts_c, signed_c, cells_c = _pred_errors(trace_c)

    # Key events
    fail_t    = next((cp.sim_time for cp in trace_a
                      if cp.motor_degraded and cp.dt_decision != 'observe'), None)
    dep_t     = next((cp.sim_time for cp in trace_a if cp.depleted), None)
    abort_c_t = next((cp.sim_time for cp in trace_c if cp.dt_decision == 'abort'), None)
    mon_b_t   = next((cp.sim_time for cp in trace_a if cp.monitor_b), None)

    fig = plt.figure(figsize=(FIG_W, 4.0))
    gs  = GridSpec(2, 1, figure=fig, height_ratios=[1.6, 1.2], hspace=0.40)
    ax1 = fig.add_subplot(gs[0])
    ax2 = fig.add_subplot(gs[1])

    # ── Top: actual battery vs predictions ───────────────────────────────────
    ax1.plot(times_a, acts_a, color=CA, lw=1.3, marker='o', ms=4,
             label='Actual PT battery (next arrive)')
    ax1.plot(times_a, preds_a, color=CA, lw=1.1, marker='^', ms=4,
             ls='--', alpha=0.75,
             label='Non-aligned DT prediction (optimistic after failure)')
    ax1.plot(times_c, preds_c, color=CC, lw=1.1, marker='v', ms=4,
             ls=':', alpha=0.85,
             label='Aligned DT prediction (conservative)')

    # Shade dangerous overestimate region
    for i in range(len(times_a)):
        if preds_a[i] > acts_a[i]:
            ax1.fill_between([times_a[i]-5, times_a[i]+5],
                              [acts_a[i], acts_a[i]],
                              [preds_a[i], preds_a[i]],
                              alpha=0.20, color=CA)

    if fail_t:

        ax1.axvline(fail_t, color='#5D4037', lw=0.9, ls=':', alpha=0.7)
        ax1.text(fail_t+4, max(acts_a)*0.9 if acts_a else 100,
                 'battery\nstress', fontsize=FONT_SIZE, color='#5D4037', rotation=90, va='top')
    if abort_c_t:
        ax1.axvline(abort_c_t, color=CC, lw=1.0, ls='--', alpha=0.8)
        ax1.text(abort_c_t+4, max(acts_a)*0.7 if acts_a else 70,
                 'Sc C\nabort', fontsize=FONT_SIZE, color=CC, rotation=90, va='top')
    if dep_t:
        ax1.axvline(dep_t, color=CA, lw=1.0, ls='-', alpha=0.6)
        ax1.text(dep_t+4, max(acts_a)*0.5 if acts_a else 50,
                 'DEPLETED', fontsize=FONT_SIZE, color=CA, rotation=90, va='top')

    ax1.set_ylabel('Battery level (Wh)')
    ax1.set_title(
        'Monitor A (Munoz, reframed as prediction error monitor)\n'
        'Non-aligned: optimistic predictions after failure (dangerous). '
        'Aligned: conservative predictions (safe).',
        pad=3, fontweight='bold', fontsize=FONT_SIZE)
    ax1.legend(fontsize=FONT_SIZE, framealpha=0.95, edgecolor=CGRID, loc='upper right')
    ax1.set_ylim(-10, max(max(acts_a), max(preds_a))*1.15 if acts_a else 100)

    # ── Bottom: signed prediction error ──────────────────────────────────────
    x_a = np.array(times_a); y_a = np.array(signed_a)
    x_c = np.array(times_c); y_c = np.array(signed_c)

    ax2.axhline(0, color=CANNOT, lw=0.8, alpha=0.5)
    ax2.axhline(config.MONITOR_A_THRESHOLD, color=CMA, lw=1.0, ls='--',
                label=f'Monitor A threshold (+{config.MONITOR_A_THRESHOLD:.1f} Wh)')
    ax2.axhline(-config.MONITOR_A_THRESHOLD, color=CC, lw=0.8, ls=':',
                alpha=0.6, label=f'Conservative bound (-{config.MONITOR_A_THRESHOLD:.1f} Wh)')

    # Bars for non-aligned (red = dangerous above zero, grey = safe below)
    for i, (t, s) in enumerate(zip(x_a, y_a)):
        color = CA if s > 0 else CANNOT
        alpha = 0.85 if s > 0 else 0.35
        ax2.bar(t, s, width=18, color=color, alpha=alpha, zorder=3)

    # Dots for aligned (blue, always negative)
    ax2.scatter(x_c, y_c, color=CC, s=28, marker='v', zorder=5,
                label='Aligned DT prediction error (conservative)')

    # Mark Monitor A fires on non-aligned (fires on positive errors > threshold)
    mon_a_fires = [(t, s) for t, s in zip(x_a, y_a)
                   if s > config.MONITOR_A_THRESHOLD]
    if mon_a_fires:
        ft, fv = zip(*mon_a_fires)
        ax2.scatter(ft, fv, color=CMA, s=50, marker='^', zorder=6,
                    label=f'Monitor A fires ({len(mon_a_fires)}x — non-aligned)')

    if fail_t:
        ax2.axvline(fail_t, color='#5D4037', lw=0.9, ls=':', alpha=0.7)
    if abort_c_t:
        ax2.axvline(abort_c_t, color=CC, lw=1.0, ls='--', alpha=0.8)
    if dep_t:
        ax2.axvline(dep_t, color=CA, lw=1.0, ls='-', alpha=0.6)

    ax2.set_xlabel('Simulation time (s)')
    ax2.set_ylabel('Signed prediction error (Wh)\n(+ = optimistic, - = conservative)')
    ax2.set_title(
        'Non-aligned: positive errors after failure (overestimates, Monitor A fires).\n'
        'Aligned: negative errors throughout (underestimates, never fires on dangerous side).',
        pad=3, fontsize=FONT_SIZE)
    ax2.legend(fontsize=FONT_SIZE, framealpha=0.95, edgecolor=CGRID, loc='upper left')
    ymax = max(max(abs(s) for s in signed_a + signed_c), config.MONITOR_A_THRESHOLD) * 1.4 + 5
    ax2.set_ylim(-ymax, ymax)

    t_lo = min(min(times_a), min(times_c)) - 20 if times_a and times_c else 0
    t_hi = max(max(times_a), max(times_c)) + 20 if times_a and times_c else 200
    for ax in (ax1, ax2):
        ax.set_xlim(t_lo, t_hi)

    fig.savefig(path)
    plt.close(fig)
    print(f'  [Fig 4] {path}')


def _annotate_events(ax, fail_t, mon_a_t, mon_b_t, abort_c_t, dep_t,
                     y_max, ypos_frac=0.85):
    """Shared vertical event markers for figures 4."""
    if fail_t:
        ax.axvline(fail_t, color='#5D4037', lw=0.8, ls=':', alpha=0.6)
        ax.text(fail_t + 4, y_max * ypos_frac, 'battery\nstress',
                fontsize=FONT_SIZE, color='#5D4037', rotation=90, va='top')
    if abort_c_t:
        ax.axvline(abort_c_t, color=CC, lw=1.0, ls='--', alpha=0.8)
        ax.text(abort_c_t + 4, y_max * (ypos_frac - 0.12),
                'Sc C\nabort', fontsize=FONT_SIZE, color=CC, rotation=90, va='top')
    if mon_b_t:
        ax.axvline(mon_b_t, color=CB, lw=1.0, ls='-.', alpha=0.8)
        ax.text(mon_b_t + 4, y_max * (ypos_frac - 0.24),
                'Mon B\nfires', fontsize=FONT_SIZE, color=CB, rotation=90, va='top')
    if mon_a_t:
        ax.axvline(mon_a_t, color=CMA, lw=1.0, ls='-.', alpha=0.8)
        ax.text(mon_a_t + 4, y_max * (ypos_frac - 0.36),
                'Mon A\nfires', fontsize=FONT_SIZE, color=CMA, rotation=90, va='top')
    if dep_t:
        ax.axvline(dep_t, color=CA, lw=1.2, ls='-', alpha=0.6)
        ax.text(dep_t + 4, y_max * (ypos_frac - 0.48),
                'DEPLETED', fontsize=FONT_SIZE, color=CA, rotation=90, va='top')


# ─────────────────────────────────────────────────────────────────────────────
# Table 1 — Summary (LaTeX-style)
# ─────────────────────────────────────────────────────────────────────────────

def table1_summary(sweep_results: List[Dict], path: str):
    """
    Summary table aggregated over all failure probabilities.

    Columns: Stage | Depletions | Mid-spray aborts | Mean cells | Mission complete | Avg battery @ return
    Mission complete = cells_done == NUM_HEC AND NOT violated AND NOT depleted.
    Depletions column is only non-zero for Sc A by construction.
    """
    all_raw = {'A': [], 'B': [], 'C': []}
    for r in sweep_results:
        all_raw['A'].extend(r['raw_A'])
        all_raw['B'].extend(r['raw_B'])
        all_raw['C'].extend(r['raw_C'])

    fig, ax = plt.subplots(figsize=(FIG_W, 1.8))
    ax.axis('off')

    headers = ['Stage',
               'Safety\ndepletions',
               'Mid-spray\naborts',
               'Mean cells\ncompleted',
               'Mission\ncomplete',
               'Avg battery\n@ return (Wh)']

    labels = {
        'A': 'Sc A — Non-aligned, no monitor',
        'B': 'Sc B — Non-aligned + Monitor B',
        'C': 'Sc C — Aligned DT (ours)',
    }

    rows = []
    for sc in ('A', 'B', 'C'):
        data = all_raw[sc]
        n     = len(data)
        # Depletions: runs where drone physically ran out of battery mid-field
        deplet = sum(1 for r in data if r['violated'])
        # Mid-spray aborts: monitor fired mid-spray (Sc B only)
        abort  = sum(1 for r in data if r['mid_abort'])
        # Mean cells completed (depleted runs included — honest)
        cells  = float(np.mean([r['cells_done'] for r in data]))
        # Mission complete: all cells done, no depletion, no mid-spray abort
        compl  = sum(1 for r in data
                     if r['cells_done'] == config.NUM_HEC
                     and not r['violated']
                     and not r['mid_abort'])
        # Average battery when returned in completed missions
        completed = [r for r in data
                     if r['cells_done'] == config.NUM_HEC
                     and not r['violated']
                     and not r['mid_abort']]
        avg_bat = (float(np.mean([r['final_battery'] for r in completed]))
                   if completed else 0.0)
        rows.append([
            labels[sc],
            f'{deplet/n*100:.1f}%' if deplet > 0 else '0.0%',
            f'{abort/n*100:.1f}%'  if abort  > 0 else '0.0%',
            f'{cells:.2f} / {config.NUM_HEC}',
            f'{compl/n*100:.1f}%',
            f'{avg_bat:.1f}',
        ])

    tbl = ax.table(cellText=rows, colLabels=headers,
                   loc='center', cellLoc='center')
    tbl.auto_set_font_size(False)
    tbl.set_fontsize(FONT_SIZE)
    tbl.scale(1, 1.7)

    # Header row styling
    for j in range(len(headers)):
        tbl[0, j].set_facecolor('#37474F')
        tbl[0, j].set_text_props(color='white', fontweight='bold')

    # Row styling — highlight depletion cell red for Sc A
    bg = ['#FFF3F3', '#FFF8F3', '#F3F8FF']
    for i, sc in enumerate(('A', 'B', 'C')):
        for j in range(len(headers)):
            cell = tbl[i+1, j]
            cell.set_facecolor(bg[i])
            cell.set_edgecolor(CGRID)
        # Highlight depletion column for Sc A
        if sc == 'A':
            tbl[i+1, 1].set_facecolor('#FFCDD2')
            tbl[i+1, 1].set_text_props(fontweight='bold', color='#C62828')

    ax.set_title(
        'Table 1 — Stage outcomes aggregated across all failure probabilities\n'
        'Depletions (safety violations) occur only in Sc A by construction',
        pad=8, fontsize=FONT_SIZE, fontweight='bold')

    fig.savefig(path)
    plt.close(fig)
    print(f'  [Table 1] {path}')


def table1_latex(sweep_results: List[Dict], path: str):
    """
    Generate a scientific LaTeX table with booktabs styling.
    """
    all_raw = {'A': [], 'B': [], 'C': []}
    for r in sweep_results:
        all_raw['A'].extend(r['raw_A'])
        all_raw['B'].extend(r['raw_B'])
        all_raw['C'].extend(r['raw_C'])

    refs = {'A': r'\ref{scenario:a}', 'B': r'\ref{scenario:b}', 'C': r'\ref{scenario:c}'}

    rows_data = []
    for sc in ('A', 'B', 'C'):
        data = all_raw[sc]
        n     = len(data)

        # Mission complete: all cells done, not violated, not aborted mid-spray
        compl  = sum(1 for r in data
                     if r['cells_done'] == config.NUM_HEC
                     and not r['violated']
                     and not r['mid_abort'])

        # Mean cells completed
        cells  = float(np.mean([r['cells_done'] for r in data]))

        # Average battery at return (only for completed missions)
        completed = [r for r in data
                     if r['cells_done'] == config.NUM_HEC
                     and not r['violated']
                     and not r['mid_abort']]
        avg_bat = (float(np.mean([r['final_battery'] for r in completed]))
                   if completed else 0.0)

        # Mid-spray aborts: monitor B fired during spraying
        mid_abort = sum(1 for r in data if r['mid_abort'])

        # Battery depletions: safety violations
        deplet = sum(1 for r in data if r['violated'])

        # DT aborts: runs that neither completed mission nor depleted
        dt_abort_count = n - compl - deplet

        rows_data.append({
            'ref': refs[sc],
            'compl': compl/n*100,
            'cells': cells,
            'avg_bat': avg_bat,
            'dt_abort': dt_abort_count/n*100,
            'mid_abort': mid_abort/n*100,
            'deplet': deplet/n*100,
        })

    latex = r"""\begin{table}[t]
\centering
\caption{Scenario outcomes aggregated across all battery stress probability
levels. }
\label{tab:outcomes}
  \setlength{\tabcolsep}{1.3pt}
\begin{tabular}{@{}lccccccc@{}}

\toprule
\multirow{2}{*}{Scenario}    & Success & Cells & Avg battery & DT& Mid-spray & Battery\\
  & rate & completed & at return & aborts &  aborts & depletions\\
\midrule
"""
    for row in rows_data:
        latex += f"{row['ref']}   & {row['compl']:.1f}\\% & {row['cells']:.2f} & {row['avg_bat']:.1f} Wh &{row['dt_abort']:.1f}\\%& {row['mid_abort']:.1f}\\% & {row['deplet']:.1f}\\%\\\\\n"

    latex += r"""\bottomrule
\end{tabular}

\end{table}
"""

    with open(path, 'w') as f:
        f.write(latex)
    print(f'  [Table 1 LaTeX] {path}')


# ─────────────────────────────────────────────────────────────────────────────
# Figure 1b — Battery vs cell index
# ─────────────────────────────────────────────────────────────────────────────

def figure1b_cell_axis_old(trace_a, trace_b, trace_c, motor_cell,path: str):
    """
    Same data as Fig 1 but x-axis is cell index, not time.
    At each cell boundary the battery levels are directly comparable:
    Sc C (aligned, speed boost) always has MORE battery than Sc A/B at
    the same cell because faster spraying consumes less total energy.
    This makes the speed boost benefit unambiguous.
    """
    fig, ax = plt.subplots(figsize=(FIG_W, FIG_H))

   
    def _cell_battery(trace):
        """Extract (cell_idx, pt_bat) at each spray_done or depleted checkpoint."""
        pts = []
        # Start point
        pts.append((-0.5, config.TOT_BAT))
        for cp in trace:
            if cp.dt_decision in ('resume', 'resume_fast') or cp.depleted:
                pts.append((cp.cell_idx, cp.pt_bat))
            elif cp.dt_decision == 'abort':
                pts.append((cp.cell_idx - 0.5, cp.pt_bat))
                break
        return zip(*pts) if pts else ([], [])

    cells_a, bats_a = _cell_battery(trace_a)
    cells_b, bats_b = _cell_battery(trace_b)
    cells_c, bats_c = _cell_battery(trace_c)

    ax.plot(cells_a, bats_a, color=CA, lw=1.4, marker='o', ms=3,
            zorder=3, label=LABEL_A)
    ax.plot(cells_b, bats_b, color=CB, lw=1.4, marker='s', ms=3,
            zorder=3, ls='--', label=LABEL_B)
    ax.plot(cells_c, bats_c, color=CC, lw=1.6, marker='^', ms=3,
            zorder=4, label=LABEL_C)

    # Battery stress vertical — at the cell where stress begins
    fail_cell = motor_cell#config.MOTOR_FAILURE_CELL
    ax.axvline(fail_cell + 0.5, color='#5D4037', lw=0.9, ls=':', alpha=0.7)
    if fail_cell >= 2:
        a = -2
    else:
        a = 0.7
    ax.text(fail_cell +a , config.TOT_BAT * 0.3,
            'battery\nstress', fontsize=FONT_SIZE, color='#5D4037', va='top')

    # Sc A depletion marker
    dep = next((cp for cp in trace_a if cp.depleted), None)
    if dep:
        ax.scatter([dep.cell_idx ], [dep.pt_bat],
                   color=CA, marker='o', s=20, zorder=8)

    # Sc B mid-spray abort marker
    abort_b = next((cp for cp in trace_b if 'monitor_b_abort' in cp.event), None)
    print(cells_b)
    if abort_b:
        
        ax.scatter([cells_b[abort_b.cell_idx]], [bats_b[abort_b.cell_idx]],
                   color=CB, marker='s', s=20, zorder=7)
    

    # Sc C proactive abort marker
    abort_c = next((cp for cp in trace_c if cp.dt_decision == 'abort'), None)
    if abort_c:
        ax.scatter([abort_c.cell_idx-1], [bats_c[abort_c.cell_idx]],
                   color=CC, marker='^', s=20, zorder=7,)


    # Speed boost advantage annotation — first degraded cell where C > A
    if cells_a and cells_c and len(cells_a) > fail_cell + 2:
        boost_cell = fail_cell + 1
        try:
            bat_c_boost = np.interp(boost_cell + 1, cells_c, bats_c)
            bat_a_boost = np.interp(boost_cell + 1, cells_a, bats_a)

        except Exception:
            pass

    ax.set_xlabel(f'Cell index (0\u2013{config.NUM_HEC - 1})', labelpad=0)
    ax.set_xlim(trace_a[0].cell_idx - 0.5, trace_a[-1].cell_idx + 0.5)
    ax.set_ylabel('Battery level (Wh)', labelpad=0)

    ax.set_xticks(range(config.NUM_HEC ))
    ax.set_xticklabels(
        ['start'] + [str(i) for i in range(config.NUM_HEC-1)])
    ax.legend(loc='upper right', framealpha=0.95, edgecolor=CGRID, ncols =1,
                 labelspacing=0.2,
    handletextpad=0.2,
    columnspacing=0.2,
    borderpad=0.2,
    borderaxespad=0.1,handlelength=1.2)
    ax.set_ylim(-15, config.TOT_BAT + 25)
    
    ax.tick_params(axis="both", pad=0)
    fig.savefig(path)
    plt.close(fig)
    print(f'  [Fig 1b] {path}')





def figure1b_cell_axis(trace_a, trace_b, trace_c, path: str):
    """
    Same data as Fig 1 but x-axis is cell index, not time.
    At each cell boundary the battery levels are directly comparable:
    Sc C (aligned, speed boost) always has MORE battery than Sc A/B at
    the same cell because faster spraying consumes less total energy.
    This makes the speed boost benefit unambiguous.
    """
    fig, ax = plt.subplots(figsize=(FIG_W, 3.2))

    safe_thresh = config.CONS_DEGRADED + config.RET_COST
    #ax.axhspan(0, safe_thresh, alpha=0.05, color='#FFCDD2', zorder=0)
    #ax.axhline(safe_thresh, color='#EF9A9A', lw=0.8, ls='--', alpha=0.6, zorder=1)
    #ax.text(0.05, safe_thresh + 3,
    #        r'$\phi_{\rm safe}$', fontsize=FONT_SIZE, color='#C62828')

    def _cell_battery(trace):
        """Extract (cell_idx, pt_bat) at each spray_done or depleted checkpoint."""
        pts = []
        # Start point
        pts.append((-0.5, config.TOT_BAT))
        for cp in trace:
            if cp.dt_decision in ('resume', 'resume_fast') or cp.depleted:
                pts.append((cp.cell_idx, cp.pt_bat))
            elif cp.dt_decision == 'abort':
                pts.append((cp.cell_idx - 0.5, cp.pt_bat))
                break
        return zip(*pts) if pts else ([], [])

    cells_a, bats_a = _cell_battery(trace_a)
    cells_b, bats_b = _cell_battery(trace_b)
    cells_c, bats_c = _cell_battery(trace_c)

    cells_a, bats_a = list(cells_a), list(bats_a)
    cells_b, bats_b = list(cells_b), list(bats_b)
    cells_c, bats_c = list(cells_c), list(bats_c)

    ax.plot(cells_a, bats_a, color=CA, lw=1.4, marker='o', ms=5,
            zorder=3, label=LABEL_A)
    ax.plot(cells_b, bats_b, color=CB, lw=1.4, marker='s', ms=5,
            zorder=3, ls='--', label=LABEL_B)
    ax.plot(cells_c, bats_c, color=CC, lw=1.6, marker='^', ms=5,
            zorder=4, label=LABEL_C)

    # Motor failure vertical
    fail_cell = config.MOTOR_FAILURE_CELL
    ax.axvline(fail_cell - 0.5, color='#5D4037', lw=0.9, ls=':', alpha=0.7)
    ax.text(fail_cell - 0.4, config.TOT_BAT * 0.3,
            'battery\nstress', fontsize=FONT_SIZE, color='#5D4037', va='top')

    # Sc A depletion marker
    dep = next((cp for cp in trace_a if cp.depleted), None)
    if dep:
        ax.scatter([dep.cell_idx], [dep.pt_bat],
                   color=CA, marker='o', s=20, zorder=8, label=LABEL_A + '-depleted')
        
    # Sc B abort marker
    abort_b = next((cp for cp in trace_b if 'monitor_b_abort' in cp.event), None)
    if abort_b:
        ax.scatter([abort_b.cell_idx], [abort_b.pt_bat],
                   color=CB, marker='v', s=60, zorder=7,
                   label=LABEL_B + '-mid-spray abort')




    # Sc C proactive abort marker
    abort_c = next((cp for cp in trace_c if cp.dt_decision == 'abort'), None)
    if abort_c:
        ax.scatter([abort_c.cell_idx], [abort_c.pt_bat],
                   color=CC, marker='^', s=60, zorder=7)


    # Shade region where Sc C > Sc A (speed boost advantage)
    if cells_a and cells_c:
        common = [c for c in cells_c if c in cells_a]
        if common:
            ax.annotate('Speed boost saves\nenergy per cell',
                        xy=(fail_cell + 1, np.interp(fail_cell + 1, cells_c, bats_c)),
                        xytext=(fail_cell + 2, np.interp(fail_cell + 1, cells_c, bats_c) + 30),
                        fontsize=FONT_SIZE, color=CC,
                        arrowprops=dict(arrowstyle='->', color=CC, lw=0.7))

    ax.set_xlabel(f'Cell index (0 – {config.NUM_HEC - 1})',labelpad=0)
    ax.set_ylabel('Battery level at cell boundary (Wh)',labelpad=0)
    ax.set_title(
        'Battery per cell — speed boost (Sc C) preserves more battery\n'
        'than full-speed scenarios (Sc A / B) after motor failure',
        pad=4, fontweight='bold')
    ax.set_xticks(range(config.NUM_HEC))
    ax.legend(loc='upper right', framealpha=0.95, edgecolor=CGRID, handlelength=1.4)
    ax.set_ylim(-15, config.TOT_BAT + 20)
    ax.set_xlim(-0.8, config.NUM_HEC - 0.2)
    ax.tick_params(axis="both", pad=0)

    fig.savefig(path)
    plt.close(fig)
    print(f'  [Fig 1b] {path}')


# ─────────────────────────────────────────────────────────────────────────────
# Figure 5 — Monitor A (Muñoz) on aligned vs non-aligned PT
# ─────────────────────────────────────────────────────────────────────────────

def figure5_monitor_a_comparison(trace_a, trace_c, path: str):
    """
    Side-by-side comparison of Monitor A prediction error on:
      Left:  Non-aligned DT (Sc A) — errors grow positive after motor failure.
             Monitor A fires on the dangerous side (overestimate).
      Right: Aligned DT (Sc C) — errors stay negative throughout.
             Monitor A never fires on the dangerous side.
             Even though the DT diverges numerically from the PT prediction
             (it conservatively underestimates), this is semantically CORRECT:
             the aligned DT always predicts a worse-case scenario, so its
             safety decisions are sound.

    Key argument: semantic alignment guarantees that the DT's predictions
    are always conservative (negative error). Numerical divergence does not
    imply semantic misalignment — the aligned DT is semantically equivalent
    to the PT even when its numerical predictions differ.
    """
    def _pred_errors(trace):
        cps = [cp for cp in trace
               if cp.dt_decision in ('resume','resume_fast','abort')
               and cp.event.startswith(('spray_done','abort_cmd'))]
        result = []
        for i in range(len(cps)-1):
            c = cps[i]; nxt = cps[i+1]
            result.append({
                't': c.sim_time,
                'cell': c.cell_idx,
                'pred': c.dt_predicted_end,
                'actual': nxt.pt_bat,
                'signed': c.dt_predicted_end - nxt.pt_bat,
                'deg': c.motor_degraded,
            })
        return result

    errs_a = _pred_errors(trace_a)
    errs_c = _pred_errors(trace_c)

    fail_t    = next((cp.sim_time for cp in trace_a
                      if cp.motor_degraded and cp.dt_decision != 'observe'), None)
    dep_t     = next((cp.sim_time for cp in trace_a if cp.depleted), None)
    abort_c_t = next((cp.sim_time for cp in trace_c if cp.dt_decision == 'abort'), None)

    fig, (ax_a, ax_c) = plt.subplots(1, 2, figsize=(FIG_W, 3.2), sharey=True)
    fig.subplots_adjust(wspace=0.08)

    def _plot_side(ax, errs, color, title, show_ylabel,
                   fail_t=None, event2_t=None, event2_col=None, event2_lbl=None):
        times  = [e['t']      for e in errs]
        signed = [e['signed'] for e in errs]

        ax.axhline(0, color=CANNOT, lw=0.8, alpha=0.5, zorder=1)
        ax.axhline(config.MONITOR_A_THRESHOLD, color=CMA, lw=1.0, ls='--',
                   label=f'Threshold (+{config.MONITOR_A_THRESHOLD:.1f} Wh)', zorder=2)
        ax.axhline(-config.MONITOR_A_THRESHOLD, color=CC, lw=0.8, ls=':',
                   alpha=0.5, zorder=2)

        # Color bars: above threshold = dangerous (Monitor A fires)
        for t, s in zip(times, signed):
            if s > config.MONITOR_A_THRESHOLD:
                c = CMA   # Monitor A fires — dangerous
            elif s > 0:
                c = color  # optimistic but below threshold
            else:
                c = CC     # conservative (safe)
            ax.bar(t, s, width=15,
                   color=c, alpha=0.80 if abs(s) > config.MONITOR_A_THRESHOLD else 0.50,
                   zorder=3)

        # Mark Monitor A fires
        fires = [(t, s) for t, s in zip(times, signed)
                 if s > config.MONITOR_A_THRESHOLD]
        if fires:
            ft, fv = zip(*fires)
            ax.scatter(ft, fv, color=CMA, s=55, marker='^', zorder=6,
                       label=f'Monitor A fires ({len(fires)}x)')

        if fail_t:
            ax.axvline(fail_t, color='#5D4037', lw=0.9, ls=':', alpha=0.7)
            
        if event2_t:
            ax.axvline(event2_t, color=event2_col, lw=1.0, ls='--', alpha=0.8)


        ax.set_xlabel('Simulation time (s)')
        if show_ylabel:
            ax.set_ylabel('Signed prediction error (Wh)\n(+ = optimistic, - = conservative)')
        ax.set_title(title, pad=3, fontsize=FONT_SIZE, fontweight='bold')
        ax.legend(fontsize=FONT_SIZE, framealpha=0.95, edgecolor=CGRID, loc='lower left')

    _plot_side(ax_a, errs_a, CA,
               'Non-aligned DT (Sc A)\nOptimistic after failure — Monitor A fires',
               show_ylabel=True,
               fail_t=fail_t, event2_t=dep_t, event2_col=CA, event2_lbl='DEPLETED')

    _plot_side(ax_c, errs_c, CC,
               'Aligned DT (Sc C)\nConservative throughout — Monitor A never fires',
               show_ylabel=False,
               fail_t=fail_t, event2_t=abort_c_t, event2_col=CC, event2_lbl='Sc C\nabort')

    # Set y limits after plotting
    all_signed = [e['signed'] for e in errs_a + errs_c]
    ymax = max(max(abs(s) for s in all_signed), config.MONITOR_A_THRESHOLD) * 1.5 + 3
    for ax in (ax_a, ax_c):
        ax.set_ylim(-ymax, ymax)

    fig.suptitle(
        'Prediction error comparison: non-aligned vs aligned DT'
        'Semantic alignment guarantees conservative predictions — '
        'numerically different but semantically safe.',
        fontsize=FONT_SIZE, fontweight='bold', y=1.01)

    fig.savefig(path, bbox_inches='tight')
    plt.close(fig)
    print(f'  [Fig 5] {path}')


# ─────────────────────────────────────────────────────────────────────────────
# Battery Telemetry vs DT Prediction Gap Analysis
# ─────────────────────────────────────────────────────────────────────────────

def extract_cell_boundary_gaps(sweep_results: List[Dict]) -> Dict:
    """
    Extract per-cell-boundary prediction error data.

    For each mission at each probability level, extract signed prediction errors
    at cell boundaries. A cell boundary is a checkpoint where spray_done or abort occurs.

    Prediction error = dt_predicted_end - pt_bat_actual_next
    (positive = DT overestimated; negative = DT underestimated)

    Returns: {
        'data': [{prob, scenario, cell_idx, mission_idx, signed_error, abs_error}, ...],
        'probs': [0.0, 0.1, 0.2, ...],
        'n_total': total number of observations
    }
    """
    data = []
    probs = []

    for r in sweep_results:
        prob = r['prob']
        probs.append(prob)

        for scenario in ('A', 'C'):  # A=DT1 (non-aligned), C=DT3 (aligned)
            for mission_idx, result in enumerate(r[f'raw_{scenario}']):
                if 'trace' not in result:
                    continue
                trace = result['trace']

                # Extract prediction errors at cell boundaries
                # Prediction error = dt_predicted_end - actual_battery_next
                cps = [cp for cp in trace
                       if cp.dt_decision in ('resume', 'resume_fast', 'abort')
                       and cp.event.startswith(('spray_done', 'abort_cmd'))]

                for i in range(len(cps) - 1):
                    c = cps[i]
                    nxt = cps[i + 1]
                    cell_idx = c.cell_idx

                    # Skip the last cell
                    if cell_idx >= config.NUM_HEC - 1:
                        continue

                    # Signed prediction error: positive = optimistic (dangerous)
                    signed_error = c.dt_predicted_end - nxt.pt_bat_noisy
                    abs_error = abs(signed_error)

                    data.append({
                        'prob': prob,
                        'scenario': scenario,
                        'cell_idx': cell_idx,
                        'mission_idx': mission_idx,
                        'signed_error': signed_error,
                        'abs_error': abs_error
                    })

    return {
        'data': data,
        'probs': sorted(set(probs)),
        'n_total': len(data)
    }


def figure_battery_gap_violin_by_cell_prob(gap_data: Dict, path: str):
    """
    Violin plot (cell × probability): For each probability level (6 panels),
    show paired violin plots of |gap_DT1| and |gap_DT3| faceted by cell index
    (0-8) on the x-axis within each panel.
    """
    import pandas as pd

    data = gap_data['data']
    if not data:
        print('  [Battery Gap] skipped — no trace data available')
        return

    df = pd.DataFrame(data)
    probs = gap_data['probs']

    fig, axes = plt.subplots(2, 3, figsize=(12, 7))
    axes = axes.flatten()

    for idx, prob in enumerate(probs):
        ax = axes[idx]
        df_prob = df[df['prob'] == prob]

        cell_indices = sorted(df_prob['cell_idx'].unique())
        cell_positions = []
        pos = 0

        for cell_idx in cell_indices:
            df_cell = df_prob[df_prob['cell_idx'] == cell_idx]
            cell_positions.append((cell_idx, pos, pos + 1))

            # DT1 (scenario A)
            dt1_data = df_cell[df_cell['scenario'] == 'A']['abs_error'].values
            # DT3 (scenario C)
            dt3_data = df_cell[df_cell['scenario'] == 'C']['abs_error'].values

            if len(dt1_data) > 0:
                parts = ax.violinplot([dt1_data], positions=[pos], widths=0.35,
                            showmeans=True, showmedians=False)
                for pc in parts['bodies']:
                    pc.set_facecolor('#E65100')
                    pc.set_alpha(0.7)
                    pc.set_edgecolor('#E65100')
                    pc.set_linewidth(1.0)
                for partname in ('cbars', 'cmins', 'cmaxes', 'cmeans'):
                    if partname in parts:
                        parts[partname].set_color('#E65100')
                        parts[partname].set_linewidth(1.5)

            if len(dt3_data) > 0:
                parts = ax.violinplot([dt3_data], positions=[pos + 0.5], widths=0.35,
                            showmeans=True, showmedians=False)
                for pc in parts['bodies']:
                    pc.set_facecolor('#1565C0')
                    pc.set_alpha(0.7)
                    pc.set_edgecolor('#1565C0')
                    pc.set_linewidth(1.0)
                for partname in ('cbars', 'cmins', 'cmaxes', 'cmeans'):
                    if partname in parts:
                        parts[partname].set_color('#1565C0')
                        parts[partname].set_linewidth(1.5)

            pos += 1.5

        ax.set_xticks([p[1] + 0.25 for p in cell_positions])
        ax.set_xticklabels([str(p[0]) for p in cell_positions])
        ax.set_xlabel('Cell Index')
        ax.set_ylabel('|Gap| (Wh)')
        ax.set_title(f'Prob={prob:.0%}')
        ax.grid(True, alpha=0.3)

    # Add legend
    handles = [
        mpatches.Patch(facecolor='#E65100', label='DT1 (non-aligned)'),
        mpatches.Patch(facecolor='#1565C0', label='DT3 (aligned)')
    ]
    fig.legend(handles=handles, loc='upper center', ncol=2, bbox_to_anchor=(0.5, 0.98))

    fig.suptitle('Per-cell-boundary gap distribution vs probability level',
                fontsize=FONT_SIZE, fontweight='bold', y=1.00)
    fig.tight_layout()
    fig.savefig(path)
    plt.close(fig)
    print(f'  [Battery Gap — Cell×Prob] {path}')


def figure_battery_gap_violin_pooled(gap_data: Dict, path: str):
    """
    Aggregated violin plot (pooled): A single plot pooling all observations
    across all probability levels — two violins, DT1 vs DT3, showing the
    overall distribution of |gap| with median/IQR annotations.
    """
    import pandas as pd

    data = gap_data['data']
    if not data:
        print('  [Battery Gap] skipped — no trace data available')
        return

    df = pd.DataFrame(data)

    dt1_data = df[df['scenario'] == 'A']['abs_error'].values
    dt3_data = df[df['scenario'] == 'C']['abs_error'].values

    fig, ax = plt.subplots(figsize=(FIG_W, FIG_H))

    positions = [1, 2]
    parts = ax.violinplot([dt1_data, dt3_data], positions=positions, widths=0.5,
                         showmeans=True, showmedians=True)

    # Color each violin
    for i, pc in enumerate(parts['bodies']):
        color = '#E65100' if i == 0 else '#1565C0'
        pc.set_facecolor(color)
        pc.set_alpha(0.7)
        pc.set_edgecolor('black')
        pc.set_linewidth(0.5)
    for partname in ('cbars', 'cmins', 'cmaxes', 'cmedians', 'cmeans'):
        if partname in parts:
            vp = parts[partname]
            vp.set_edgecolor('#333333')
            vp.set_linewidth(1.5)

 #   # Compute and display statistics
 #   stats_text = f"DT1\n"
 #   stats_text += f"  median={np.median(dt1_data):.2f} Wh\n"
 #   stats_text += f"  IQR=[{np.percentile(dt1_data, 25):.2f}, {np.percentile(dt1_data, 75):.2f}]\n"
 #   stats_text += f"  mean={np.mean(dt1_data):.2f} Wh\n"
 #   stats_text += f"DT3\n"
 #   stats_text += f"  median={np.median(dt3_data):.2f} Wh\n"
 #   stats_text += f"  IQR=[{np.percentile(dt3_data, 25):.2f}, {np.percentile(dt3_data, 75):.2f}]\n"
 #   stats_text += f"  mean={np.mean(dt3_data):.2f} Wh"
#
 #   ax.text(0.7, 0.97, stats_text, transform=ax.transAxes,
 #          fontsize=FONT_SIZE-2, verticalalignment='top', horizontalalignment='right',
 #          family='monospace')
#
    from matplotlib.lines import Line2D
    stats_text_dt1 = (
        f"DT1\n"
        f"  median={np.median(dt1_data):.2f} Wh\n"
        f"  IQR=[{np.percentile(dt1_data, 25):.2f}, "
        f"{np.percentile(dt1_data, 75):.2f}]\n"
        f"  mean={np.mean(dt1_data):.2f} Wh"
    )
    
    stats_text_dt3 = (
        f"DT3\n"
        f"  median={np.median(dt3_data):.2f} Wh\n"
        f"  IQR=[{np.percentile(dt3_data, 25):.2f}, "
        f"{np.percentile(dt3_data, 75):.2f}]\n"
        f"  mean={np.mean(dt3_data):.2f} Wh"
    )
    handles = [
        Line2D([], [], linestyle="none", label=stats_text_dt1),
        Line2D([], [], linestyle="none", label=stats_text_dt3),
    ]
    ax.legend(
        handles=handles,    ncols=1,
        fontsize=FONT_SIZE-2,
        bbox_to_anchor=(0.5, 0.27),
        loc="lower center",  
        framealpha=0,
        edgecolor=CGRID,
        labelspacing=0.2,
        handletextpad=0.2,
        columnspacing=0.2,
        borderpad=0.2,
        borderaxespad=0.1,
        handlelength=0.8
    )


    ax.set_xticks(positions)
    ax.set_xticklabels(['DT1', 'DT3'],)
    ax.set_ylabel('|Gap| (Wh)', labelpad=0)
    ax.tick_params(axis='both', pad=0)
    #ax.set_title('Per-cell-boundary gap distribution (pooled across all probability levels)')
    # ax.grid(True, alpha=0.3, axis='y')

    fig.tight_layout()
    fig.savefig(path)
    plt.close(fig)
    print(f'  [Battery Gap — Pooled] {path}')


def figure_battery_gap_prob_comparison(gap_data: Dict, path: str):
    """
    Probability-level comparison plot: For each probability level, show signed
    prediction errors (dt_predicted_end - pt_bat_actual_next) as paired violins.
    X-axis: stress probability level (0-50%).
    Y-axis: signed prediction error (positive = DT optimistic/dangerous).
    """
    import pandas as pd

    data = gap_data['data']
    if not data:
        print('  [Battery Gap] skipped — no trace data available')
        return

    df = pd.DataFrame(data)
    probs = gap_data['probs']

    fig, ax = plt.subplots(figsize=(FIG_W,FIG_H))

    pos = 1
    positions_dt1 = []
    positions_dt3 = []

    for prob in probs:
        df_prob = df[df['prob'] == prob]
        dt1_data = df_prob[df_prob['scenario'] == 'A']['abs_error'].values
        dt3_data = df_prob[df_prob['scenario'] == 'C']['abs_error'].values

        if len(dt1_data) > 0:
            positions_dt1.append(pos)
            parts = ax.violinplot([dt1_data], positions=[pos], widths=0.35,
                         showmeans=True, showmedians=False)
            for pc in parts['bodies']:
                pc.set_facecolor('#E65100')
                pc.set_alpha(0.7)
                pc.set_edgecolor('#E65100')
                pc.set_linewidth(1.0)
            for partname in ('cbars', 'cmins', 'cmaxes', 'cmeans'):
                if partname in parts:
                    parts[partname].set_color('#E65100')
                    parts[partname].set_linewidth(1.5)
            pos += 0.5

        if len(dt3_data) > 0:
            positions_dt3.append(pos)
            parts = ax.violinplot([dt3_data], positions=[pos], widths=0.35,
                         showmeans=True, showmedians=False)
            for pc in parts['bodies']:
                pc.set_facecolor('#1565C0')
                pc.set_alpha(0.7)
                pc.set_edgecolor('#1565C0')
                pc.set_linewidth(1.0)
            for partname in ('cbars', 'cmins', 'cmaxes', 'cmeans'):
                if partname in parts:
                    parts[partname].set_color('#1565C0')
                    parts[partname].set_linewidth(1.5)
            pos += 1.0

    # Set x-axis labels
    prob_labels = [f'{p:.0%}' for p in probs]
    ax.set_xticks([(positions_dt1[i] + positions_dt3[i]) / 2 for i in range(len(probs))])
    ax.set_xticklabels(prob_labels, fontsize=FONT_SIZE)
    
    ax.set_ylabel('|Gap| (Wh)', labelpad=0)
    ax.grid(True, alpha=0.3, axis='y')

    # Add legend
    handles = [
        mpatches.Patch(facecolor='#E65100', label='DT1'),
        mpatches.Patch(facecolor='#1565C0', label="DT3")
    ]
    ax.legend(handles=handles, loc='upper left',   ncols=1,
        fontsize=FONT_SIZE,
        framealpha=0.8,
        edgecolor=CGRID,
        labelspacing=0.2,
        handletextpad=0.2,
        columnspacing=0.2,
        borderpad=0.2,
        borderaxespad=0.1,
        handlelength=0.8)
    ax.tick_params(axis='both', pad=0)
    fig.tight_layout()
    fig.savefig(path)
    plt.close(fig)
    print(f'  [Battery Gap — Prob Comparison] {path}')


def figure_battery_gap_line_plot(gap_data: Dict, path: str):
    """
    Line plot with error bands: Mean gap ± std dev for each probability level.

    Shows how DT1 and DT3 gap trends change as stress probability increases.
    """
    import pandas as pd

    data = gap_data['data']
    if not data:
        print('  [Battery Gap] skipped — no trace data available')
        return

    df = pd.DataFrame(data)
    probs = sorted(gap_data['probs'])

    fig, ax = plt.subplots(figsize=(FIG_W, FIG_H))

    dt1_means = []
    dt1_stds = []
    dt3_means = []
    dt3_stds = []

    for prob in probs:
        df_prob = df[df['prob'] == prob]
        dt1_data = df_prob[df_prob['scenario'] == 'A']['abs_error'].values
        dt3_data = df_prob[df_prob['scenario'] == 'C']['abs_error'].values

        dt1_means.append(np.mean(dt1_data) if len(dt1_data) > 0 else 0)
        dt1_stds.append(np.std(dt1_data) if len(dt1_data) > 0 else 0)
        dt3_means.append(np.mean(dt3_data) if len(dt3_data) > 0 else 0)
        dt3_stds.append(np.std(dt3_data) if len(dt3_data) > 0 else 0)

    prob_pcts = np.array([p * 100 for p in probs])

    # Plot with error bands
    ax.plot(prob_pcts, dt1_means, color='#E65100', linewidth=1, marker='o',
            markersize=2, label='DT1', zorder=3)
    ax.fill_between(prob_pcts,
                     np.array(dt1_means) - np.array(dt1_stds),
                     np.array(dt1_means) + np.array(dt1_stds),
                     color='#E65100', alpha=0.2, zorder=1)

    ax.plot(prob_pcts, dt3_means, color='#1565C0', linewidth=1, marker='s',
            markersize=2, label='DT3', zorder=3)
    ax.fill_between(prob_pcts,
                     np.array(dt3_means) - np.array(dt3_stds),
                     np.array(dt3_means) + np.array(dt3_stds),
                     color='#1565C0', alpha=0.2, zorder=1)

    #ax.set_xlabel('Stress Probability Level (%)', fontsize=FONT_SIZE, fontweight='bold')
    ax.set_ylabel('Mean |Gap| (Wh)', labelpad=0)
    ax.set_xticks(prob_pcts)
    ax.tick_params(axis='both', pad=0)
    ax.set_xticklabels([f'{int(p)}%' for p in prob_pcts])
    ax.grid(True, alpha=0.3, linestyle='--')
    ax.legend(ncols=1,
        fontsize=FONT_SIZE,
        framealpha=0.8,
        edgecolor=CGRID,
        labelspacing=0.2,
        handletextpad=0.2,
        columnspacing=0.2,
        borderpad=0.2,
        borderaxespad=0.1,
        handlelength=0.8)
    
    # Add value labels on points
    diffs = np.array(dt1_means) - np.array(dt3_means)
    i=1
    for x, diff,y in zip(prob_pcts, diffs, dt1_means):
            if i%2==0:
                a = -1
            else:
                a=1
            ax.annotate(f'{diff:.1f}%' if diff > 0 else "0%", xy=(x+a, y), xytext=(0, 8),
                       textcoords='offset points', ha='center', fontsize=FONT_SIZE-1,
                       )
            i+=1

    fig.tight_layout()
    fig.savefig(path, dpi=300)
    plt.close(fig)
    print(f'  [Battery Gap — Line Plot] {path}')


def figure_battery_gap_boxplot(gap_data: Dict, path: str):
    """
    Boxplot comparison: Shows median, quartiles, and outliers per probability level.

    Cleaner alternative to violin plots for comparing DT1 vs DT3.
    """
    import pandas as pd

    data = gap_data['data']
    if not data:
        print('  [Battery Gap] skipped — no trace data available')
        return

    df = pd.DataFrame(data)
    probs = sorted(gap_data['probs'])

    fig, ax = plt.subplots(figsize=(12, 6))

    # Prepare data for boxplot
    positions = []
    boxplot_data = []
    box_colors = []
    pos = 1

    for prob in probs:
        df_prob = df[df['prob'] == prob]
        dt1_data = df_prob[df_prob['scenario'] == 'A']['abs_error'].values
        dt3_data = df_prob[df_prob['scenario'] == 'C']['abs_error'].values

        if len(dt1_data) > 0:
            positions.append(pos)
            boxplot_data.append(dt1_data)
            box_colors.append('#E65100')
            pos += 1

        if len(dt3_data) > 0:
            positions.append(pos)
            boxplot_data.append(dt3_data)
            box_colors.append('#1565C0')
            pos += 1

        pos += 0.5  # Space between probability groups

    bp = ax.boxplot(boxplot_data, positions=positions, widths=0.6,
                    patch_artist=True, showmeans=True,
                    meanprops=dict(marker='D', markerfacecolor='#FFD700',
                                  markeredgecolor='black', markersize=6))

    # Color the boxes
    for patch, color in zip(bp['boxes'], box_colors):
        patch.set_facecolor(color)
        patch.set_alpha(0.7)

    # Format whiskers and caps
    for whisker in bp['whiskers']:
        whisker.set(linewidth=1.2, color='#333333')
    for cap in bp['caps']:
        cap.set(linewidth=1.2, color='#333333')
    for median in bp['medians']:
        median.set(linewidth=1.5, color='#333333')

    # Set x-axis labels
    prob_labels = [f'{p:.0%}' for p in probs]
    tick_positions = []
    for i in range(len(probs)):
        start = i * (2 + 0.5)
        tick_positions.append(start + 1.5)

    ax.set_xticks(tick_positions)
    ax.set_xticklabels(prob_labels, fontsize=FONT_SIZE)
    ax.set_xlabel('Stress Probability Level', fontsize=FONT_SIZE, fontweight='bold')
    ax.set_ylabel('|Gap| (Wh)', fontsize=FONT_SIZE, fontweight='bold')
    ax.set_title('Per-Cell-Boundary Gap Distribution by Probability Level\nBoxplot: boxes show IQR, line is median, diamond is mean',
                 fontsize=FONT_SIZE, fontweight='bold', pad=15)
    ax.grid(True, alpha=0.3, axis='y', linestyle='--')

    # Add legend
    handles = [
        mpatches.Patch(facecolor='#E65100', label='DT1 (non-aligned)'),
        mpatches.Patch(facecolor='#1565C0', label='DT3 (aligned)'),
        mlines.Line2D([], [], color='#FFD700', marker='D', linestyle='None',
                     markersize=6, markeredgecolor='black', label='Mean')
    ]
    ax.legend(handles=handles, fontsize=FONT_SIZE, loc='upper left', framealpha=0.95)

    fig.tight_layout()
    fig.savefig(path, dpi=300)
    plt.close(fig)
    print(f'  [Battery Gap — Boxplot] {path}')


def figure_battery_gap_relative_improvement(gap_data: Dict, path: str):
    """
    Relative improvement metric: how much better is DT3 vs DT1 (in %).

    improvement = (|gap_DT1| - |gap_DT3|) / |gap_DT1| × 100%

    Shows the percentage reduction in prediction error when using aligned DT.
    """
    import pandas as pd

    data = gap_data['data']
    if not data:
        print('  [Battery Gap] skipped — no trace data available')
        return

    df = pd.DataFrame(data)
    probs = sorted(gap_data['probs'])

    # Calculate paired improvements: for each (mission, cell), compare DT1 vs DT3
    improvements = []
    for prob in probs:
        df_prob = df[df['prob'] == prob]

        # Group by mission and cell to get paired observations
        dt1_by_mc = {(r['mission_idx'], r['cell_idx']): r['abs_error']
                     for _, r in df_prob[df_prob['scenario'] == 'A'].iterrows()}
        dt3_by_mc = {(r['mission_idx'], r['cell_idx']): r['abs_error']
                     for _, r in df_prob[df_prob['scenario'] == 'C'].iterrows()}

        common_keys = set(dt1_by_mc.keys()) & set(dt3_by_mc.keys())

        prob_improvements = []
        for key in common_keys:
            gap_dt1 = dt1_by_mc[key]
            gap_dt3 = dt3_by_mc[key]

            # Avoid division by zero
            if gap_dt1 > 0.001:  # Only if DT1 has measurable gap
                improvement = ((gap_dt1 - gap_dt3) / gap_dt1) * 100
                prob_improvements.append(improvement)
            elif gap_dt1 <= 0.001 and gap_dt3 <= 0.001:
                # Both near zero, call it 0% improvement (no error to improve)
                prob_improvements.append(0)

        if prob_improvements:
            improvements.append({
                'prob': prob,
                'mean': np.mean(prob_improvements),
                'std': np.std(prob_improvements),
                'n': len(prob_improvements)
            })

    fig, ax = plt.subplots(figsize=(FIG_W, FIG_H))

    if improvements:
        imp_df = pd.DataFrame(improvements)
        prob_pcts = np.array([p * 100 for p in imp_df['prob'].values])

        # Plot improvement with error bands
        ax.plot(prob_pcts, imp_df['mean'], color='#00695C', linewidth=1,
                marker='o', markersize=2, label='Mean improvement', zorder=3)
        ax.fill_between(prob_pcts,
                        imp_df['mean'] - imp_df['std'],
                        imp_df['mean'] + imp_df['std'],
                        color='#00695C', alpha=0.2, zorder=1, label='±1 std dev')

        # Add zero line
        ax.axhline(y=0, color='#666666', linestyle='--', linewidth=1.5, alpha=0.7)

        #ax.set_xlabel('Stress Probability Level (%)',)
        #ax.set_ylabel('Relative Improvement (%)', )
        #ax.set_title('DT3 Advantage: Relative Improvement Over DT1\nimprovement = (|gap_DT1| - |gap_DT3|) / |gap_DT1| × 100%',
        #            fontsize=FONT_SIZE, fontweight='bold', #pad=15)
        ax.set_xticks(prob_pcts)
        ax.set_xticklabels([f'{int(p)}%' for p in prob_pcts], fontsize=FONT_SIZE)
        ax.tick_params(axis='y', labelsize=FONT_SIZE)
        ax.grid(True, alpha=0.3, linestyle='--')
        ax.legend(ncols=1,
        fontsize=FONT_SIZE,
        framealpha=0.8,
        edgecolor=CGRID,
        labelspacing=0.2,
        handletextpad=0.2,
        columnspacing=0.2,
        borderpad=0.2,
        borderaxespad=0.1,
        handlelength=0.8)

        # Add value labels on points
        for x, y in zip(prob_pcts, imp_df['mean']):
            ax.annotate(f'{y:.1f}%' if y > 0 else "0%", xy=(x, y), xytext=(0, 8),
                       textcoords='offset points', ha='center', fontsize=FONT_SIZE,
                       )

    fig.tight_layout()
    fig.savefig(path, dpi=300)
    plt.close(fig)
    print(f'  [Battery Gap — Relative Improvement] {path}')


def figure_battery_gap_cdf(gap_data: Dict, path: str):
    """
    Cumulative Distribution Function (CDF) comparison.

    For each probability level, plot CDF curves for DT1 vs DT3.
    X-axis: gap (Wh), Y-axis: proportion of observations with gap ≤ X
    """
    import pandas as pd

    data = gap_data['data']
    if not data:
        print('  [Battery Gap] skipped — no trace data available')
        return

    df = pd.DataFrame(data)
    probs = sorted(gap_data['probs'])

    # Create 2 rows × 3 cols for 6 probability levels
    fig, axes = plt.subplots(2, 3, figsize=(14, 8))
    axes = axes.flatten()

    for idx, prob in enumerate(probs):
        ax = axes[idx]
        df_prob = df[df['prob'] == prob]

        dt1_data = df_prob[df_prob['scenario'] == 'A']['abs_error'].values
        dt3_data = df_prob[df_prob['scenario'] == 'C']['abs_error'].values

        # Compute empirical CDF
        if len(dt1_data) > 0:
            dt1_sorted = np.sort(dt1_data)
            dt1_cdf = np.arange(1, len(dt1_sorted) + 1) / len(dt1_sorted)
            ax.plot(dt1_sorted, dt1_cdf, color='#E65100', linewidth=2.5,
                   label=f'DT1 (n={len(dt1_data)})', zorder=2)

        if len(dt3_data) > 0:
            dt3_sorted = np.sort(dt3_data)
            dt3_cdf = np.arange(1, len(dt3_sorted) + 1) / len(dt3_sorted)
            ax.plot(dt3_sorted, dt3_cdf, color='#1565C0', linewidth=2.5,
                   label=f'DT3 (n={len(dt3_data)})', zorder=2)

        ax.set_xlabel('Gap (% of battery)', fontsize=FONT_SIZE)
        ax.set_ylabel('Cumulative Probability', fontsize=FONT_SIZE)
        ax.set_title(f'Prob = {prob:.0%}', fontsize=FONT_SIZE, fontweight='bold')
        ax.grid(True, alpha=0.3)
        ax.legend(fontsize=FONT_SIZE, loc='lower right')
        ax.set_ylim([0, 1.05])

    fig.suptitle('Cumulative Distribution Functions (CDF) by Probability Level\nX: Gap (Wh), Y: Cumulative % of observations',
                fontsize=FONT_SIZE, fontweight='bold', y=0.995)
    fig.tight_layout()
    fig.savefig(path, dpi=300)
    plt.close(fig)
    print(f'  [Battery Gap — CDF] {path}')


def table_battery_gap_summary(gap_data: Dict, path: str):
    """
    Summary table: For each probability level, report mean |gap_DT1|,
    mean |gap_DT3|, and percentage of observations where DT3 is closer.
    """
    import pandas as pd

    data = gap_data['data']
    if not data:
        print('  [Battery Gap] skipped — no trace data available')
        return

    df = pd.DataFrame(data)
    probs = gap_data['probs']

    rows = []
    for prob in probs:
        df_prob = df[df['prob'] == prob]

        dt1_data = df_prob[df_prob['scenario'] == 'A']['abs_error'].values
        dt3_data = df_prob[df_prob['scenario'] == 'C']['abs_error'].values

        # Compute comparison: percentage where DT3 is closer
        # Align data by mission and cell
        dt1_by_mc = {(r['mission_idx'], r['cell_idx']): r['abs_error']
                     for _, r in df_prob[df_prob['scenario'] == 'A'].iterrows()}
        dt3_by_mc = {(r['mission_idx'], r['cell_idx']): r['abs_error']
                     for _, r in df_prob[df_prob['scenario'] == 'C'].iterrows()}

        # Find common mission-cell pairs
        common_keys = set(dt1_by_mc.keys()) & set(dt3_by_mc.keys())
        better_count = sum(1 for key in common_keys if dt3_by_mc[key] < dt1_by_mc[key])
        better_pct = (better_count / len(common_keys) * 100) if common_keys else 0

        mean_dt1 = np.mean(dt1_data) if len(dt1_data) > 0 else 0
        mean_dt3 = np.mean(dt3_data) if len(dt3_data) > 0 else 0

        rows.append({
            'Prob': f'{prob:.0%}',
            'N_obs': len(common_keys),
            'Mean|gap_DT1|': f'{mean_dt1:.2f}',
            'Mean|gap_DT3|': f'{mean_dt3:.2f}',
            'DT3_better_%': f'{better_pct:.1f}%'
        })

    # Add pooled row
    dt1_all = df[df['scenario'] == 'A']['abs_error'].values
    dt3_all = df[df['scenario'] == 'C']['abs_error'].values

    dt1_by_mc_all = {(r['mission_idx'], r['cell_idx']): r['abs_error']
                     for _, r in df[df['scenario'] == 'A'].iterrows()}
    dt3_by_mc_all = {(r['mission_idx'], r['cell_idx']): r['abs_error']
                     for _, r in df[df['scenario'] == 'C'].iterrows()}

    common_keys_all = set(dt1_by_mc_all.keys()) & set(dt3_by_mc_all.keys())
    better_count_all = sum(1 for key in common_keys_all if dt3_by_mc_all[key] < dt1_by_mc_all[key])
    better_pct_all = (better_count_all / len(common_keys_all) * 100) if common_keys_all else 0

    rows.append({
        'Prob': 'Pooled (all)',
        'N_obs': len(common_keys_all),
        'Mean|gap_DT1|': f'{np.mean(dt1_all):.2f}',
        'Mean|gap_DT3|': f'{np.mean(dt3_all):.2f}',
        'DT3_better_%': f'{better_pct_all:.1f}%'
    })

    summary_df = pd.DataFrame(rows)

    # Print to console
    print(f'\n[Battery Gap Summary Table]')
    print(summary_df.to_string(index=False))

    # Save to CSV
    summary_df.to_csv(path, index=False)
    print(f'  [Battery Gap — Summary] {path}')
