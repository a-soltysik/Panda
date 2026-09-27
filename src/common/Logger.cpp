#include "panda/Logger.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <ostream>
#include <source_location>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

#include "FunctionName.hpp"
#include "SourcePath.hpp"
#include "Stacktrace.hpp"
#include "panda/Assert.hpp"

namespace panda::log
{
namespace
{
auto levelTag(Level level) -> std::string_view
{
    switch (level)
    {
    case Level::Debug:
        return "DBG";
    case Level::Info:
        return "INF";
    case Level::Warning:
        return "WRN";
    case Level::Error:
        return "ERR";
    case Level::Off:
        return "OFF";
    }
    return "???";
}

auto writeStream(std::ostream& stream, const Entry& entry) noexcept -> std::expected<void, SinkError>
{
    try
    {
        const auto time = std::chrono::floor<std::chrono::milliseconds>(entry.time);
        const auto functionName = detail::FunctionName {entry.location.function_name()};
        stream << std::format("{:%FT%T}Z [{}] {}:{} ({}): {}\n",
                              time,
                              levelTag(entry.level),
                              detail::relativeSourcePath(entry.location.file_name()),
                              entry.location.line(),
                              functionName.view(),
                              entry.message);
        if (stream)
        {
            return {};
        }
    }
    catch (...)
    {
        // Streams can throw according to their exception mask; contain that at this adapter.
        return std::unexpected {SinkError::WriteFailed};
    }
    return std::unexpected {SinkError::WriteFailed};
}

auto flushStream(std::ostream& stream) noexcept -> std::expected<void, SinkError>
{
    try
    {
        stream.flush();
        if (stream)
        {
            return {};
        }
    }
    catch (...)
    {
        // The sink contract also covers streams configured to throw on failure.
        return std::unexpected {SinkError::FlushFailed};
    }
    return std::unexpected {SinkError::FlushFailed};
}

void reportSinkFailure(std::expected<void, SinkError> result) noexcept
{
    if (!result)
    {
        const auto* message = result.error() == SinkError::WriteFailed ? "[ERR] Panda log sink write failed\n"
                                                                       : "[ERR] Panda log sink flush failed\n";
        std::ignore = std::fputs(message, stderr);
        std::ignore = std::fflush(stderr);
    }
}

}

StreamSink::StreamSink(std::ostream& stream)
    : _stream {stream}
{
}

auto StreamSink::write(const Entry& entry) noexcept -> std::expected<void, SinkError>
{
    return writeStream(_stream, entry);
}

auto StreamSink::flush() noexcept -> std::expected<void, SinkError>
{
    return flushStream(_stream);
}

FileSink::FileSink(std::ofstream stream)
    : _stream {std::move(stream)}
{
}

auto FileSink::open(const std::filesystem::path& path) -> std::expected<std::unique_ptr<FileSink>, std::string>
{
    auto stream = std::ofstream {path, std::ios::app};
    if (!stream)
    {
        return std::unexpected {std::format("Cannot open log file '{}' for append", path.string())};
    }
    return std::unique_ptr<FileSink> {new FileSink {std::move(stream)}};
}

auto FileSink::write(const Entry& entry) noexcept -> std::expected<void, SinkError>
{
    return writeStream(_stream, entry);
}

auto FileSink::flush() noexcept -> std::expected<void, SinkError>
{
    return flushStream(_stream);
}

Logger::Logger(bool console)
{
    if (console)
    {
        std::ignore = addSink(std::make_unique<StreamSink>(std::cerr));
    }
}

Logger::~Logger()
{
    flush();
}

auto Logger::instance() -> Logger&
{
    static auto logger = Logger {};
    return logger;
}

void Logger::setLevel(Level level) noexcept
{
    _level.store(level, std::memory_order_relaxed);
}

auto Logger::shouldLog(Level level) const noexcept -> bool
{
    return level != Level::Off && level >= _level.load(std::memory_order_relaxed);
}

auto Logger::addSink(std::unique_ptr<Sink> sink) -> SinkId
{
    expect(sink != nullptr, "Cannot register a null log sink");
    const auto lock = std::scoped_lock {_mutex};
    expect(_nextId != std::numeric_limits<SinkId>::max(), "Log sink identifiers exhausted");
    const auto identifier = _nextId++;
    _sinks.emplace_back(identifier, std::move(sink));
    return identifier;
}

auto Logger::removeSink(SinkId sinkId) -> bool
{
    const auto lock = std::scoped_lock {_mutex};
    const auto found = std::ranges::find(_sinks, sinkId, &decltype(_sinks)::value_type::first);
    if (found == _sinks.end())
    {
        return false;
    }
    reportSinkFailure(found->second->flush());
    _sinks.erase(found);
    return true;
}

void Logger::write(Level level, std::string message, std::source_location location)
{
    if (!shouldLog(level))
    {
        return;
    }
    const auto entry = Entry {.message = std::move(message),
                              .location = location,
                              .time = std::chrono::system_clock::now(),
                              .level = level};
    const auto lock = std::scoped_lock {_mutex};
    for (const auto& [identifier, sink] : _sinks)
    {
        reportSinkFailure(sink->write(entry));
        if (level == Level::Error)
        {
            reportSinkFailure(sink->flush());
        }
    }
}

void Logger::writeWithStacktrace(Level level, std::string message, std::source_location location)
{
    if (!shouldLog(level))
    {
        return;
    }
    try
    {
        message += detail::stacktraceText();
    }
    catch (...)
    {
        // Additional diagnostics must not discard the original entry.
        std::ignore = std::fputs("[WRN] Stack trace unavailable; logging original entry\n", stderr);
    }
    write(level, std::move(message), location);
}

void Logger::flush()
{
    const auto lock = std::scoped_lock {_mutex};
    for (const auto& [identifier, sink] : _sinks)
    {
        reportSinkFailure(sink->flush());
    }
}

}
