/**
 * @file error.cpp
 * @brief Implementation of twin::Error helpers.
 */
#include "twin/core/error.hpp"

namespace twin {

std::string_view to_string(ErrorCode code) noexcept {
    switch (code) {
        case ErrorCode::InvalidArgument: return "invalid_argument";
        case ErrorCode::ParseError: return "parse_error";
        case ErrorCode::ValidationError: return "validation_error";
        case ErrorCode::UnsupportedConstruct: return "unsupported_construct";
        case ErrorCode::NotFound: return "not_found";
        case ErrorCode::IoError: return "io_error";
        case ErrorCode::IntegrityError: return "integrity_error";
        case ErrorCode::ArithmeticOverflow: return "arithmetic_overflow";
        case ErrorCode::TimeNotRepresentable: return "time_not_representable";
        case ErrorCode::TimeRegression: return "time_regression";
        case ErrorCode::InvariantViolation: return "invariant_violation";
        case ErrorCode::TransitionNotEnabled: return "transition_not_enabled";
        case ErrorCode::IncompatibleObservation: return "incompatible_observation";
        case ErrorCode::StateError: return "state_error";
        case ErrorCode::Unavailable: return "unavailable";
        case ErrorCode::Internal: return "internal";
    }
    return "internal";
}

Error& Error::with(std::string key, std::string value) & {
    context.emplace_back(std::move(key), std::move(value));
    return *this;
}

Error&& Error::with(std::string key, std::string value) && {
    context.emplace_back(std::move(key), std::move(value));
    return std::move(*this);
}

std::string_view Error::context_value(std::string_view key) const noexcept {
    for (const auto& [k, v] : context) {
        if (k == key) {
            return v;
        }
    }
    return {};
}

std::string Error::to_string() const {
    std::string out{twin::to_string(code)};
    out += ": ";
    out += message;
    if (!context.empty()) {
        out += " [";
        bool first = true;
        for (const auto& [k, v] : context) {
            if (!first) {
                out += ", ";
            }
            first = false;
            out += k;
            out += '=';
            out += v;
        }
        out += ']';
    }
    return out;
}

Error make_error(ErrorCode code, std::string message) {
    return Error{code, std::move(message), {}};
}

}  // namespace twin
