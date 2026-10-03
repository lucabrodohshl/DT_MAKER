/**
 * @file result.hpp
 * @brief twin::Result<T>: a value or a structured twin::Error.
 * @ingroup core
 *
 * A minimal, dependency-free alternative to C++23 `std::expected`, used on
 * every fallible path. Accessing the value of an error result (or the error of
 * a successful result) is a programming bug and throws twin::BadResultAccess;
 * domain failures are never reported through exceptions.
 */
#pragma once

#include <optional>
#include <stdexcept>
#include <utility>
#include <variant>

#include "twin/core/error.hpp"

namespace twin {

/// @brief Thrown only when code accesses the wrong alternative of a Result (a bug).
class BadResultAccess : public std::logic_error {
public:
    using std::logic_error::logic_error;
};

/**
 * @brief Holds either a value of type @p T or an Error.
 * @tparam T value type; must not be twin::Error.
 */
template <class T>
class [[nodiscard]] Result {
public:
    /// @brief Successful result.
    Result(T value) : storage_(std::in_place_index<0>, std::move(value)) {}  // NOLINT(google-explicit-constructor)
    /// @brief Failed result.
    Result(Error error) : storage_(std::in_place_index<1>, std::move(error)) {}  // NOLINT(google-explicit-constructor)

    /// @brief True iff the result holds a value.
    [[nodiscard]] bool ok() const noexcept { return storage_.index() == 0; }
    /// @brief Same as ok().
    explicit operator bool() const noexcept { return ok(); }

    /// @brief The value. @throws BadResultAccess if this is an error.
    [[nodiscard]] const T& value() const& { check_value(); return std::get<0>(storage_); }
    /// @copydoc value() const&
    [[nodiscard]] T& value() & { check_value(); return std::get<0>(storage_); }
    /// @copydoc value() const&
    [[nodiscard]] T&& value() && { check_value(); return std::get<0>(std::move(storage_)); }

    /// @brief The error. @throws BadResultAccess if this is a value.
    [[nodiscard]] const Error& error() const& { check_error(); return std::get<1>(storage_); }
    /// @copydoc error() const&
    [[nodiscard]] Error&& error() && { check_error(); return std::get<1>(std::move(storage_)); }

    /// @brief Dereference shorthand for value().
    const T& operator*() const& { return value(); }
    /// @brief Member access shorthand for value().
    const T* operator->() const { return &value(); }

private:
    void check_value() const {
        if (!ok()) {
            throw BadResultAccess("Result::value() on error: " + std::get<1>(storage_).to_string());
        }
    }
    void check_error() const {
        if (ok()) {
            throw BadResultAccess("Result::error() on success");
        }
    }

    std::variant<T, Error> storage_;
};

/**
 * @brief Result specialisation for operations that produce no value.
 */
template <>
class [[nodiscard]] Result<void> {
public:
    /// @brief Successful status.
    Result() = default;
    /// @brief Failed status.
    Result(Error error) : error_(std::move(error)) {}  // NOLINT(google-explicit-constructor)

    /// @brief True iff no error is held.
    [[nodiscard]] bool ok() const noexcept { return !error_.has_value(); }
    /// @brief Same as ok().
    explicit operator bool() const noexcept { return ok(); }
    /// @brief The error. @throws BadResultAccess if successful.
    [[nodiscard]] const Error& error() const& {
        if (!error_) {
            throw BadResultAccess("Status::error() on success");
        }
        return *error_;
    }

private:
    std::optional<Error> error_;
};

/// @brief A Result without a value.
using Status = Result<void>;

/// @brief The successful Status.
[[nodiscard]] inline Status ok_status() { return Status{}; }

}  // namespace twin
