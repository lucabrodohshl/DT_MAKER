"""
Energy model — physical consumption, DT predictions, speed modulation,
continuous trace builder.
"""
import numpy as np
from typing import List, Tuple
import config as config


def compute_physical_consumption(cell: int,
                                  motor_degraded: bool,
                                  rng: np.random.Generator,
                                  speed_factor: float = 1.0
                                  ) -> Tuple[float, float]:
    """
    Physical energy for spraying cell at speed_factor.

    Faster speed → less hover time per cell → less total energy.
    This is the key physical insight: hover power (~1000W) dominates,
    so time reduction from higher speed outweighs aerodynamic overhead increase.

    Returns (E_cell_Wh, t_spray_s).
    """
    density  = config.FIELD_CROP_DENSITY[cell]
    humidity = config.FIELD_HUMIDITY[cell]
    pressure = config.FIELD_PRESSURE[cell]
    windy    = (humidity >= config.WIND_HUM_THRESH and
                pressure <= config.WIND_PRES_THRESH)
    wind_factor = 1.15 if windy else 1.0

    t_nom = (config.SPRAY_TIME_MIN_S
             + density * (config.SPRAY_TIME_MAX_S - config.SPRAY_TIME_MIN_S)
             * wind_factor)
    t_nom += rng.normal(0, config.SPRAY_TIME_MIN_S * 0.03)
    t_nom  = float(np.clip(t_nom,
                           config.SPRAY_TIME_MIN_S * 0.95,
                           config.SPRAY_TIME_MAX_S * 1.05))

    sf      = float(np.clip(speed_factor, 0.6, 2.0))
    t_spray = t_nom / sf

    motor_factor = config.MOTOR_DEGRAD_FACTOR if motor_degraded else 1.0
    P_hover  = config.P_HOVER_W * motor_factor
    P_flight = config.P_HOVER_W * config.FWD_FLIGHT_OVERHEAD * sf**2
    P_pump   = (config.P_PUMP_MIN_W
                + density * (config.P_PUMP_MAX_W - config.P_PUMP_MIN_W))
    P_pump  *= sf
    if windy:
        P_pump *= 1.12

    E_cell = (P_hover + P_flight + P_pump) * t_spray / 3600.0
    upper  = config.CONS_DEGRADED * 1.1 if motor_degraded else config.CONS_NOM * 1.1
    E_cell = float(np.clip(E_cell, config.CONS_MIN * 0.5, upper))

    return E_cell, t_spray


def compute_flight_energy(motor_degraded: bool,
                           rng: np.random.Generator) -> float:
    motor_factor = config.MOTOR_DEGRAD_FACTOR if motor_degraded else 1.0
    E = (config.P_FLIGHT_W * motor_factor * config.FLIGHT_TIME_S) / 3600.0
    E += rng.normal(0, 0.3)
    return float(np.clip(E, config.FLY_COST * 0.9, config.FLY_COST * 1.1))


def dt_predict_cons_est(cell: int, motor_degraded: bool,
                         speed_factor: float = 1.0) -> float:
    """
    Aligned DT: field-informed consumption estimate.
    Correctly incorporates motor_delta when degraded.
    Correctly applies speed energy ratio.
    """
    density  = config.FIELD_CROP_DENSITY[cell]
    humidity = config.FIELD_HUMIDITY[cell]
    pressure = config.FIELD_PRESSURE[cell]
    windy    = (humidity >= config.WIND_HUM_THRESH and
                pressure <= config.WIND_PRES_THRESH)
    wind_factor = 1.15 if windy else 1.05
    cons_base   = float(np.clip(
        config.CONS_MIN + density * (config.CONS_NOM - config.CONS_MIN) * wind_factor,
        config.CONS_MIN, config.CONS_NOM
    ))
    if motor_degraded:
        cons_base = float(np.clip(
        (cons_base + config.MOTOR_DELTA) * config.SAFETY_MARGIN,
        config.CONS_MIN + config.MOTOR_DELTA,
        config.CONS_DEGRADED))

    sf    = float(np.clip(speed_factor, 0.6, 2.0))
    ratio = _speed_energy_ratio(sf)
    return cons_base * ratio


def dt_nonaligned_predict_cons_est(cell: int, motor_degraded: bool) -> float:
    """
    Non-aligned DT: WRONG estimate.
    BUG 1: ignores motor_delta after failure.
    BUG 2: never issues speed boost command.
    Same field knowledge as aligned — divergence is purely in formula.
    """
    density  = config.FIELD_CROP_DENSITY[cell]
    humidity = config.FIELD_HUMIDITY[cell]
    pressure = config.FIELD_PRESSURE[cell]
    windy    = (humidity >= config.WIND_HUM_THRESH and
                pressure <= config.WIND_PRES_THRESH)
    wind_factor = 1.10 if windy else 1.0
    cons_base   = config.CONS_MIN + density * (config.CONS_NOM - config.CONS_MIN) * wind_factor
    return float(np.clip(cons_base, config.CONS_MIN, config.CONS_NOM))


def _speed_energy_ratio(sf: float) -> float:
    sf = float(np.clip(sf, 0.6, 2.0))
    P_fl = config.P_HOVER_W * config.FWD_FLIGHT_OVERHEAD * sf**2
    P_pu = config.P_PUMP_AVG_W * sf
    E_sf  = (config.P_HOVER_W + P_fl + P_pu) / sf
    E_nom = config.P_HOVER_W + config.P_HOVER_W * config.FWD_FLIGHT_OVERHEAD + config.P_PUMP_AVG_W
    return float(E_sf / E_nom)


def dt_compute_speed_factor(motor_degraded: bool) -> float:
    """
    Aligned DT speed command: boost when degraded, nominal otherwise.
    Speed boost saves ~17% energy per cell when motor is degraded.
    Non-aligned DT never calls this.
    """
    return config.SPEED_BOOST_FACTOR if motor_degraded else 1.0


def battery_segment_curve(b_start: float, b_end: float,
                           n_pts: int, motor_degraded: bool) -> np.ndarray:
    t_norm = np.linspace(0.0, 1.0, n_pts)
    linear = b_start + (b_end - b_start) * t_norm
    if not motor_degraded:
        drop = abs(b_end - b_start)
        arch = config.ENERGY_ARCH * drop * np.sin(np.pi * t_norm)
        return linear + arch
    return linear


def build_continuous_trace(checkpoints: List[config.CheckPoint],
                            rng: np.random.Generator
                            ) -> Tuple[np.ndarray, np.ndarray]:
    if len(checkpoints) < 2:
        return np.array([0.0]), np.array([config.TOT_BAT])

    t0_approx = checkpoints[0].sim_time - config.FLIGHT_TIME_S
    b0_approx = checkpoints[0].pt_bat + config.FLY_COST

    all_t   = [t0_approx] + [cp.sim_time      for cp in checkpoints]
    all_b   = [b0_approx] + [cp.pt_bat        for cp in checkpoints]
    all_deg = [False]     + [cp.motor_degraded for cp in checkpoints]

    times, battery = [], []
    theta     = 2.5
    sigma     = config.BAT_SENSOR_NOISE_STD * 0.35
    exp_decay = np.exp(-theta * config.BAT_SAMPLE_DT)
    noise_std = sigma * np.sqrt(1.0 - exp_decay**2)
    ou_state  = 0.0

    for i in range(len(all_t) - 1):
        t0, b0 = all_t[i],   all_b[i]
        t1, b1 = all_t[i+1], all_b[i+1]
        degraded = all_deg[i] or all_deg[i+1]
        n_pts = max(3, int((t1 - t0) * config.BAT_SAMPLE_HZ))
        seg_t = np.linspace(t0, t1, n_pts)
        seg_b = battery_segment_curve(b0, b1, n_pts, degraded)
        noise = np.zeros(n_pts)
        noise[0] = ou_state
        for k in range(1, n_pts):
            noise[k] = noise[k-1]*exp_decay + noise_std*rng.standard_normal()
        ou_state = noise[-1]
        if i == 0:
            times.extend(seg_t);     battery.extend(seg_b + noise)
        else:
            times.extend(seg_t[1:]); battery.extend((seg_b+noise)[1:])

    return (np.array(times),
            np.clip(np.array(battery),
                    -config.BAT_SENSOR_NOISE_STD*2,
                    config.TOT_BAT + config.BAT_SENSOR_NOISE_STD*2))
