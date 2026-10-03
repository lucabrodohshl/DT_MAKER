/**
 * @file model.hpp
 * @brief Immutable, validated, tick-scaled form of an IR model executed by the kernel.
 * @ingroup kernel
 *
 * twin::kernel::Model is the only representation of the behavioural model the
 * kernel executes. It is created exclusively through Model::create(), which
 *  1. re-validates the IR model (twin::ir::validate) — a model that bypassed the
 *     compiler can never be executed in a malformed state;
 *  2. scales every integer bound c (model time units) to ticks c*R exactly,
 *     failing with ErrorCode::ArithmeticOverflow instead of wrapping;
 *  3. pre-computes read-only indexes (outgoing transitions per location, action
 *     label table). These indexes are derived data: they change no semantics.
 *
 * Instances are immutable and shared via std::shared_ptr<const Model>, so any
 * number of live executions and predictions can use one model concurrently.
 */
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "twin/core/logical_time.hpp"
#include "twin/core/result.hpp"
#include "twin/ir/model.hpp"

namespace twin::kernel {

/// @brief Dense identifier of an action label ("a!", "a?", "tau") within one model.
using LabelId = std::uint32_t;

/**
 * @brief Atomic clock constraint with its bound scaled to ticks:  x_lhs - x_rhs ~ bound.
 *
 * Mirrors ir::ClockConstraint exactly except that the bound is c*R ticks.
 */
struct TickConstraint {
    ir::ClockIndex lhs{1};                         ///< Left clock (>= 1).
    ir::ClockIndex rhs{ir::kReferenceClock};       ///< Right clock or reference clock 0.
    ir::Comparison op{ir::Comparison::LessEqual};  ///< Comparison.
    Ticks bound{0};                                ///< c * R.
};

/**
 * @brief Immutable executable model. See file documentation.
 */
class Model {
public:
    /**
     * @brief Validate and prepare an IR model for execution.
     * @return ErrorCode::ValidationError for malformed IR, ErrorCode::ArithmeticOverflow
     *         if a bound cannot be scaled to ticks.
     */
    [[nodiscard]] static Result<std::shared_ptr<const Model>> create(ir::Model ir);

    /// @brief The IR model this executable model was built from (unchanged).
    [[nodiscard]] const ir::Model& ir() const noexcept { return ir_; }
    /// @brief Logical-time resolution R.
    [[nodiscard]] TimeBase time_base() const noexcept { return ir_.time; }
    /// @brief Number of model clocks n (clock ids are 1..n).
    [[nodiscard]] std::size_t clock_count() const noexcept { return ir_.clocks.size(); }
    /// @brief Number of locations.
    [[nodiscard]] std::size_t location_count() const noexcept { return ir_.locations.size(); }
    /// @brief Number of transitions.
    [[nodiscard]] std::size_t transition_count() const noexcept { return ir_.transitions.size(); }

    /// @brief Invariant of @p location in ticks (precondition: index in range).
    [[nodiscard]] std::span<const TickConstraint> invariant(ir::LocationIndex location) const;
    /// @brief Guard of @p transition in ticks (precondition: index in range).
    [[nodiscard]] std::span<const TickConstraint> guard(ir::TransitionIndex transition) const;
    /// @brief The IR transition (precondition: index in range).
    [[nodiscard]] const ir::Transition& transition(ir::TransitionIndex transition) const;
    /// @brief Outgoing transitions of @p location, in ascending index order.
    [[nodiscard]] std::span<const ir::TransitionIndex> outgoing(ir::LocationIndex location) const;

    /// @brief Label id of an action label such as "plan_accepted!" or "tau".
    [[nodiscard]] std::optional<LabelId> label_id(std::string_view label) const;
    /// @brief Label text of a label id.
    [[nodiscard]] const std::string& label_name(LabelId label) const;
    /// @brief Label id carried by @p transition.
    [[nodiscard]] LabelId label_of(ir::TransitionIndex transition) const;
    /// @brief All distinct action labels, in ascending label-id order.
    [[nodiscard]] const std::vector<std::string>& labels() const noexcept { return labels_; }

private:
    Model() = default;

    ir::Model ir_;
    std::vector<std::vector<TickConstraint>> invariants_;
    std::vector<std::vector<TickConstraint>> guards_;
    std::vector<std::vector<ir::TransitionIndex>> outgoing_;
    std::vector<std::string> labels_;
    std::vector<LabelId> transition_label_;
};

}  // namespace twin::kernel
