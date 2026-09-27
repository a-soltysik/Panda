#pragma once

#include <concepts>
#include <expected>
#include <format>
#include <functional>
#include <optional>
#include <source_location>
#include <string_view>
#include <type_traits>
#include <utility>

namespace panda
{

/// @brief Writes a fatal diagnostic directly to stderr and aborts, also in Release.
/// Bypasses logger filters, sinks and locks. Writes allocation-free context first,
/// then attempts a bounded stack trace; trace failure does not prevent abort.
[[noreturn]] void panic(std::string_view message,
                        std::source_location location = std::source_location::current()) noexcept;

/// @brief Logs a warning on failure and returns the condition. Active in Release.
/// @param condition A condition evaluated once by the caller.
/// @param message Actionable context, copied only on failure.
/// @param location Call site, captured automatically.
/// @return The supplied condition; does not unwrap or consume a result.
[[nodiscard]] auto shouldBe(bool condition,
                            std::string_view message,
                            std::source_location location = std::source_location::current()) -> bool;

/// @brief Aborts with context if the condition is false. Active in Release.
void expect(bool condition,
            std::string_view message,
            std::source_location location = std::source_location::current()) noexcept;

/// @brief Logs a warning if the condition is true; returns whether it is false.
[[nodiscard]] auto shouldNotBe(bool condition,
                               std::string_view message,
                               std::source_location location = std::source_location::current()) -> bool;

/// @brief Aborts if the condition is true. Active in Release.
void expectNot(bool condition,
               std::string_view message,
               std::source_location location = std::source_location::current()) noexcept;

/// @brief Checks equality without copying or consuming either operand.
/// @return True on equality; otherwise logs a warning at the caller's location.
template <typename Value, typename Expected>
requires requires(const Value& value, const Expected& expected) {
    { value == expected } -> std::convertible_to<bool>;
}
[[nodiscard]] auto shouldBe(const Value& value,
                            const Expected& expected,
                            std::string_view message,
                            std::source_location location = std::source_location::current()) -> bool
{
    return shouldBe(value == expected, message, location);
}

/// @brief Checks inequality, including a pointer against nullptr, without consuming operands.
/// @return True on inequality; otherwise logs a warning at the caller's location.
template <typename Value, typename Forbidden>
requires requires(const Value& value, const Forbidden& forbidden) {
    { value == forbidden } -> std::convertible_to<bool>;
}
[[nodiscard]] auto shouldNotBe(const Value& value,
                               const Forbidden& forbidden,
                               std::string_view message,
                               std::source_location location = std::source_location::current()) -> bool
{
    return shouldBe(!(value == forbidden), message, location);
}

/// @brief Evaluates a predicate once against a borrowed value; warns if it returns false.
/// @return The predicate's result. Predicate exceptions propagate.
template <typename Value, typename Predicate>
requires std::predicate<Predicate, const Value&>
[[nodiscard]] auto shouldBe(const Value& value,
                            Predicate&& predicate,
                            std::string_view message,
                            std::source_location location = std::source_location::current()) -> bool
{
    return shouldBe(std::invoke(std::forward<Predicate>(predicate), value), message, location);
}

/// @brief Evaluates a predicate once; warns if the value satisfies the forbidden predicate.
/// @return True when the predicate returns false. Predicate exceptions propagate.
template <typename Value, typename Predicate>
requires std::predicate<Predicate, const Value&>
[[nodiscard]] auto shouldNotBe(const Value& value,
                               Predicate&& predicate,
                               std::string_view message,
                               std::source_location location = std::source_location::current()) -> bool
{
    return shouldNotBe(std::invoke(std::forward<Predicate>(predicate), value), message, location);
}

/// @brief Takes a value and returns it if equal to the required value; otherwise aborts.
/// @return An owned value. Move move-only arguments explicitly; comparison exceptions propagate.
template <typename Value, typename Expected>
requires requires(const Value& value, const Expected& expected) {
    { value == expected } -> std::convertible_to<bool>;
}
[[nodiscard]] auto expect(Value value,
                          const Expected& expected,
                          std::string_view message,
                          std::source_location location = std::source_location::current()) -> Value
{
    expect(value == expected, message, location);
    return value;
}

/// @brief Takes a value and returns it if different from the forbidden value; otherwise aborts.
/// @return An owned value. Supports pointer/nullptr checks; comparison exceptions propagate.
template <typename Value, typename Forbidden>
requires requires(const Value& value, const Forbidden& forbidden) {
    { value == forbidden } -> std::convertible_to<bool>;
}
[[nodiscard]] auto expectNot(Value value,
                             const Forbidden& forbidden,
                             std::string_view message,
                             std::source_location location = std::source_location::current()) -> Value
{
    expect(!(value == forbidden), message, location);
    return value;
}

/// @brief Takes a value and returns it if the predicate succeeds; otherwise aborts.
/// @return An owned value. The predicate is invoked once; its exceptions propagate.
template <typename Value, typename Predicate>
requires std::predicate<Predicate, const Value&>
[[nodiscard]] auto expect(Value value,
                          Predicate&& predicate,
                          std::string_view message,
                          std::source_location location = std::source_location::current()) -> Value
{
    expect(std::invoke(std::forward<Predicate>(predicate), std::as_const(value)), message, location);
    return value;
}

/// @brief Takes a value and returns it if the forbidden predicate fails; otherwise aborts.
/// @return An owned value. The predicate is invoked once; its exceptions propagate.
template <typename Value, typename Predicate>
requires std::predicate<Predicate, const Value&>
[[nodiscard]] auto expectNot(Value value,
                             Predicate&& predicate,
                             std::string_view message,
                             std::source_location location = std::source_location::current()) -> Value
{
    expectNot(std::invoke(std::forward<Predicate>(predicate), std::as_const(value)), message, location);
    return value;
}

namespace detail
{
template <typename Error>
[[noreturn]] void panicExpected(std::string_view message, const Error& error, std::source_location location) noexcept
{
    // Diagnostic formatting must never replace the checked fatal path with an exception.
    try
    {
        if constexpr (std::formattable<Error, char>)
        {
            panic(std::format("{}: {}", message, error), location);
        }
        else if constexpr (requires {
                               { error.message } -> std::convertible_to<std::string_view>;
                           })
        {
            panic(std::format("{}: {}", message, std::string_view {error.message}), location);
        }
        else if constexpr (requires {
                               { error.message() } -> std::convertible_to<std::string_view>;
                           })
        {
            panic(std::format("{}: {}", message, error.message()), location);
        }
    }
    catch (...)
    {
        // Fall back to the caller's context even if formatting or allocation failed.
        panic(message, location);
    }
    panic(message, location);
}
}

/// @brief Takes an expected by value and returns its owned value, or aborts with error context.
/// Move a move-only result into this function. No reference into a temporary escapes.
/// Use ordinary branching or expected's monadic operations for recoverable errors.
/// @return An owned value, or void for expected<void, Error>.
template <typename Value, typename Error>
[[nodiscard]] auto expect(std::expected<Value, Error> result,
                          std::string_view message,
                          std::source_location location = std::source_location::current()) -> Value
{
    if (!result)
    {
        detail::panicExpected(message, result.error(), location);
    }
    if constexpr (!std::is_void_v<Value>)
    {
        return *std::move(result);
    }
}

/// @brief Takes an optional by value and returns its owned value, or aborts on absence.
/// @return An owned value; move a move-only optional into this function.
template <typename Value>
[[nodiscard]] auto expect(std::optional<Value> result,
                          std::string_view message,
                          std::source_location location = std::source_location::current()) -> Value
{
    if (!result)
    {
        panic(message, location);
    }
    return *std::move(result);
}

}
