#pragma once

#include <fmt/base.h>
#include <fmt/chrono.h>
#include <fmt/format.h>

#include <atomic>
#include <boost/exception/detail/exception_ptr.hpp>
#include <boost/thread/synchronized_value.hpp>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <source_location>
#include <span>
#include <string>
#include <string_view>
#include <thread>

#include "drip/common/utils/Timer.hpp"

namespace drip::common::log
{

class LogSink;

namespace detail
{
class LogMessageBuilder;
}

class Logger
{
public:
    using SinkId = uint32_t;

    enum class Level : uint8_t
    {
        Debug,
        Info,
        Warning,
        Error
    };

    struct Entry
    {
        std::string message;
        std::source_location location;
        std::chrono::system_clock::time_point time;
        Level level;
    };

    Logger(const Logger&) = delete;
    auto operator=(const Logger&) = delete;
    Logger(Logger&&) = delete;
    auto operator=(Logger&&) = delete;
    ~Logger();

    void setLevels(std::span<const Level> newLevels);
    void start();
    void stop();
    [[nodiscard]] auto isRunning() const -> bool;
    [[nodiscard]] auto shouldLog(Level level) const -> bool;

    template <typename T, typename... Args>
    auto addSink(Args&&... args) -> SinkId
    {
        auto sinks = _sinks.synchronize();
        const auto id = _currentSinkId++;
        sinks->emplace(id, std::make_unique<T>(std::forward<Args>(args)...));
        return id;
    }

    void removeSink(SinkId sinkId);

    void setAutoFlush(bool enabled, std::chrono::milliseconds interval);
    void setFlushOnLevel(Level level);
    void setExceptionHandler(std::function<void(const boost::exception_ptr&)> exceptionHandler);
    void raiseException(const boost::exception_ptr& exception) const;

    static auto instance() -> Logger&;

private:
    friend class detail::LogMessageBuilder;

    Logger() = default;
    void log(Level level, std::string&& message, const std::source_location& location);
    void flush();
    [[nodiscard]] auto shouldFlushImmediately(Level level) const -> bool;

    std::set<Level> _levels = {Level::Debug, Level::Info, Level::Warning, Level::Error};
    boost::synchronized_value<std::map<SinkId, std::unique_ptr<LogSink>>> _sinks;
    std::atomic<SinkId> _currentSinkId = 0;

    std::atomic<bool> _isRunning = false;
    std::chrono::milliseconds _autoFlushInterval {5000};
    Level _immediateFlushLevel = Level::Error;

    std::unique_ptr<utils::Timer> _autoFlushTimer;
    std::function<void(const boost::exception_ptr&)> _exceptionHandler = [](const auto&) {
    };
};

}

template <>
struct fmt::formatter<drip::common::log::Logger::Entry> : formatter<std::string_view>
{
    [[nodiscard]] static auto getLevelTag(drip::common::log::Logger::Level level) -> std::string_view;
    [[nodiscard]] static auto getFunctionName(std::string_view function) -> std::string_view;

    template <typename FormatContext>
    [[nodiscard]] auto format(const drip::common::log::Logger::Entry& entry, FormatContext& ctx) const
        -> decltype(ctx.out())
    {
        return formatter<std::string_view>::format(fmt::format("{:%H:%M:%S} {} {}:{}, {}",
                                                               entry.time,
                                                               getLevelTag(entry.level),
                                                               getFunctionName(entry.location.function_name()),
                                                               entry.location.line(),
                                                               entry.message),
                                                   ctx);
    }
};
