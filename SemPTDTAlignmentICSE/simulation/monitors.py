"""
Runtime monitors — both watch PT telemetry directly.
"""
import config as config
import energy as energy
import numpy as np


def monitor_a_check(pt_bat_noisy: float, dt_budget: float) -> bool:
    """
    Monitor A: cross-trace fidelity (Muñoz et al. style).
    Original formulation: fires when |PT_sensor - DT_budget| > threshold.

    NOTE: with ALPHA=1.0 (DT syncs to PT sensor), this gap is always near
    zero and Monitor A is trivially silent. The meaningful Monitor A for
    this simulation is monitor_a_prediction_check(), which compares the
    DT's one-step-ahead prediction against the actual outcome.
    Kept here for completeness and backward compatibility.
    """
    return abs(pt_bat_noisy - dt_budget) > config.MONITOR_A_THRESHOLD


def monitor_a_prediction_check(dt_predicted_end: float,
                                 pt_bat_actual_next: float) -> bool:
    """
    Monitor A (reframed as prediction error monitor).

    Fires when the DT's prediction of battery after this cell is
    significantly MORE optimistic than reality:
      signed_error = dt_predicted_end - pt_bat_actual_next
      fires when signed_error > MONITOR_A_THRESHOLD

    Positive error = DT overestimated remaining battery = DANGEROUS.
    Negative error = DT underestimated remaining battery = SAFE/conservative.

    Non-aligned DT: systematically positive after motor failure
      (missing motor_delta -> thinks each cell costs less -> predicts
       higher battery than actually remains).
    Aligned DT: systematically negative (conservative cons_est ->
       predicts lower battery than actually remains -> never fires).

    This is the Muñoz spirit: detect when PT and DT diverge in a way
    that indicates misalignment. Here divergence is in prediction space
    rather than current-state space.
    """
    signed_error = dt_predicted_end - pt_bat_actual_next
    return signed_error > config.MONITOR_A_THRESHOLD


def monitor_b_check(pt_bat: float, cell: int, motor_degraded: bool) -> bool:
    """
    Monitor B: safety property monitor on PT telemetry.
    Fires when PT cannot safely complete current cell and return.
    Uses correct consumption model, independent of which DT is running.
    Reactive: fires during execution after commitment.
    """
    cons_est_correct = energy.dt_predict_cons_est(cell, motor_degraded,
                                                   speed_factor=1.0)
    return pt_bat < (cons_est_correct + config.RET_COST)
