#include "drip/common/log/Logger.hpp"

#include <atomic>
#include <boost/exception/detail/exception_ptr.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <ranges>
#include <source_location>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "drip/common/log/sink/LogSink.hpp"
#include "drip/common/utils/Timer.hpp"

namespace drip::common::log
{

Logger::~Logger()
{
    stop();
}

auto Logger::instance() -> Logger&
{
    static Logger logger;
    return logger;
}

void Logger::log(Level level, std::string&& message, const std::source_location& location)
{
    if (!_isRunning)
    {
        return;
    }

    if (!_levels.contains(level))
    {
        return;
    }

    const auto entry = Entry {.message = std::move(message),
                              .location = location,
                              .time = std::chrono::system_clock::now(),
                              .level = level};

    {
        auto sinks = _sinks.synchronize();
        for (const auto& sink : *sinks | std::views::values)
        {
            sink->write(entry);
        }
    }

    if (shouldFlushImmediately(level))
    {
        flush();
    }
}

void Logger::setLevels(std::span<const Level> newLevels)
{
    _levels = {newLevels.begin(), newLevels.end()};
}

void Logger::start()
{
    if (_isRunning)
    {
        return;
    }

    _isRunning = true;

    if (!_autoFlushTimer)
    {
        _autoFlushTimer = utils::Timer::periodic(_autoFlushInterval, [this] {
            flush();
        });
    }
}

void Logger::stop()
{
    if (!_isRunning)
    {
        return;
    }

    _isRunning = false;

    if (_autoFlushTimer)
    {
        _autoFlushTimer->stop();
    }
}

auto Logger::isRunning() const -> bool
{
    return _isRunning.load();
}

auto Logger::shouldLog(Level level) const -> bool
{
    return _isRunning.load() && _levels.contains(level);
}

void Logger::removeSink(SinkId sinkId)
{
    auto sinks = _sinks.synchronize();
    sinks->erase(sinkId);
}

void Logger::raiseException(const boost::exception_ptr& exception) const
{
    _exceptionHandler(exception);
}

void Logger::setAutoFlush(bool enabled, std::chrono::milliseconds interval)
{
    _autoFlushInterval = interval;
    if (_autoFlushTimer)
    {
        _autoFlushTimer->stop();
        _autoFlushTimer.reset();
    }

    if (enabled && _isRunning)
    {
        _autoFlushTimer = utils::Timer::periodic(_autoFlushInterval, [this] {
            flush();
        });
    }
}

void Logger::setFlushOnLevel(Level level)
{
    _immediateFlushLevel = level;
}

void Logger::setExceptionHandler(std::function<void(const boost::exception_ptr&)> exceptionHandler)
{
    _exceptionHandler = std::move(exceptionHandler);
}

void Logger::flush()
{
    auto sinks = _sinks.synchronize();
    for (const auto& sink : *sinks | std::views::values)
    {
        sink->flush();
    }
}

auto Logger::shouldFlushImmediately(Level level) const -> bool
{
    return static_cast<uint8_t>(level) >= static_cast<uint8_t>(_immediateFlushLevel);
}

}

namespace
{

auto findStartOfFunctionName(std::string_view function) -> size_t
{
    const auto firstBracket = function.find('(');
    if (firstBracket == std::string_view::npos)
    {
        return 0;
    }
    const auto lastSpace = function.rfind(' ', firstBracket - 2);
    if (lastSpace == std::string_view::npos)
    {
        return 0;
    }
    return lastSpace + 1;
}

}

auto fmt::formatter<drip::common::log::Logger::Entry>::getLevelTag(drip::common::log::Logger::Level level)
    -> std::string_view
{
    using namespace std::string_view_literals;
    using enum drip::common::log::Logger::Level;
    switch (level)
    {
    case Debug:
        return "[DBG]"sv;
    case Info:
        return "[INF]"sv;
    case Warning:
        return "[WRN]"sv;
    case Error:
        return "[ERR]"sv;
    default:
        [[unlikely]] return "[???]"sv;
    }
}

auto fmt::formatter<drip::common::log::Logger::Entry>::getFunctionName(std::string_view function) -> std::string_view
{
    const auto start = findStartOfFunctionName(function);
    return function.substr(start, function.find('(') - start);
}
