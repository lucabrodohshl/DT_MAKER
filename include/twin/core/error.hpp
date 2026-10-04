/**
 * @file error.hpp
 * @brief Structured, machine-readable error values.
 * @ingroup core
 *
 * Errors are first-class values in this code base: every operation that can
 * fail for a reason other than a programming bug returns twin::Result<T>
 * (see result.hpp) carrying a twin::Error. Error codes are stable identifiers:
 * they appear in API responses, compiler diagnostics and ledger records.
 */
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace twin {

/**
 * @brief Stable error categories.
 *
 * The categories distinguish *semantic refusals* (the kernel correctly refuses a
 * step that the formal model does not admit, e.g. #InvariantViolation) from
 * *input errors* and *infrastructure failures*. Only the latter may make the
 * production runtime fail-stop; see docs/trusted-computing-base.md.
 */
enum class ErrorCode {
    InvalidArgument,          ///< A caller-supplied value is malformed.
    ParseError,               ///< A document is syntactically invalid.
    ValidationError,          ///< A well-formed document violates a semantic rule.
    UnsupportedConstruct,     ///< Input uses a construct outside the supported formal fragment.
    NotFound,                 ///< A referenced entity does not exist.
    IoError,                  ///< File-system or stream failure.
    IntegrityError,           ///< Hash, chain or binding mismatch (tampering or corruption).
    ArithmeticOverflow,       ///< Exact arithmetic would leave its representable range.
    TimeNotRepresentable,     ///< A timestamp is not on the logical-time grid.
    TimeRegression,           ///< An operation would move logical time backwards.
    InvariantViolation,       ///< A delay would violate the invariant of the current location.
    TransitionNotEnabled,     ///< The requested discrete transition is not enabled.
    IncompatibleObservation,  ///< An observation is inconsistent with every possible state.
    StateError,               ///< Operation not permitted in the current lifecycle state.
    Unavailable,              ///< Infrastructure (ledger storage, peer service) unavailable.
    Internal                  ///< A broken internal invariant (a bug).
};

/// @brief Stable lower_snake_case name of an error code, e.g. "invariant_violation".
[[nodiscard]] std::string_view to_string(ErrorCode code) noexcept;

/// @brief Inverse of to_string(ErrorCode); std::nullopt for unknown names (e.g. from a peer service).
[[nodiscard]] std::optional<ErrorCode> error_code_from_string(std::string_view name) noexcept;

/**
 * @brief An error value: a code, a human-readable message and key/value context.
 *
 * The context carries structured details (e.g. `{"clock","t_mode"}`) that tools
 * render or serialise without parsing the message text.
 */
struct Error {
    ErrorCode code{ErrorCode::Internal};                       ///< Category.
    std::string message;                                       ///< Human-readable explanation.
    std::vector<std::pair<std::string, std::string>> context;  ///< Structured details.

    /// @brief Append a context entry (lvalue overload, chainable).
    Error& with(std::string key, std::string value) &;
    /// @brief Append a context entry (rvalue overload, chainable).
    Error&& with(std::string key, std::string value) &&;
    /// @brief Look up a context value by key; empty view if absent.
    [[nodiscard]] std::string_view context_value(std::string_view key) const noexcept;
    /// @brief Render as "code: message [k=v, ...]".
    [[nodiscard]] std::string to_string() const;
};

/// @brief Convenience constructor for an Error.
[[nodiscard]] Error make_error(ErrorCode code, std::string message);

}  // namespace twin
