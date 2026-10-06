# Logical-time model

Every semantic statement of this system is made in **logical time**: the clock of the formal
model, sampled on an exact grid. Wall-clock time paces simulations and appears as informational
metadata. It never decides anything.

## The grid

- A model declares a resolution R, the ticks per model time unit. R must be a power of ten. The
  drone and pump models use R = 1000, so one tick is 1 ms when the unit is a second.
- Logical time is the grid T_R = (1/R)·ℕ. Instants and clock values are integers (`twin::Ticks`,
  `int64`).
- The horizon is `kMaxTicks = 2^62 − 1`. Arithmetic is checked: anything that would leave the
  range is an `arithmetic_overflow` error, never a wrap-around.
- Constants of the model (guards, invariants) are scaled exactly: `x ≤ 60` becomes
  `x ≤ 60000 ticks`.

## Exact text and JSON

- Times cross every interface as integer ticks or exact decimal strings in model units
  (`"16.8"`). `parse_time` refuses strings that are not on the grid (`time_not_representable`).
- **Floating-point JSON numbers are rejected** wherever time enters the semantics (runtime API,
  world API, feeds). Ledger records contain integers only, so their canonical bytes, and
  therefore their hashes, are platform-independent.

## Why restricting to the grid is sound

Guards and invariants are conjunctions of constraints `x ⋈ c` and `x − y ⋈ c` with integer
constants scaled to ticks. For a configuration on the grid:

- the set of delays enabling a transition is an interval whose end points are on the grid;
- all decisions about grid-timed inputs therefore coincide with the dense-time semantics
  restricted to the grid.

The proof states this in section 9 ("Logical time"). The property tests check the window lemma
against brute force.

## Time in the co-simulation

- The co-simulation master (`CoSimDriver`) advances the Physical Twin in fixed logical steps
  (100 ticks by default), in a fixed order: commands, PT step, PT events, map updates, decisions,
  deadline check.
- PT events carry the PT's logical timestamp. The twin's state time is the time of its last
  input.
- `--speed` only maps logical seconds to wall-clock seconds for pacing. Running at 10× or pausing
  changes nothing semantically. The deterministic e2e test runs the whole mission without any
  pacing.

## Time in monitor mode

The external Physical Twin stamps its events and telemetry with logical times (ticks). The
runtime never reads a wall clock for semantics. Studio stores telemetry with three distinct
times:
- **observed at**: Studio's reception time, labelled as such, because the PT has no wall clock;
- **ingested at**: Studio's wall clock;
- **logical ticks**: the PT's logical timestamp.

## Deadlines and monitoring

- A location invariant bounds how long the twin may stay. `snapshot().deadline` is the latest
  admissible logical time.
- After each co-simulation step the driver asks the kernel whether the current logical time is
  still admissible (`projected(now)`). If it is not, it records a `deadline_missed` alarm. That is
  a monitoring verdict about the *observed* execution, not a real-time guarantee about the
  physical system.

## Not claimed

The system makes no claim about physical response times, scheduling latency or hard real-time
behaviour. Statements such as "the twin admits plan_accepted! within 5 s of entering REPLANNING"
are statements about logical time in the model and the recorded execution.
