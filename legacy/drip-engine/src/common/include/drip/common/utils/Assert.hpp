#pragma once

#include <fmt/base.h>
#include <fmt/format.h>

#include <concepts>
#include <cstdlib>
#include <optional>
#include <source_location>
#include <string_view>
#include <type_traits>
#include <utility>

#include "drip/common/log/LogMessageBuilder.hpp"
#include "drip/common/log/Logger.hpp"

namespace drip::common
{
template <typename T>
concept Result = requires(T result) {
    { result.value };
    { result.result } -> std::equality_comparable;
};

template <Result T>
struct ResultHelper
{
    using Ok = decltype(std::declval<T>().value);
    using Error = decltype(std::declval<T>().result);
};

[[noreturn]] inline auto panic() noexcept
{
    std::abort();
}

[[noreturn]] inline auto panic(std::string_view message,
                               std::source_location location = std::source_location::current()) noexcept
{
    log::detail::LogMessageBuilder {fmt::format("Terminal error: {}", message),
                                    location,
                                    log::Logger::Level::Error,
                                    log::Logger::instance().shouldLog(log::Logger::Level::Error)};
    panic();
}

namespace detail
{

template <typename... Args, typename T>
requires fmt::is_formattable<std::decay_t<T>>::value
auto formatMessageWithValue(std::string_view format, const T& value, Args&&... args) -> std::string
{
    return fmt::format("{}: {}", fmt::format(fmt::runtime(format), std::forward<Args>(args)...), value);
}

template <typename... Args>
auto formatMessageWithoutValue(std::string_view format, Args&&... args) -> std::string
{
    return fmt::format(fmt::runtime(format), std::forward<Args>(args)...);
}

template <typename T, typename... Args>
auto formatMessage(std::string_view format, const T* value, Args&&... args) -> std::string
{
    if constexpr (fmt::is_formattable<std::decay_t<T>>::value)
    {
        if (value != nullptr)
        {
            return formatMessageWithValue(format, *value, std::forward<Args>(args)...);
        }
    }
    return formatMessageWithoutValue(format, std::forward<Args>(args)...);
}

template <typename T, typename... Args>
void logMessage(
    log::Logger::Level level, std::string_view format, const T* value, std::source_location location, Args&&... args)
{
    log::detail::LogMessageBuilder {formatMessage(format, value, std::forward<Args>(args)...),
                                    location,
                                    level,
                                    log::Logger::instance().shouldLog(level)};
}

template <typename T>
struct ValueTypeResolver
{
    using type = T;
};

template <Result T>
struct ValueTypeResolver<T>
{
    using type = ResultHelper<T>::Ok;
};

template <typename T>
struct ValueTypeResolver<std::optional<T>>
{
    using type = T;
};

template <typename T>
using ValueTypeT = ValueTypeResolver<std::decay_t<T>>::type;

}

template <typename T, typename... Args>
class Expect
{
public:
    using ValueType = detail::ValueTypeT<T>;

    template <typename U = T, typename E>
    requires std::equality_comparable_with<T, E> && (!std::invocable<E, const std::remove_reference_t<T>&>)
    Expect(U&& result,
           E&& expected,
           std::string_view format,
           Args&&... args,
           std::source_location location = std::source_location::current()) noexcept
        : _result(getResult(
              std::forward<U>(result), std::forward<E>(expected), location, format, std::forward<Args>(args)...))
    {
    }

    template <typename U = T>
    Expect(U&& result,
           const ResultHelper<U>::Error& expected,
           std::string_view format,
           Args&&... args,
           std::source_location location = std::source_location::current()) noexcept
        : _result(getResult(std::forward<U>(result), expected, location, format, std::forward<Args>(args)...))
    {
    }

    template <typename U = T, typename P>
    requires std::invocable<P, const std::remove_reference_t<U>&>
    Expect(U&& result,
           P&& predicate,
           std::string_view format,
           Args&&... args,
           std::source_location location = std::source_location::current()) noexcept
        : _result(getResult(
              std::forward<U>(result), std::forward<P>(predicate), location, format, std::forward<Args>(args)...))
    {
    }

    template <typename U = T>
    requires(std::convertible_to<U, bool>)
    Expect(U&& result,
           std::string_view format,
           Args&&... args,
           std::source_location location = std::source_location::current()) noexcept
        : _result(getResult(std::forward<U>(result), location, format, std::forward<Args>(args)...))
    {
    }

    template <typename U = T>
    Expect(std::optional<U>&& result,
           std::string_view format,
           Args&&... args,
           std::source_location location = std::source_location::current()) noexcept
        : _result(getResult(std::move(result), location, format, std::forward<Args>(args)...))
    {
    }

    [[nodiscard]] auto result() && noexcept -> ValueType
    {
        return std::move(_result);
    }

    auto result() const& = delete;

private:
    template <typename U = T, typename E>
    requires std::equality_comparable_with<T, E>
    auto getResult(U&& result, E&& expected, std::source_location location, std::string_view format, Args&&... args)
        -> ValueType
    {
        if (result != expected) [[unlikely]]
        {
            panic(detail::formatMessage(format, &result, std::forward<Args>(args)...), location);
        }
        return std::forward<U>(result);
    }

    template <typename U = T>
    auto getResult(U&& result,
                   const ResultHelper<U>::Error& expected,
                   std::source_location location,
                   std::string_view format,
                   Args&&... args) noexcept -> ValueType
    {
        if (result.result != expected) [[unlikely]]
        {
            panic(detail::formatMessage(format, &result.result, std::forward<Args>(args)...), location);
        }
        return std::forward<U>(result).value;
    }

    template <typename U = T, typename P>
    requires std::invocable<P, const std::remove_reference_t<U>&>
    auto getResult(
        U&& result, P&& predicate, std::source_location location, std::string_view format, Args&&... args) noexcept
        -> ValueType
    {
        if (!predicate(result)) [[unlikely]]
        {
            panic(detail::formatMessage(format, static_cast<const void*>(nullptr), std::forward<Args>(args)...),
                  location);
        }
        return std::forward<U>(result);
    }

    template <typename U = T>
    requires(std::convertible_to<U, bool>)
    auto getResult(U&& result, std::source_location location, std::string_view format, Args&&... args) noexcept
        -> ValueType
    {
        if (!result) [[unlikely]]
        {
            panic(detail::formatMessage(format, static_cast<const void*>(nullptr), std::forward<Args>(args)...),
                  location);
        }

        return std::forward<U>(result);
    }

    template <typename U = T>
    auto getResult(std::optional<U>&& result,
                   std::source_location location,
                   std::string_view format,
                   Args&&... args) noexcept -> ValueType
    {
        if (!result.has_value()) [[unlikely]]
        {
            panic(detail::formatMessage(format, static_cast<const void*>(nullptr), std::forward<Args>(args)...),
                  location);
        }
        return std::move(result).value();
    }

    ValueType _result;
};

template <typename T, typename E, typename... Args>
requires std::equality_comparable_with<T, E> && (!std::invocable<E, const std::remove_reference_t<T>&>)
Expect(T&& result, E&& expected, std::string_view format, Args&&... args) -> Expect<T, Args...>;

template <typename T, typename... Args>
Expect(T&& result, const typename ResultHelper<T>::Error& expected, std::string_view format, Args&&... args)
    -> Expect<T, Args...>;

template <typename T, typename P, typename... Args>
requires std::invocable<P, const std::remove_reference_t<T>&>
Expect(T&& result, P&& predicate, std::string_view format, Args&&... args) -> Expect<T, Args...>;

template <typename T, typename... Args>
Expect(T&& result, std::string_view format, Args&&... args) -> Expect<T, Args...>;

template <typename T, typename... Args>
class ExpectNot
{
public:
    using ValueType = detail::ValueTypeT<T>;

    template <typename U = T>
    requires(std::convertible_to<U, bool>)
    ExpectNot(U&& result,
              std::string_view format,
              Args&&... args,
              std::source_location location = std::source_location::current()) noexcept
        : _result(getResult(std::forward<U>(result), location, format, std::forward<Args>(args)...))
    {
    }

    template <typename U = T, typename E>
    requires std::equality_comparable_with<T, E> && (!std::invocable<E, const std::remove_reference_t<T>&>)
    ExpectNot(U&& result,
              E&& notExpected,
              std::string_view format,
              Args&&... args,
              std::source_location location = std::source_location::current()) noexcept
        : _result(getResult(
              std::forward<U>(result), std::forward<E>(notExpected), location, format, std::forward<Args>(args)...))
    {
    }

    template <typename U = T>
    ExpectNot(U&& result,
              const ResultHelper<U>::Error& notExpected,
              std::string_view format,
              Args&&... args,
              std::source_location location = std::source_location::current()) noexcept
        : _result(getResult(std::forward<U>(result), notExpected, location, format, std::forward<Args>(args)...))
    {
    }

    template <typename U = T, typename P>
    requires std::invocable<P, const std::remove_reference_t<U>&>
    ExpectNot(U&& result,
              P&& predicate,
              std::string_view format,
              Args&&... args,
              std::source_location location = std::source_location::current()) noexcept
        : _result(getResult(
              std::forward<U>(result), std::forward<P>(predicate), location, format, std::forward<Args>(args)...))
    {
    }

    [[nodiscard]] auto result() && noexcept -> ValueType
    {
        return std::move(_result);
    }

    auto result() const& = delete;

private:
    template <typename U = T>
    requires(std::convertible_to<U, bool>)
    auto getResult(U&& result, std::source_location location, std::string_view format, Args&&... args) noexcept
        -> ValueType
    {
        if (result) [[unlikely]]
        {
            panic(detail::formatMessage(format, static_cast<const void*>(nullptr), std::forward<Args>(args)...),
                  location);
        }
        return std::forward<U>(result);
    }

    template <typename U = T, typename E>
    requires std::equality_comparable_with<T, E>
    auto getResult(
        U&& result, E&& notExpected, std::source_location location, std::string_view format, Args&&... args) noexcept
        -> ValueType
    {
        if (result == notExpected) [[unlikely]]
        {
            panic(detail::formatMessage(format, &result, std::forward<Args>(args)...), location);
        }
        return std::forward<U>(result);
    }

    template <typename U = T>
    auto getResult(U&& result,
                   const ResultHelper<U>::Error& notExpected,
                   std::source_location location,
                   std::string_view format,
                   Args&&... args) noexcept -> ValueType
    {
        if (result.result == notExpected) [[unlikely]]
        {
            panic(detail::formatMessage(format, &result.result, std::forward<Args>(args)...), location);
        }
        return std::forward<U>(result).value;
    }

    template <typename U = T, typename P>
    requires std::invocable<P, const std::remove_reference_t<U>&>
    auto getResult(
        U&& result, P&& predicate, std::source_location location, std::string_view format, Args&&... args) noexcept
        -> ValueType
    {
        if (predicate(result)) [[unlikely]]
        {
            panic(detail::formatMessage(format, static_cast<const void*>(nullptr), std::forward<Args>(args)...),
                  location);
        }
        return std::forward<U>(result);
    }

    ValueType _result;
};

template <typename T, typename... Args>
ExpectNot(T&& result, std::string_view format, Args&&... args) -> ExpectNot<T, Args...>;

template <typename T, typename E, typename... Args>
requires std::equality_comparable_with<T, E> && (!std::invocable<E, const std::remove_reference_t<T>&>)
ExpectNot(T&& result, E&& notExpected, std::string_view format, Args&&... args) -> ExpectNot<T, Args...>;

template <typename T, typename... Args>
ExpectNot(T&& result, const typename ResultHelper<T>::Error& notExpected, std::string_view format, Args&&... args)
    -> ExpectNot<T, Args...>;

template <typename T, typename P, typename... Args>
requires std::invocable<P, const std::remove_reference_t<T>&>
ExpectNot(T&& result, P&& predicate, std::string_view format, Args&&... args) -> ExpectNot<T, Args...>;

template <typename T, typename... Args>
class ShouldBe
{
public:
    template <typename U = T, typename E>
    requires std::equality_comparable_with<T, E> && (!std::invocable<E, const std::remove_reference_t<T>&>)
    ShouldBe(const U& result,
             E&& expected,
             std::string_view format,
             Args&&... args,
             std::source_location location = std::source_location::current()) noexcept
        : _success(getResult(result, std::forward<E>(expected), location, format, std::forward<Args>(args)...))
    {
    }

    template <typename U = T>
    requires(std::convertible_to<U, bool>)
    ShouldBe(U&& result,
             std::string_view format,
             Args&&... args,
             std::source_location location = std::source_location::current()) noexcept
        : _success(getResult(std::forward<U>(result), location, format, std::forward<Args>(args)...))
    {
    }

    template <typename U = T, typename P>
    requires std::invocable<P, const std::remove_reference_t<U>&>
    ShouldBe(const U& value,
             P&& predicate,
             std::string_view format,
             Args&&... args,
             std::source_location location = std::source_location::current()) noexcept
        : _success(getResult(value, std::forward<P>(predicate), location, format, std::forward<Args>(args)...))
    {
    }

    [[nodiscard]] auto result() const noexcept -> bool
    {
        return _success;
    }

private:
    template <typename U = T, typename E>
    requires std::equality_comparable_with<T, E>
    auto getResult(
        const U& result, E&& expected, std::source_location location, std::string_view format, Args&&... args) noexcept
        -> bool
    {
        if (result != expected) [[unlikely]]
        {
            detail::logMessage(log::Logger::Level::Error, format, &result, location, std::forward<Args>(args)...);
            return false;
        }
        return true;
    }

    template <typename U = T>
    requires(std::convertible_to<U, bool>)
    auto getResult(U&& result, std::source_location location, std::string_view format, Args&&... args) noexcept -> bool
    {
        if (!result) [[unlikely]]
        {
            detail::logMessage(log::Logger::Level::Warning,
                               format,
                               static_cast<const void*>(nullptr),
                               location,
                               std::forward<Args>(args)...);
            return false;
        }
        return true;
    }

    template <typename U = T, typename P>
    requires std::invocable<P, const std::remove_reference_t<U>&>
    auto getResult(
        const U& value, P&& predicate, std::source_location location, std::string_view format, Args&&... args) noexcept
        -> bool
    {
        if (!predicate(value)) [[unlikely]]
        {
            detail::logMessage(log::Logger::Level::Warning,
                               format,
                               static_cast<const void*>(nullptr),
                               location,
                               std::forward<Args>(args)...);
            return false;
        }
        return true;
    }

    bool _success;
};

template <typename T, typename E, typename... Args>
requires std::equality_comparable_with<T, E> && (!std::invocable<E, const std::remove_reference_t<T>&>)
ShouldBe(const T& result, E&& expected, std::string_view format, Args&&... args) -> ShouldBe<T, Args...>;

template <typename T, typename... Args>
ShouldBe(T&& result, std::string_view format, Args&&... args) -> ShouldBe<T, Args...>;

template <typename T, typename P, typename... Args>
requires std::invocable<P, const std::remove_reference_t<T>&>
ShouldBe(const T& value, P&& predicate, std::string_view format, Args&&... args) -> ShouldBe<T, Args...>;

template <typename T, typename... Args>
class ShouldNotBe
{
public:
    template <typename U = T>
    requires(std::convertible_to<U, bool>)
    ShouldNotBe(const U& result,
                std::string_view format,
                Args&&... args,
                std::source_location location = std::source_location::current()) noexcept
        : _success(getResult(std::forward<U>(result), location, format, std::forward<Args>(args)...))
    {
    }

    template <typename U = T, typename E>
    requires std::equality_comparable_with<T, E> && (!std::invocable<E, const std::remove_reference_t<T>&>)
    ShouldNotBe(const U& result,
                E&& notExpected,
                std::string_view format,
                Args&&... args,
                std::source_location location = std::source_location::current()) noexcept
        : _success(getResult(result, std::forward<E>(notExpected), location, format, std::forward<Args>(args)...))
    {
    }

    template <typename U = T>
    ShouldNotBe(const U& result,
                const ResultHelper<U>::Error& notExpected,
                std::string_view format,
                Args&&... args,
                std::source_location location = std::source_location::current()) noexcept
        : _success(getResult(result, notExpected, location, format, std::forward<Args>(args)...))
    {
    }

    template <typename U = T, typename P>
    requires std::invocable<P, const std::remove_reference_t<U>&>
    ShouldNotBe(const U& result,
                P&& predicate,
                std::string_view format,
                Args&&... args,
                std::source_location location = std::source_location::current()) noexcept
        : _success(getResult(result, std::forward<P>(predicate), location, format, std::forward<Args>(args)...))
    {
    }

    [[nodiscard]] auto result() const noexcept -> bool
    {
        return _success;
    }

private:
    template <typename U = T>
    requires(std::convertible_to<U, bool>)
    auto getResult(const U& result, std::source_location location, std::string_view format, Args&&... args) noexcept
        -> bool
    {
        if (result) [[unlikely]]
        {
            detail::logMessage(log::Logger::Level::Warning,
                               format,
                               static_cast<const void*>(nullptr),
                               location,
                               std::forward<Args>(args)...);
            return false;
        }
        return true;
    }

    template <typename U = T, typename E>
    requires std::equality_comparable_with<T, E>
    auto getResult(const U& result,
                   E&& notExpected,
                   std::source_location location,
                   std::string_view format,
                   Args&&... args) noexcept -> bool
    {
        if (result == notExpected) [[unlikely]]
        {
            detail::logMessage(log::Logger::Level::Warning, format, &result, location, std::forward<Args>(args)...);
            return false;
        }
        return true;
    }

    template <typename U = T>
    auto getResult(const U& result,
                   const ResultHelper<U>::Error& notExpected,
                   std::source_location location,
                   std::string_view format,
                   Args&&... args) noexcept -> bool
    {
        if (result.result == notExpected) [[unlikely]]
        {
            detail::logMessage(log::Logger::Level::Warning,
                               format,
                               &result.result,
                               location,
                               std::forward<Args>(args)...);
            return false;
        }
        return true;
    }

    template <typename U = T, typename P>
    requires std::invocable<P, const std::remove_reference_t<U>&>
    auto getResult(
        const U& result, P&& predicate, std::source_location location, std::string_view format, Args&&... args) noexcept
        -> bool
    {
        if (predicate(result)) [[unlikely]]
        {
            detail::logMessage(log::Logger::Level::Warning,
                               format,
                               static_cast<const void*>(nullptr),
                               location,
                               std::forward<Args>(args)...);
            return false;
        }
        return true;
    }

    bool _success;
};

template <typename T, typename... Args>
ShouldNotBe(const T& result, std::string_view format, Args&&... args) -> ShouldNotBe<T, Args...>;

template <typename T, typename E, typename... Args>
requires std::equality_comparable_with<T, E> && (!std::invocable<E, const std::remove_reference_t<T>&>)
ShouldNotBe(const T& result, E&& notExpected, std::string_view format, Args&&... args) -> ShouldNotBe<T, Args...>;

template <typename T, typename... Args>
ShouldNotBe(const T& result,
            const typename ResultHelper<T>::Error& notExpected,
            std::string_view format,
            Args&&... args) -> ShouldNotBe<T, Args...>;

template <typename T, typename P, typename... Args>
requires std::invocable<P, const std::remove_reference_t<T>&>
ShouldNotBe(const T& result, P&& predicate, std::string_view format, Args&&... args) -> ShouldNotBe<T, Args...>;

}

#if NDEBUG
#    define DRIP_DEBUG_EXPECT(condition) ((void) 0)
#    define DRIP_EXPECT(condition) drip::common::ShouldBe(condition, #condition)
#else
#    define DRIP_DEBUG_EXPECT(condition) drip::common::Expect(condition, #condition)
#    define DRIP_EXPECT(condition) drip::common::Expect(condition, #condition)
#endif
