#pragma once

#include <concepts>
#include <exception>
#include <source_location>
#include <string_view>
#include <utility>

namespace panda
{

template <typename T, typename = decltype(std::declval<T>().value), typename = decltype(std::declval<T>().result)>
struct ResultHelper
{
    using Ok = decltype(std::declval<T>().value);
    using Error = decltype(std::declval<T>().result);
};

template <typename T>
concept Result = requires {
    typename ResultHelper<T>::Ok;
    typename ResultHelper<T>::Error;
};

[[noreturn]] inline auto panic() noexcept
{
    std::terminate();
}

[[noreturn]] inline auto panic(std::string_view message,
                               std::source_location location = std::source_location::current())
{
    log::internal::LogDispatcher::log(log::Level::Error, fmt::format("Terminal error: {}", message), location);
    panic();
}

template <typename T>
auto expect(T&& result,
            const std::convertible_to<T> auto& expected,
            std::string_view message,
            std::source_location location = std::source_location::current()) -> void
{
    if (result != expected) [[unlikely]]
    {
        if constexpr (fmt::has_formatter<std::decay_t<T>, fmt::format_context>())
        {
            panic(fmt::format("{}: {}", message, result), location);
        }
        else
        {
            panic(message, location);
        }
    }
}

template <Result T>
[[nodiscard]] auto expect(T&& result,
                          const typename ResultHelper<T>::Error& expected,
                          std::string_view message,
                          std::source_location location = std::source_location::current()) ->
    typename ResultHelper<T>::Ok
{
    if (result.result != expected) [[unlikely]]
    {
        if constexpr (fmt::has_formatter<std::decay_t<decltype(result.result)>, fmt::format_context>())
        {
            panic(fmt::format("{}: {}", message, result.result), location);
        }
        else
        {
            panic(message, location);
        }
    }
    return std::move(result.value);
}

template <typename T>
[[nodiscard]] auto expect(T&& value,
                          std::invocable<T> auto predicate,
                          std::string_view message,
                          std::source_location location = std::source_location::current()) -> T
{
    if (!predicate(std::forward<T>(value))) [[unlikely]]
    {
        panic(message, location);
    }
    return std::forward<T>(value);
}

template <typename T>
auto expectNot(T&& result,
               const std::convertible_to<T> auto& expected,
               std::string_view message,
               std::source_location location = std::source_location::current()) -> void
{
    if (result == expected) [[unlikely]]
    {
        if constexpr (fmt::has_formatter<std::decay_t<T>, fmt::format_context>())
        {
            panic(fmt::format("{}: {}", message, result), location);
        }
        else
        {
            panic(message, location);
        }
    }
}

template <Result T>
[[nodiscard]] auto expectNot(T&& result,
                             const typename ResultHelper<T>::Error& expected,
                             std::string_view message,
                             std::source_location location = std::source_location::current()) ->
    typename ResultHelper<T>::Ok
{
    if (result.result == expected) [[unlikely]]
    {
        if constexpr (fmt::has_formatter<std::decay_t<decltype(result.result)>, fmt::format_context>())
        {
            panic(fmt::format("{}: {}", message, result.result), location);
        }
        else
        {
            panic(message, location);
        }
    }
    return std::move(result.value);
}

template <typename T>
[[nodiscard]] auto expectNot(T&& value,
                             std::invocable<T> auto predicate,
                             std::string_view message,
                             std::source_location location = std::source_location::current()) -> T
{
    if (predicate(std::forward<T>(value))) [[unlikely]]
    {
        panic(message, location);
    }
    return std::forward<T>(value);
}

template <typename T>
auto shouldBe(T&& result,
              const std::convertible_to<T> auto& expected,
              std::string_view message = "-",
              std::source_location location = std::source_location::current()) -> bool
{
    if (result != expected) [[unlikely]]
    {
        if constexpr (fmt::has_formatter<std::decay_t<T>, fmt::format_context>())
        {
            log::internal::LogDispatcher::log(log::Level::Error, fmt::format("{}: {}", message, result), location);
        }
        else
        {
            log::internal::LogDispatcher::log(log::Level::Error, fmt::format("{}", message), location);
        }
        return false;
    }
    return true;
}

inline auto shouldBe(bool result,
                     std::string_view message = "-",
                     std::source_location location = std::source_location::current()) -> bool
{
    if (!result) [[unlikely]]
    {
        log::internal::LogDispatcher::log(log::Level::Error, fmt::format("{}", message), location);
        return false;
    }
    return true;
}

template <typename T>
auto shouldNotBe(T&& result,
                 const std::convertible_to<T> auto& expected,
                 std::string_view message,
                 std::source_location location = std::source_location::current()) -> bool
{
    if (result == expected) [[unlikely]]
    {
        if constexpr (fmt::has_formatter<std::decay_t<T>, fmt::format_context>())
        {
            log::internal::LogDispatcher::log(log::Level::Error, fmt::format("{}: {}", message, result), location);
        }
        else
        {
            log::internal::LogDispatcher::log(log::Level::Error, fmt::format("{}", message), location);
        }
        return false;
    }
    return true;
}

}