/**
 * @file property_test.cpp
 * @brief Property-based validation of the kernel's derived queries.
 *
 * For many randomly generated timed automata (random clocks, invariants,
 * guards including diagonal constraints, resets) and many configurations
 * reached by random kernel walks, the exact queries the runtime and the
 * planner rely on are compared with brute force over the tick grid:
 *
 *  - enabling_window(c, e) == { d in [0, H] | fire(delay(c, d), e) is defined }
 *    (restricted to the horizon H), including "unbounded" windows;
 *  - max_delay(c)        == max { d in [0, H] | delay(c, d) is defined };
 *  - the kernel functions are pure (their inputs are unchanged).
 *
 * This is validation, not proof: the window lemma is proved in
 * proof/sections/05-kernel-semantics.tex; these tests check that the code
 * implements it on thousands of concrete cases.
 */
#include <gtest/gtest.h>

#include <random>
#include <string>
#include <vector>

#include "support/model_builder.hpp"
#include "twin/kernel/semantics.hpp"

namespace twin::kernel {
namespace {

using test::ModelBuilder;

constexpr Ticks kHorizon = 150;  // brute-force horizon (ticks); 15 time units at R = 10

ir::Model random_model(std::mt19937& rng) {
    auto pick = [&rng](int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); };
    const int clocks = pick(1, 3);
    const int locations = pick(2, 5);
    ModelBuilder b("random");
    b.ticks_per_unit(10);
    for (int i = 0; i < clocks; ++i) b.clock("x" + std::to_string(i));
    static const char* const kOps[] = {"<", "<=", ">", ">=", "=="};
    auto clock = [&] { return "x" + std::to_string(pick(0, clocks - 1)); };
    for (int l = 0; l < locations; ++l) {
        std::vector<test::NamedConstraint> inv;
        if (l > 0 && pick(0, 2) > 0) inv.push_back(ModelBuilder::c(clock(), pick(0, 1) ? "<=" : "<", pick(2, 9)));
        b.location("L" + std::to_string(l), inv);
    }
    b.initial("L0");
    const int edges = pick(2, 8);
    for (int e = 0; e < edges; ++e) {
        std::vector<test::NamedConstraint> guard;
        const int atoms = pick(0, 2);
        for (int a = 0; a < atoms; ++a) {
            if (clocks >= 2 && pick(0, 4) == 0) {
                guard.push_back(ModelBuilder::diff("x0", "x1", kOps[pick(0, 4)], pick(-3, 4)));
            } else {
                guard.push_back(ModelBuilder::c(clock(), kOps[pick(0, 4)], pick(0, 8)));
            }
        }
        std::vector<std::string> resets;
        for (int c = 0; c < clocks; ++c) {
            if (pick(0, 2) == 0) resets.push_back("x" + std::to_string(c));
        }
        static const char* const kLabels[] = {"a!", "b!", "tau"};
        b.transition("L" + std::to_string(pick(0, locations - 1)), kLabels[pick(0, 2)],
                     "L" + std::to_string(pick(0, locations - 1)), guard, resets);
    }
    return b.build();
}

/// Configurations reached by a random walk of delays and discrete steps.
std::vector<Configuration> random_walk(const Model& m, std::mt19937& rng, int steps) {
    std::vector<Configuration> out;
    Result<Configuration> init = initial_configuration(m);
    if (!init) return out;
    Configuration c = init.value();
    out.push_back(c);
    for (int i = 0; i < steps; ++i) {
        const Ticks d = std::uniform_int_distribution<Ticks>(0, 40)(rng);
        if (Result<Configuration> delayed = delay(m, c, d)) {
            c = delayed.value();
            out.push_back(c);
        }
        const auto outgoing = m.outgoing(c.location);
        if (outgoing.empty()) continue;
        const ir::TransitionIndex t =
            outgoing[std::uniform_int_distribution<std::size_t>(0, outgoing.size() - 1)(rng)];
        if (Result<Configuration> fired = fire(m, c, t)) {
            c = fired.value();
            out.push_back(c);
        }
    }
    return out;
}

TEST(KernelProperties, WindowsAndDeadlinesMatchBruteForce) {
    std::mt19937 rng(20261004);  // deterministic: failures are reproducible
    std::size_t checked_windows = 0;
    std::size_t checked_configs = 0;
    for (int trial = 0; trial < 120; ++trial) {
        const ir::Model ir_model = random_model(rng);
        Result<std::shared_ptr<const Model>> created = Model::create(ir_model);
        ASSERT_TRUE(created.ok()) << created.error().to_string();
        const Model& m = *created.value();
        for (const Configuration& c : random_walk(m, rng, 25)) {
            const Configuration before = c;
            ++checked_configs;
            // max_delay vs brute force.
            const std::optional<Ticks> md = max_delay(m, c);
            Ticks brute_max = -1;
            for (Ticks d = 0; d <= kHorizon; ++d) {
                if (delay(m, c, d)) brute_max = d;
            }
            if (md) {
                EXPECT_EQ(std::min(*md, kHorizon), brute_max) << "trial " << trial;
            } else {
                EXPECT_EQ(brute_max, kHorizon) << "unbounded deadline but delay refused, trial " << trial;
            }
            // enabling_window vs brute force, per outgoing transition.
            for (ir::TransitionIndex t : m.outgoing(c.location)) {
                ++checked_windows;
                const std::optional<DelayWindow> w = enabling_window(m, c, t);
                for (Ticks d = 0; d <= kHorizon; ++d) {
                    bool admissible = false;
                    if (Result<Configuration> delayed = delay(m, c, d)) admissible = fire(m, delayed.value(), t).ok();
                    const bool in_window = w && d >= w->earliest && (!w->latest || d <= *w->latest);
                    ASSERT_EQ(admissible, in_window)
                        << "trial " << trial << " transition " << m.transition(t).id << " delay " << d;
                }
                // explain_window: same window, and the window is exactly the intersection of the
                // factors' delay sets with the execution horizon.
                const WindowExplanation x = explain_window(m, c, t);
                ASSERT_EQ(x.window.has_value(), w.has_value()) << "trial " << trial;
                if (w) {
                    EXPECT_EQ(x.window->earliest, w->earliest);
                    EXPECT_EQ(x.window->latest, w->latest);
                }
                Ticks lo = 0;
                std::optional<Ticks> hi;
                bool never = false;
                for (const WindowFactor& f : x.factors) {
                    never = never || f.never;
                    if (f.min_delay) lo = std::max(lo, *f.min_delay);
                    if (f.max_delay) hi = hi ? std::min(*hi, *f.max_delay) : *f.max_delay;
                }
                for (Ticks d = 0; d <= kHorizon; ++d) {
                    const bool by_factors = !never && d >= lo && (!hi || d <= *hi);
                    const bool in_window = w && d >= w->earliest && (!w->latest || d <= *w->latest);
                    ASSERT_EQ(by_factors, in_window) << "trial " << trial << " transition " << m.transition(t).id << " delay " << d;
                }
            }
            EXPECT_EQ(c, before) << "kernel queries must not modify their input";
        }
    }
    EXPECT_GT(checked_configs, 1000U);
    EXPECT_GT(checked_windows, 1000U);
}

}  // namespace
}  // namespace twin::kernel
