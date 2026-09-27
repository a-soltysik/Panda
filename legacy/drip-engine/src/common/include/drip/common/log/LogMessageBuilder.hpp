#pragma once

#include <fmt/base.h>
#include <fmt/format.h>

#include <boost/exception/detail/exception_ptr.hpp>
#include <boost/stacktrace/stacktrace.hpp>
#include <source_location>
#include <string_view>

#include "Logger.hpp"

namespace drip::common::log
{

namespace detail
{

class LogMessageBuilder
{
public:
    explicit LogMessageBuilder(std::string message,
                               const std::source_location& location,
                               Logger::Level level,
                               bool enabled);
    LogMessageBuilder(const LogMessageBuilder&) = delete;
    LogMessageBuilder(LogMessageBuilder&&) = delete;
    auto operator=(const LogMessageBuilder&) = delete;
    auto operator=(LogMessageBuilder&&) = delete;
    ~LogMessageBuilder();

    template <typename T>
    auto withException(const T& exception) -> LogMessageBuilder&
    {
        return withCurrentException(boost::make_exception_ptr(exception));
    }

    auto withCurrentException(boost::exception_ptr exception = boost::current_exception()) -> LogMessageBuilder&;
    auto withStacktrace(boost::stacktrace::stacktrace stacktrace = boost::stacktrace::stacktrace {})
        -> LogMessageBuilder&;
    auto withStacktraceFromCurrentException(
        boost::stacktrace::stacktrace = boost::stacktrace::stacktrace::from_current_exception()) -> LogMessageBuilder&;
    auto asString() -> std::string;

private:
    void log() noexcept;

    std::string _message;
    std::source_location _location;
    Logger::Level _level;
    bool _enabled;
    bool _asString = false;
};

}

template <typename... Args>
struct Debug : detail::LogMessageBuilder
{
    explicit Debug(std::string_view format,
                   Args&&... args,
                   const std::source_location& location = std::source_location::current()) noexcept;
};

template <typename... Args>
struct Info : detail::LogMessageBuilder
{
    explicit Info(std::string_view format,
                  Args&&... args,
                  const std::source_location& location = std::source_location::current()) noexcept;
};

template <typename... Args>
struct Warning : detail::LogMessageBuilder
{
    explicit Warning(std::string_view format,
                     Args&&... args,
                     const std::source_location& location = std::source_location::current()) noexcept;
};

template <typename... Args>
struct Error : detail::LogMessageBuilder
{
    explicit Error(std::string_view format,
                   Args&&... args,
                   const std::source_location& location = std::source_location::current()) noexcept;
};

template <typename... Args>
Debug<Args...>::Debug(std::string_view format, Args&&... args, const std::source_location& location) noexcept
    : LogMessageBuilder(Logger::instance().shouldLog(Logger::Level::Debug)
                            ? fmt::format(fmt::runtime(format), std::forward<Args>(args)...)
                            : std::string {},
                        location,
                        Logger::Level::Debug,
                        Logger::instance().shouldLog(Logger::Level::Debug))
{
}

template <typename... Args>
Info<Args...>::Info(std::string_view format, Args&&... args, const std::source_location& location) noexcept
    : LogMessageBuilder(Logger::instance().shouldLog(Logger::Level::Info)
                            ? fmt::format(fmt::runtime(format), std::forward<Args>(args)...)
                            : std::string {},
                        location,
                        Logger::Level::Info,
                        Logger::instance().shouldLog(Logger::Level::Info))
{
}

template <typename... Args>
Warning<Args...>::Warning(std::string_view format, Args&&... args, const std::source_location& location) noexcept
    : LogMessageBuilder(Logger::instance().shouldLog(Logger::Level::Warning)
                            ? fmt::format(fmt::runtime(format), std::forward<Args>(args)...)
                            : std::string {},
                        location,
                        Logger::Level::Warning,
                        Logger::instance().shouldLog(Logger::Level::Warning))
{
}

template <typename... Args>
Error<Args...>::Error(std::string_view format, Args&&... args, const std::source_location& location) noexcept
    : LogMessageBuilder(Logger::instance().shouldLog(Logger::Level::Error)
                            ? fmt::format(fmt::runtime(format), std::forward<Args>(args)...)
                            : std::string {},
                        location,
                        Logger::Level::Error,
                        Logger::instance().shouldLog(Logger::Level::Error))
{
}

template <typename... Args>
Debug(std::string_view, Args&&...) -> Debug<Args...>;

template <typename... Args>
Info(std::string_view, Args&&...) -> Info<Args...>;

template <typename... Args>
Warning(std::string_view, Args&&...) -> Warning<Args...>;

template <typename... Args>
Error(std::string_view, Args&&...) -> Error<Args...>;

}
