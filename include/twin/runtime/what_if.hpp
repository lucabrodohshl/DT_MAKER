/**
 * @file what_if.hpp
 * @brief What-if scenarios and timed action availability (pure queries on copies of semantic state).
 * @ingroup runtime
 *
 * Answers, from the kernel's own semantics, the questions an engineer asks of a timed model:
 *
 *  - which events can occur from a (hypothetical) state, by which transitions, to which targets;
 *  - WHEN: the exact admissible delay window of every alternative (kernel::enabling_window),
 *    relative to the state and as absolute logical time, and the atoms that determine it
 *    (kernel::explain_window: source invariant, guard, target invariant after resets);
 *  - how far time may advance (the location invariant's deadline, kernel::max_delay);
 *  - what a scenario (a sequence of delays and events) does, step by step, under the
 *    monitoring semantics (nondeterminism is kept as a set, never resolved), and exactly
 *    where and why it becomes impossible.
 *
 * Windows are per transition and per possible configuration; each is an interval of the tick
 * grid (convexity is a lemma of the semantics). An event's admissible delays are the union
 * of its alternatives' windows and may therefore be non-convex: they are reported as a list.
 * Nothing is recorded and the live state is never modified.
 */
#pragma once

#include "twin/core/result.hpp"
#include "twin/json/canonical.hpp"
#include "twin/kernel/model.hpp"
#include "twin/kernel/state_set.hpp"

namespace twin::runtime {

/**
 * @brief Run a what-if request.
 *
 * Request: `{start, steps}` with
 *  - `start`: `{"kind": "current"}` (the committed state, given as @p current),
 *    `{"kind": "initial"}`, or `{"kind": "configurations", "configurations": [{location,
 *    clocks: {name: "<decimal>"}, time: "<decimal>"}]}` (for example a recorded replay state;
 *    every configuration must satisfy its location invariant and share one time);
 *  - `steps`: list of `{"kind": "delay", "delay": "<decimal>"}` (or `"until": "<decimal>"`: let
 *    time pass up to that absolute time, a no-op when it is not in the future) or `{"kind": "event",
 *    "label": "<label>" | "transition": "<id>", "delay": "<decimal>" | "at": "<decimal>"}`
 *    (`delay` is relative to the end of the previous step, `at` is absolute logical time;
 *    naming a transition selects that branch only).
 *
 * Response: `{start, steps: [...], first_invalid, final: {state, propositions, max_delay,
 * availability, unavailable}, note}` (see docs/runtime-api.md, POST /runtime/what-if).
 */
[[nodiscard]] Result<json::Json> what_if(const kernel::Model& model, const kernel::StateSet& current,
                                         const json::Json& request);

/// @brief Availability of every event from @p states (the `final` part of a what-if response).
[[nodiscard]] json::Json availability_view(const kernel::Model& model, const kernel::StateSet& states);

}  // namespace twin::runtime
