/**
 * @file semantics.cpp
 * @brief Implementation of the kernel transition system K_sem.
 *
 * Every function in this file is a direct transcription of a rule of the IR
 * semantics; the correspondence is stated next to each function and proved in
 * proof/sections/06-kernel-isomorphism.tex. Keep this file small and obvious:
 * any change here must be reflected in the proof.
 */
#include "twin/kernel/semantics.hpp"

#include <algorithm>

namespace twin::kernel {
namespace {

/// d ~ b  (proof: Def. "atom satisfaction").
constexpr bool compare(Ticks d, ir::Comparison op, Ticks b) noexcept {
    switch (op) {
        case ir::Comparison::Less: return d < b;
        case ir::Comparison::LessEqual: return d <= b;
        case ir::Comparison::Equal: return d == b;
        case ir::Comparison::GreaterEqual: return d >= b;
        case ir::Comparison::Greater: return d > b;
    }
    return false;
}

/// v |= (x - y ~ b). Overflow-free: values lie in [0, 2^62), |b| < 2^61.
bool atom_holds(const Configuration& c, const TickConstraint& a) noexcept {
    return compare(clock_value(c, a.lhs) - clock_value(c, a.rhs), a.op, a.bound);
}

/// Index of the first atom of @p g violated by @p c, if any.
std::optional<std::size_t> first_violation(const Configuration& c,
                                           std::span<const TickConstraint> g) noexcept {
    for (std::size_t i = 0; i < g.size(); ++i) {
        if (!atom_holds(c, g[i])) {
            return i;
        }
    }
    return std::nullopt;
}

/// Render an atom in model syntax with its exact bound, e.g. "t_mode <= 5".
std::string atom_text(const Model& m, const TickConstraint& a) {
    std::string out = ir::clock_name(m.ir(), a.lhs);
    if (a.rhs != ir::kReferenceClock) {
        out += " - " + ir::clock_name(m.ir(), a.rhs);
    }
    out += ' ';
    out += ir::to_string(a.op);
    out += ' ';
    out += format_time(a.bound, m.time_base());
    return out;
}

/**
 * The common core of enablement() and fire(): applies rule (DISCRETE).
 * On Enablement::Enabled, @p out receives (l', v[Y:=0], theta).
 */
Enablement discrete_rule(const Model& m, const Configuration& c, ir::TransitionIndex e,
                         Configuration* out, std::optional<std::size_t>* failed_atom) {
    const ir::Transition& t = m.transition(e);
    if (c.location != t.source) {
        return Enablement::WrongLocation;
    }
    if (auto bad = first_violation(c, m.guard(e))) {           // v |= g
        *failed_atom = bad;
        return Enablement::GuardFalse;
    }
    Configuration next{t.target, c.clocks, c.time};            // (l', v, theta)
    for (ir::ClockIndex r : t.resets) {                         // v[Y := 0]
        next.clocks[r - 1U] = 0;
    }
    if (auto bad = first_violation(next, m.invariant(t.target))) {  // v[Y:=0] |= Inv(l')
        *failed_atom = bad;
        return Enablement::TargetInvariantFalse;
    }
    *out = std::move(next);
    return Enablement::Enabled;
}

/// Inclusive interval of delays, used by enabling_window() and max_delay().
struct Interval {
    Ticks lo{0};
    Ticks hi{0};
    bool upper_constrained{false};  ///< hi comes from an atom, not from the horizon.
    [[nodiscard]] bool empty() const noexcept { return lo > hi; }
};

constexpr ir::Comparison flip(ir::Comparison op) noexcept {
    switch (op) {
        case ir::Comparison::Less: return ir::Comparison::Greater;
        case ir::Comparison::LessEqual: return ir::Comparison::GreaterEqual;
        case ir::Comparison::Equal: return ir::Comparison::Equal;
        case ir::Comparison::GreaterEqual: return ir::Comparison::LessEqual;
        case ir::Comparison::Greater: return ir::Comparison::Less;
    }
    return op;
}

/// Intersect @p iv with { d | d (op) m } over the integers.
void restrict_delay(Interval& iv, ir::Comparison op, Ticks m) noexcept {
    auto cap = [&iv](Ticks v) {
        if (v < iv.hi) {
            iv.hi = v;
        }
        iv.upper_constrained = true;
    };
    switch (op) {
        case ir::Comparison::Less: cap(m - 1); break;
        case ir::Comparison::LessEqual: cap(m); break;
        case ir::Comparison::Equal:
            iv.lo = std::max(iv.lo, m);
            cap(m);
            break;
        case ir::Comparison::GreaterEqual: iv.lo = std::max(iv.lo, m); break;
        case ir::Comparison::Greater: iv.lo = std::max(iv.lo, m + 1); break;
    }
}

/**
 * Restrict @p iv to the delays d for which atom @p a holds in the valuation
 * w + d*s, where w_x = base(x) and s_x = slope(x) in {0,1} (s = 0 for the
 * reference clock and for clocks reset by the transition).
 *
 *   (w_l + s_l d) - (w_r + s_r d) ~ b   <=>   d0 + k d ~ b,  d0 = w_l - w_r, k = s_l - s_r.
 */
template <class Base, class Slope>
void restrict_by_atom(Interval& iv, const TickConstraint& a, Base base, Slope slope) noexcept {
    const Ticks d0 = base(a.lhs) - base(a.rhs);
    const int k = slope(a.lhs) - slope(a.rhs);
    if (k == 0) {
        if (!compare(d0, a.op, a.bound)) {
            iv.lo = 1;  // make the interval empty
            iv.hi = 0;
        }
    } else if (k == 1) {
        restrict_delay(iv, a.op, a.bound - d0);  // d ~ b - d0
    } else {
        restrict_delay(iv, flip(a.op), d0 - a.bound);  // -d ~ b - d0  <=>  d ~' d0 - b
    }
}

/// Delays admissible for the time-elapse rule from @p c (horizon included).
Interval delay_interval(const Model& m, const Configuration& c) noexcept {
    Interval iv{0, kMaxTicks - c.time, false};
    for (Ticks v : c.clocks) {
        iv.hi = std::min(iv.hi, kMaxTicks - v);
    }
    auto base = [&c](ir::ClockIndex x) { return clock_value(c, x); };
    auto slope = [](ir::ClockIndex x) { return x == ir::kReferenceClock ? 0 : 1; };
    for (const TickConstraint& a : m.invariant(c.location)) {
        restrict_by_atom(iv, a, base, slope);
    }
    return iv;
}

}  // namespace

bool satisfies(const Configuration& c, std::span<const TickConstraint> conjunction) {
    return !first_violation(c, conjunction).has_value();
}

// Rule (INIT): c0 = (l0, 0, 0), defined iff 0 |= Inv(l0).
Result<Configuration> initial_configuration(const Model& model) {
    Configuration c{model.ir().initial, std::vector<Ticks>(model.clock_count(), 0), 0};
    if (auto bad = first_violation(c, model.invariant(c.location))) {
        return make_error(ErrorCode::InvariantViolation,
                          "the zero valuation violates the invariant of the initial location")
            .with("location", model.ir().locations[c.location].id)
            .with("atom", atom_text(model, model.invariant(c.location)[*bad]));
    }
    return c;
}

// Rule (DELAY): (l,v,t) -d-> (l, v+d, t+d)  iff  v |= Inv(l) and v+d |= Inv(l).
Result<Configuration> delay(const Model& model, const Configuration& c, Ticks delta) {
    if (delta < 0) {
        return make_error(ErrorCode::InvalidArgument, "negative delay")
            .with("delta", std::to_string(delta));
    }
    const std::span<const TickConstraint> inv = model.invariant(c.location);
    if (auto bad = first_violation(c, inv)) {
        return make_error(ErrorCode::InvariantViolation,
                          "source configuration violates the invariant of its location")
            .with("location", model.ir().locations[c.location].id)
            .with("atom", atom_text(model, inv[*bad]));
    }
    Configuration next = c;
    if (__builtin_add_overflow(c.time, delta, &next.time) || next.time > kMaxTicks) {
        return make_error(ErrorCode::ArithmeticOverflow, "delay exceeds the execution horizon");
    }
    for (Ticks& v : next.clocks) {
        if (__builtin_add_overflow(v, delta, &v) || v > kMaxTicks) {
            return make_error(ErrorCode::ArithmeticOverflow, "delay exceeds the execution horizon");
        }
    }
    if (auto bad = first_violation(next, inv)) {
        Error e = make_error(ErrorCode::InvariantViolation,
                             "letting this much logical time elapse violates the location invariant");
        e.with("location", model.ir().locations[c.location].id)
            .with("atom", atom_text(model, inv[*bad]))
            .with("requested_delay", format_time(delta, model.time_base()));
        const Interval iv = delay_interval(model, c);
        if (iv.upper_constrained && !iv.empty()) {
            e.with("max_delay", format_time(iv.hi, model.time_base()));
        }
        return e;
    }
    return next;
}

Enablement enablement(const Model& model, const Configuration& c, ir::TransitionIndex transition) {
    if (transition >= model.transition_count()) {
        return Enablement::NoSuchTransition;
    }
    Configuration ignored;
    std::optional<std::size_t> atom;
    return discrete_rule(model, c, transition, &ignored, &atom);
}

// Rule (DISCRETE): (l,v,t) -e-> (l', v[Y:=0], t) for e = (l, g, a, Y, l').
Result<Configuration> fire(const Model& model, const Configuration& c,
                           ir::TransitionIndex transition) {
    if (transition >= model.transition_count()) {
        return make_error(ErrorCode::InvalidArgument, "transition index out of range")
            .with("transition", std::to_string(transition));
    }
    Configuration next;
    std::optional<std::size_t> atom;
    const ir::Transition& t = model.transition(transition);
    switch (discrete_rule(model, c, transition, &next, &atom)) {
        case Enablement::Enabled:
            return next;
        case Enablement::WrongLocation:
            return make_error(ErrorCode::TransitionNotEnabled,
                              "the configuration is not in the transition's source location")
                .with("transition", t.id)
                .with("location", model.ir().locations[c.location].id);
        case Enablement::GuardFalse:
            return make_error(ErrorCode::TransitionNotEnabled, "the guard is false")
                .with("transition", t.id)
                .with("atom", atom_text(model, model.guard(transition)[atom.value_or(0)]));
        case Enablement::TargetInvariantFalse:
            return make_error(ErrorCode::TransitionNotEnabled,
                              "the target invariant would be violated after the resets")
                .with("transition", t.id)
                .with("atom", atom_text(model, model.invariant(t.target)[atom.value_or(0)]));
        case Enablement::NoSuchTransition:
            break;
    }
    return make_error(ErrorCode::Internal, "unreachable enablement state");
}

std::vector<ir::TransitionIndex> enabled_transitions(const Model& model, const Configuration& c) {
    std::vector<ir::TransitionIndex> out;
    for (ir::TransitionIndex e : model.outgoing(c.location)) {
        if (enablement(model, c, e) == Enablement::Enabled) {
            out.push_back(e);
        }
    }
    return out;
}

std::vector<DiscreteSuccessor> discrete_successors(const Model& model, const Configuration& c) {
    std::vector<DiscreteSuccessor> out;
    for (ir::TransitionIndex e : model.outgoing(c.location)) {
        Configuration next;
        std::optional<std::size_t> atom;
        if (discrete_rule(model, c, e, &next, &atom) == Enablement::Enabled) {
            out.push_back(DiscreteSuccessor{e, std::move(next)});
        }
    }
    return out;
}

Result<Configuration> timed_step(const Model& model, const Configuration& c, Ticks delta,
                                 ir::TransitionIndex transition) {
    Result<Configuration> delayed = delay(model, c, delta);
    if (!delayed) {
        return delayed;
    }
    return fire(model, delayed.value(), transition);
}

// Labelling: L(l, v, t) = { p | loc(p) = l }.
std::vector<ir::PropositionIndex> propositions(const Model& model, const Configuration& c) {
    std::vector<ir::PropositionIndex> out;
    const auto& props = model.ir().propositions;
    for (std::size_t i = 0; i < props.size(); ++i) {
        if (props[i].location == c.location) {
            out.push_back(static_cast<ir::PropositionIndex>(i));
        }
    }
    return out;
}

std::optional<DelayWindow> enabling_window(const Model& model, const Configuration& c,
                                           ir::TransitionIndex transition) {
    if (transition >= model.transition_count()) {
        return std::nullopt;
    }
    const ir::Transition& t = model.transition(transition);
    if (c.location != t.source || !satisfies(c, model.invariant(c.location))) {
        return std::nullopt;
    }
    // (1) delay admissible in the source location; (2) guard holds after the delay.
    Interval iv = delay_interval(model, c);
    auto base = [&c](ir::ClockIndex x) { return clock_value(c, x); };
    auto slope = [](ir::ClockIndex x) { return x == ir::kReferenceClock ? 0 : 1; };
    for (const TickConstraint& a : model.guard(transition)) {
        restrict_by_atom(iv, a, base, slope);
    }
    // (3) target invariant holds after the resets: reset clocks are 0 and do not advance.
    auto is_reset = [&t](ir::ClockIndex x) {
        return std::binary_search(t.resets.begin(), t.resets.end(), x);
    };
    auto base_after = [&](ir::ClockIndex x) { return is_reset(x) ? Ticks{0} : clock_value(c, x); };
    auto slope_after = [&](ir::ClockIndex x) {
        return (x == ir::kReferenceClock || is_reset(x)) ? 0 : 1;
    };
    for (const TickConstraint& a : model.invariant(t.target)) {
        restrict_by_atom(iv, a, base_after, slope_after);
    }
    if (iv.empty()) {
        return std::nullopt;
    }
    DelayWindow w;
    w.earliest = iv.lo;
    if (iv.upper_constrained) {
        w.latest = iv.hi;
    }
    return w;
}

std::optional<Ticks> max_delay(const Model& model, const Configuration& c) {
    const Interval iv = delay_interval(model, c);
    if (!iv.upper_constrained || iv.empty()) {
        return iv.empty() ? std::optional<Ticks>(0) : std::nullopt;
    }
    return iv.hi;
}

std::vector<AtomEvaluation> explain_guard(const Model& model, const Configuration& c,
                                          ir::TransitionIndex transition) {
    std::vector<AtomEvaluation> out;
    const std::span<const TickConstraint> g = model.guard(transition);
    const ir::Conjunction& source_atoms = model.transition(transition).guard;
    for (std::size_t i = 0; i < g.size(); ++i) {
        const Ticks diff = clock_value(c, g[i].lhs) - clock_value(c, g[i].rhs);
        out.push_back(AtomEvaluation{source_atoms[i], diff, g[i].bound, compare(diff, g[i].op, g[i].bound)});
    }
    return out;
}

}  // namespace twin::kernel
