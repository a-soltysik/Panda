#pragma once

#include <concepts>
#include <exception>
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

template <typename T, typename Exception = std::runtime_error>
auto expect(T&& result,
            const std::convertible_to<T> auto& expected,
            const char* message,
            std::source_location location = std::source_location::current()) -> void
{
    if (result != expected) [[unlikely]]
    {
        if constexpr (fmt::has_formatter<decltype(result), fmt::format_context>())
        {
            log::internal::LogDispatcher::log(log::Level::Error, fmt::format("{}: {}", message, result), location);
        }
        else
        {
            log::internal::LogDispatcher::log(log::Level::Error, fmt::format("{}", message), location);
        }
        throw Exception {message};
    }
}

template <Result T, typename Exception = std::runtime_error>
[[nodiscard]] auto expect(T&& result,
                          const typename ResultHelper<T>::Error& expected,
                          const char* message,
                          std::source_location location = std::source_location::current()) ->
    typename ResultHelper<T>::Ok
{
    if (result.result != expected) [[unlikely]]
    {
        if constexpr (fmt::has_formatter<decltype(result.result), fmt::format_context>())
        {
            log::internal::LogDispatcher::log(log::Level::Error, fmt::format("{}: {}", message, result.result), location);
        }
        else
        {
            log::internal::LogDispatcher::log(log::Level::Error, fmt::format("{}", message), location);
        }
        throw Exception {message};
    }
    return std::move(result.value);
}

template <typename T, typename Exception = std::runtime_error>
[[nodiscard]] auto expect(T&& value,
                          std::invocable<T> auto predicate,
                          const char* message,
                          std::source_location location = std::source_location::current()) -> T
{
    if (!predicate(std::forward<T>(value))) [[unlikely]]
    {
        log::internal::LogDispatcher::log(log::Level::Error, message, location);
        throw Exception {message};
    }
    return std::forward<T>(value);
}

template <typename T, typename Exception = std::runtime_error>
auto expectNot(T&& result,
               const std::convertible_to<T> auto& expected,
               const char* message,
               std::source_location location = std::source_location::current()) -> void
{
    if (result == expected) [[unlikely]]
    {
        if constexpr (fmt::has_formatter<decltype(result), fmt::format_context>())
        {
            log::internal::LogDispatcher::log(log::Level::Error, fmt::format("{}: {}", message, result), location);
        }
        else
        {
            log::internal::LogDispatcher::log(log::Level::Error, fmt::format("{}", message), location);
        }

        throw Exception {message};
    }
}

template <Result T, typename Exception = std::runtime_error>
[[nodiscard]] auto expectNot(T&& result,
                             const typename ResultHelper<T>::Error& expected,
                             const char* message,
                             std::source_location location = std::source_location::current()) ->
    typename ResultHelper<T>::Ok
{
    if (result.result == expected) [[unlikely]]
    {
        if constexpr (fmt::has_formatter<decltype(result.result), fmt::format_context>())
        {
            log::internal::LogDispatcher::log(log::Level::Error, fmt::format("{}: {}", message, result.result), location);
        }
        else
        {
            log::internal::LogDispatcher::log(log::Level::Error, fmt::format("{}", message), location);
        }
        throw Exception {message};
    }
    return std::move(result.value);
}

template <typename T, typename Exception = std::runtime_error>
[[nodiscard]] auto expectNot(T&& value,
                             std::invocable<T> auto predicate,
                             const char* message,
                             std::source_location location = std::source_location::current()) -> T
{
    if (predicate(std::forward<T>(value))) [[unlikely]]
    {
        log::internal::LogDispatcher::log(log::Level::Error, message, location);
        throw Exception {message};
    }
    return std::forward<T>(value);
}

template <typename T>
auto shouldBe(T&& result,
              const std::convertible_to<T> auto& expected,
              const char* message,
              std::source_location location = std::source_location::current()) -> bool
{
    if (result != expected) [[unlikely]]
    {
        if constexpr (fmt::has_formatter<decltype(result), fmt::format_context>())
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

template <typename T>
auto shouldNotBe(T&& result,
                 const std::convertible_to<T> auto& expected,
                 const char* message,
                 std::source_location location = std::source_location::current()) -> bool
{
    if (result == expected) [[unlikely]]
    {
        if constexpr (fmt::has_formatter<decltype(result), fmt::format_context>())
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

template <typename Exception = std::runtime_error>
auto panic(const char* message, std::source_location location = std::source_location::current())
{
    log::internal::LogDispatcher::log(log::Level::Error, message, location);
    throw Exception {message};
}

}