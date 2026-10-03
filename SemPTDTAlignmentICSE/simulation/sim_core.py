"""Core simulation logic — run_mission and run_sweep."""
import numpy as np
from typing import List, Tuple, Optional
import config as config
import energy as energy
import monitors as monitors


def run_mission(scenario: str,
                motor_failure_cell: Optional[int],
                rng: np.random.Generator,
                clog_prob: float = 0.10
                ) -> Tuple[List[config.CheckPoint], bool]:
    """
    scenario 'A': non-aligned DT, no monitor
    scenario 'B': non-aligned DT + Monitor B (aborts immediately on fire)
    scenario 'C': aligned DT with speed boost
    """
    assert scenario in ('A','B','C')
    aligned   = scenario == 'C'
    monitored = scenario == 'B'

    pt_bat=config.TOT_BAT; dt_budget=config.TOT_BAT
    motor_degraded=False; cells_done=0; sim_time=0.0
    trace: List[config.CheckPoint] = []
    safety_violated=False; speed_factor=1.0

    # mission_start
    pt_bat    -= energy.compute_flight_energy(False, rng)
    dt_budget -= config.FLY_COST
    sim_time  += config.FLIGHT_TIME_S

    for cell in range(config.NUM_HEC):

        # Motor failure
        if motor_failure_cell == cell and not motor_degraded:
            motor_degraded = True

        # arrive! — blend PT sensor into DT budget
        pt_noisy_arrive = pt_bat + rng.normal(0, config.BAT_SENSOR_NOISE_STD)
        # Both DTs receive identical PT battery telemetry (ALPHA=1.0).
        # The ONLY difference is the consumption formula (cons_est):
        # aligned uses correct formula (includes motor_delta + speed ratio),
        # non-aligned uses wrong formula (no motor_delta, no speed boost).
        # This is the fair comparison: same data, different interpretation.
        ALPHA = 1.0
        dt_budget = ALPHA*pt_noisy_arrive + (1-ALPHA)*dt_budget

        # Speed command (aligned only)
        speed_factor = (energy.dt_compute_speed_factor(motor_degraded)
                        if aligned else 1.0)

        # cons_est
        if aligned:
            cons_est = energy.dt_predict_cons_est(cell, motor_degraded, speed_factor)
        else:
            cons_est = energy.dt_nonaligned_predict_cons_est(cell, motor_degraded)

        # DT DECISION: greedy — can I spray this cell and return?
        # Aligned uses correct cons_est (includes motor_delta + speed ratio).
        # Non-aligned uses wrong cons_est (no motor_delta, no speed boost).
        can_continue  = dt_budget >= (cons_est + config.RET_COST)
        dt_decision   = ("resume_fast" if can_continue and speed_factor > 1.0
                         else "resume"  if can_continue
                         else "abort")
        dt_predicted_end = dt_budget - cons_est - config.FLY_COST

        if not can_continue:
            # Proactive abort
            pt_bat    -= config.RET_COST
            dt_budget -= config.RET_COST
            sim_time  += config.FLIGHT_TIME_S
            pt_n = pt_bat + rng.normal(0, config.BAT_SENSOR_NOISE_STD)
            depleted = int(pt_bat) <= 0 and cells_done < config.NUM_HEC
            
            trace.append(config.CheckPoint(
                sim_time=sim_time, cell_idx=cell,
                pt_bat=pt_bat, pt_bat_noisy=pt_n, dt_budget=dt_budget,
                dt_decision="abort", speed_factor=speed_factor,
                cons_actual=0.0, cons_est=cons_est,
                motor_degraded=motor_degraded,
                monitor_a=monitors.monitor_a_check(pt_n, dt_budget),
                monitor_b=monitors.monitor_b_check(pt_bat, cell, motor_degraded),
                fidelity_gap=abs(pt_n-dt_budget), depleted=depleted,
                event=f"abort_cmd cell={cell}",
                dt_predicted_end=dt_predicted_end
            ))
            break

        # Nozzle clog
        if rng.random() < clog_prob:
            pt_bat -= config.CLEANING_COST; sim_time += 120.0

        # Physical spray
        E_cell, t_spray = energy.compute_physical_consumption(
            cell, motor_degraded, rng, speed_factor
        )

        # Passive telemetry during spray
        n_obs = max(0, int(t_spray/config.TELEMETRY_INTERVAL_S)-1)
        for k in range(1, n_obs+1):
            obs_frac     = k/(n_obs+1)
            obs_time     = sim_time + obs_frac*t_spray
            obs_pt_bat   = pt_bat - obs_frac*E_cell
            obs_pt_noisy = obs_pt_bat + rng.normal(0, config.BAT_SENSOR_NOISE_STD)
            obs_dt_model = dt_budget - obs_frac*cons_est
            obs_dt_blend = ALPHA*obs_pt_noisy + (1-ALPHA)*obs_dt_model
            mon_b = monitors.monitor_b_check(obs_pt_bat, cell, motor_degraded)
            trace.append(config.CheckPoint(
                sim_time=obs_time, cell_idx=cell,
                pt_bat=obs_pt_bat, pt_bat_noisy=obs_pt_noisy, dt_budget=obs_dt_blend,
                dt_decision="observe", speed_factor=speed_factor,
                cons_actual=0.0, cons_est=cons_est,
                motor_degraded=motor_degraded,
                monitor_a=monitors.monitor_a_check(obs_pt_noisy, obs_dt_blend),
                monitor_b=mon_b,
                fidelity_gap=abs(obs_pt_noisy-obs_dt_blend), depleted=False,
                event=f"telemetry cell={cell} obs={k}",
                dt_predicted_end=obs_dt_blend-cons_est-config.RET_COST
            ))
            # Scenario B: monitor fires → mid-spray abort
            if mon_b and monitored:
                pt_bat -= config.RET_COST
                sim_time += config.FLIGHT_TIME_S
                pt_n = pt_bat + rng.normal(0,config.BAT_SENSOR_NOISE_STD)
                trace.append(config.CheckPoint(
                    sim_time=sim_time, cell_idx=cell,
                    pt_bat=pt_bat, pt_bat_noisy=pt_n, dt_budget=obs_dt_blend,
                    dt_decision="abort", speed_factor=speed_factor,
                    cons_actual=obs_frac*E_cell, cons_est=cons_est,
                    motor_degraded=motor_degraded,
                    monitor_a=False, monitor_b=True,
                    fidelity_gap=abs(obs_pt_noisy-obs_dt_blend), depleted=False,
                    event=f"monitor_b_abort cell={cell}",
                    dt_predicted_end=0.0
                ))
                return trace, False  # safe abort

        # Spray done
        pt_bat   -= E_cell
        sim_time += t_spray
        cells_done += 1

        pt_n = pt_bat + rng.normal(0, config.BAT_SENSOR_NOISE_STD)
        depleted = int(pt_bat) <= 0 and cells_done < config.NUM_HEC
        if depleted:
            pt_bat = 0.0; safety_violated = True

        mon_a = monitors.monitor_a_check(pt_n, dt_budget)
        mon_b = monitors.monitor_b_check(pt_bat, cell, motor_degraded)
        dt_budget_new = ALPHA*pt_n + (1-ALPHA)*(dt_budget-cons_est)

        trace.append(config.CheckPoint(
            sim_time=sim_time, cell_idx=cell,
            pt_bat=pt_bat, pt_bat_noisy=pt_n, dt_budget=dt_budget_new,
            dt_decision=dt_decision, speed_factor=speed_factor,
            cons_actual=E_cell, cons_est=cons_est,
            motor_degraded=motor_degraded,
            monitor_a=mon_a, monitor_b=mon_b,
            fidelity_gap=abs(pt_n-dt_budget_new), depleted=depleted,
            event=("DEPLETED" if depleted else f"spray_done cell={cell}"),
            dt_predicted_end=pt_n-cons_est-config.RET_COST
        ))
        dt_budget = dt_budget_new

        if depleted: break

        # Flight to next cell
        E_leg=energy.compute_flight_energy(motor_degraded, rng)
        t_leg_start=sim_time; bat_leg_start=pt_bat
        pt_bat    -= E_leg; dt_budget -= config.FLY_COST
        sim_time  += config.FLIGHT_TIME_S

        # Flight telemetry
        n_flt = max(1, int(config.FLIGHT_TIME_S/(config.TELEMETRY_INTERVAL_S*2)))
        for k in range(1, n_flt+1):
            obs_frac     = k/(n_flt+1)
            obs_pt_bat   = bat_leg_start - obs_frac*E_leg
            obs_pt_noisy = obs_pt_bat + rng.normal(0, config.BAT_SENSOR_NOISE_STD)
            obs_dt_model = dt_budget + config.FLY_COST*(1-obs_frac)
            obs_dt_blend = ALPHA*obs_pt_noisy + (1-ALPHA)*obs_dt_model
            trace.append(config.CheckPoint(
                sim_time=t_leg_start+obs_frac*config.FLIGHT_TIME_S,
                cell_idx=cell,
                pt_bat=obs_pt_bat, pt_bat_noisy=obs_pt_noisy, dt_budget=obs_dt_blend,
                dt_decision="observe", speed_factor=speed_factor,
                cons_actual=0.0, cons_est=cons_est,
                motor_degraded=motor_degraded,
                monitor_a=monitors.monitor_a_check(obs_pt_noisy, obs_dt_blend),
                monitor_b=monitors.monitor_b_check(obs_pt_bat, cell, motor_degraded),
                fidelity_gap=abs(obs_pt_noisy-obs_dt_blend), depleted=False,
                event=f"flight_tel cell={cell}->{cell+1}",
                dt_predicted_end=obs_dt_blend-cons_est-config.RET_COST
            ))

        if cell == config.NUM_HEC-1:
            pt_bat -= config.RET_COST; dt_budget -= config.RET_COST
            sim_time += config.FLIGHT_TIME_S

    return trace, safety_violated


def run_sweep(n:int, seed:int=0, fail_prob:float=0.3, capture_traces:bool=False):
    results = {'A':[], 'B':[], 'C':[]}
    for i in range(n):
        rng_cfg   = np.random.default_rng(seed+i)
        fail_cell = (int(rng_cfg.integers(0, config.NUM_HEC))
                     if rng_cfg.random() < fail_prob else None)
        for sc in ('A','B','C'):
            # Deterministic per-scenario stream: ord(sc) avoids Python's
            # per-process hash randomization (PYTHONHASHSEED), so results are
            # reproducible run-to-run without setting any environment variable.
            trace, violated = run_mission(
                sc, fail_cell,
                np.random.default_rng([seed + i, ord(sc)])
            )
            cells_done = sum(1 for cp in trace
                             if cp.dt_decision in ('resume','resume_fast')
                             and not cp.depleted)
            mid_abort  = any('monitor_b_abort' in cp.event for cp in trace)
            abort_t    = next((cp.sim_time for cp in trace
                               if cp.dt_decision=='abort'), None)
            viol_t     = next((cp.sim_time for cp in trace
                               if cp.depleted), None)
            mon_b_t    = next((cp.sim_time for cp in trace
                               if cp.monitor_b), None)
            final_bat  = trace[-1].pt_bat if trace else 0.0
            depleted = next((cp.sim_time for cp in trace if cp.depleted), False)
            mission_complete = cells_done == config.NUM_HEC and not violated and not depleted
            result = {
                'violated': violated, 'fail_cell': fail_cell,
                'cells_done': cells_done, 'mid_abort': mid_abort,
                'abort_t': abort_t, 'viol_t': viol_t, 'mon_b_t': mon_b_t,
                'final_battery': final_bat, 'mission_complete': mission_complete,
                'depleted': depleted
            }
            if capture_traces:
                result['trace'] = trace
            results[sc].append(result)
    return results
