import numpy as np
from dataclasses import dataclass

TOT_BAT       = 400.0   # Wh  (verified: nominal mission completes, degraded causes violation)
FLY_COST      = 10.0
RET_COST      = 10.0
CONS_NOM      = 30.0    # Wh max spray nominal
CONS_MIN      = 10.0    # Wh min spray nominal
MOTOR_DELTA   = 45.0
CONS_DEGRADED = CONS_NOM + MOTOR_DELTA   # 50 Wh
NUM_HEC       = 9
CLEANING_COST = 5.0
WIND_HUM_THRESH  = 70
WIND_PRES_THRESH = 1000

DRONE_MASS_KG=10.0; ROTOR_AREA_M2=0.385; AIR_DENSITY_KGM3=1.225
GRAVITY_MS2=9.81; FWD_FLIGHT_OVERHEAD=0.18
P_HOVER_W = float(np.sqrt((DRONE_MASS_KG*GRAVITY_MS2)**3/(2*AIR_DENSITY_KGM3*ROTOR_AREA_M2)))
P_PUMP_MIN_W=30.0; P_PUMP_MAX_W=50.0; P_PUMP_AVG_W=40.0
P_total_min = P_HOVER_W*(1+FWD_FLIGHT_OVERHEAD)+P_PUMP_MIN_W
P_total_max = P_HOVER_W*(1+FWD_FLIGHT_OVERHEAD)+P_PUMP_MAX_W
SPRAY_TIME_MIN_S = (CONS_MIN*3600.0)/P_total_max
SPRAY_TIME_MAX_S = (CONS_NOM*3600.0)/P_total_min
P_FLIGHT_W   = P_HOVER_W*(1+FWD_FLIGHT_OVERHEAD)
FLIGHT_TIME_S = (FLY_COST*3600.0)/P_FLIGHT_W
MOTOR_DEGRAD_FACTOR = CONS_DEGRADED/CONS_NOM   # 1.667

BAT_SENSOR_NOISE_STD = 1.5

# Speed boost: 40% faster → 17% less energy per cell
SPEED_BOOST_FACTOR = 1.4
def _speed_energy_ratio(sf):
    sf=float(np.clip(sf,0.6,2.0))
    return float(((P_HOVER_W+P_HOVER_W*FWD_FLIGHT_OVERHEAD*sf**2+P_PUMP_AVG_W*sf)/sf)
                 /(P_HOVER_W+P_HOVER_W*FWD_FLIGHT_OVERHEAD+P_PUMP_AVG_W))
SPEED_BOOST_ENERGY_RATIO = _speed_energy_ratio(SPEED_BOOST_FACTOR)

FIELD_CROP_DENSITY=[0.45,0.72,0.38,0.85,0.61,0.52,0.68,0.41,0.79]
FIELD_HUMIDITY=[62,74,68,78,65,60,75,67,80]
FIELD_PRESSURE=[1008,997,1003,994,1005,1010,995,1002,993]

MONITOR_A_THRESHOLD = 2*BAT_SENSOR_NOISE_STD+5.0

TELEMETRY_INTERVAL_S=20.0; BAT_SAMPLE_HZ=10; BAT_SAMPLE_DT=1.0/BAT_SAMPLE_HZ
ENERGY_ARCH=0.15

#12345465766564533
CANONICAL_SEEDS=[7643, 46763]
MOTOR_FAILURE_CELL=0   # failure at cell 0
MOTOR_FAILURE_CELLS=[0,3] #3
N_SWEEP=5000
FAILURES_PROB=[0.0,0.1,0.2,0.3,0.4,0.5]
OUTPUT_FOLDER="results/"

SAFETY_MARGIN = 1
NO_SWEEP = False
@dataclass
class CheckPoint:
    sim_time:float; cell_idx:int
    pt_bat:float; pt_bat_noisy:float; dt_budget:float
    dt_decision:str; speed_factor:float
    cons_actual:float; cons_est:float
    motor_degraded:bool; monitor_a:bool; monitor_b:bool
    fidelity_gap:float; depleted:bool; event:str
    dt_predicted_end:float=0.0
