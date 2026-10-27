#pragma once

#include "kernox/core/error.hpp"

#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>

namespace kernox::core {

template <typename T>
class Result {
    static_assert(!std::is_same_v<T, void>, "Use Result<void> specialization for void results");
    static_assert(!std::is_same_v<T, Error>, "Result cannot directly wrap Error as value type");

public:
    using value_type = T;

    // Success constructors
    Result(const T& val) : storage_(val) {} // NOLINT(google-explicit-constructor)
    Result(T&& val) : storage_(std::move(val)) {} // NOLINT(google-explicit-constructor)

    // Failure constructor
    Result(Error err) : storage_(std::move(err)) {} // NOLINT(google-explicit-constructor)

    // Factory methods
    static Result<T> ok(T val) {
        return Result<T>(std::move(val));
    }

    static Result<T> err(Error error) {
        return Result<T>(std::move(error));
    }

    static Result<T> err(ErrorCode code, std::string message, int sys_errno = 0) {
        return Result<T>(Error(code, std::move(message), sys_errno));
    }

    [[nodiscard]] bool has_value() const noexcept {
        return std::holds_alternative<T>(storage_);
    }

    [[nodiscard]] bool is_ok() const noexcept {
        return has_value();
    }

    [[nodiscard]] bool is_err() const noexcept {
        return !has_value();
    }

    explicit operator bool() const noexcept {
        return is_ok();
    }

    [[nodiscard]] const T& value() const & {
        if (!has_value()) {
            throw std::logic_error("Attempted to access value on failed Result: " + error().to_string());
        }
        return std::get<T>(storage_);
    }

    [[nodiscard]] T& value() & {
        if (!has_value()) {
            throw std::logic_error("Attempted to access value on failed Result: " + error().to_string());
        }
        return std::get<T>(storage_);
    }

    [[nodiscard]] T&& value() && {
        if (!has_value()) {
            throw std::logic_error("Attempted to access value on failed Result: " + error().to_string());
        }
        return std::get<T>(std::move(storage_));
    }

    template <typename U>
    [[nodiscard]] T value_or(U&& default_value) const & {
        if (has_value()) {
            return std::get<T>(storage_);
        }
        return static_cast<T>(std::forward<U>(default_value));
    }

    template <typename U>
    [[nodiscard]] T value_or(U&& default_value) && {
        if (has_value()) {
            return std::get<T>(std::move(storage_));
        }
        return static_cast<T>(std::forward<U>(default_value));
    }

    [[nodiscard]] const Error& error() const & {
        if (has_value()) {
            throw std::logic_error("Attempted to access error on successful Result");
        }
        return std::get<Error>(storage_);
    }

    [[nodiscard]] Error& error() & {
        if (has_value()) {
            throw std::logic_error("Attempted to access error on successful Result");
        }
        return std::get<Error>(storage_);
    }

private:
    std::variant<T, Error> storage_;
};

// Specialization for void
template <>
class Result<void> {
public:
    using value_type = void;

    Result() = default; // Represents success

    // Failure constructor
    Result(Error err) : error_(std::move(err)) {} // NOLINT(google-explicit-constructor)

    static Result<void> ok() {
        return Result<void>();
    }

    static Result<void> err(Error error) {
        return Result<void>(std::move(error));
    }

    static Result<void> err(ErrorCode code, std::string message, int sys_errno = 0) {
        return Result<void>(Error(code, std::move(message), sys_errno));
    }

    [[nodiscard]] bool has_value() const noexcept {
        return !error_.has_value();
    }

    [[nodiscard]] bool is_ok() const noexcept {
        return has_value();
    }

    [[nodiscard]] bool is_err() const noexcept {
        return error_.has_value();
    }

    explicit operator bool() const noexcept {
        return is_ok();
    }

    void value() const {
        if (is_err()) {
            throw std::logic_error("Attempted to access value on failed Result<void>: " + error().to_string());
        }
    }

    [[nodiscard]] const Error& error() const & {
        if (is_ok()) {
            throw std::logic_error("Attempted to access error on successful Result<void>");
        }
        return *error_;
    }

    [[nodiscard]] Error& error() & {
        if (is_ok()) {
            throw std::logic_error("Attempted to access error on successful Result<void>");
        }
        return *error_;
    }

private:
    std::optional<Error> error_{std::nullopt};
};

} // namespace kernox::core
