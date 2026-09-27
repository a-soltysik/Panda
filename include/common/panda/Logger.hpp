#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <mutex>
#include <ostream>
#include <source_location>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace panda::log
{

/// @brief Ordered severity; Off disables ordinary logging.
enum class Level : std::uint8_t
{
    Debug,
    Info,
    Warning,
    Error,
    Off
};

/// @brief Owned diagnostic text with call-site metadata and wall-clock time.
struct Entry
{
    std::string message;
    std::source_location location;
    std::chrono::system_clock::time_point time;
    Level level {Level::Info};
};

/// @brief Sink operation failure, including stream or formatting failure.
enum class SinkError : std::uint8_t
{
    WriteFailed,
    FlushFailed
};

/// @brief Synchronous destination. Called under the logger lock; never reenter that logger.
/// References to entries are borrowed only for the duration of write().
class Sink
{
public:
    Sink() = default;

    Sink(const Sink&) = delete;

    Sink(Sink&&) = delete;

    auto operator=(const Sink&) -> Sink& = delete;

    auto operator=(Sink&&) -> Sink& = delete;

    virtual ~Sink() = default;

    /// @brief Writes a borrowed entry; reports failure without throwing.
    [[nodiscard]] virtual auto write(const Entry& entry) noexcept -> std::expected<void, SinkError> = 0;

    /// @brief Flushes pending output; reports failure without throwing.
    [[nodiscard]] virtual auto flush() noexcept -> std::expected<void, SinkError> = 0;
};

/// @brief Borrows a stream that must outlive the sink. External access needs synchronization.
class StreamSink final : public Sink
{
public:
    /// @brief Uses the supplied stream without taking ownership.
    explicit StreamSink(std::ostream& stream);

    /// @brief Formats and writes an entry; stream or formatting failure returns WriteFailed.
    [[nodiscard]] auto write(const Entry& entry) noexcept -> std::expected<void, SinkError> override;

    /// @brief Flushes the borrowed stream; stream failure returns FlushFailed.
    [[nodiscard]] auto flush() noexcept -> std::expected<void, SinkError> override;

private:
    std::ostream& _stream;
};

/// @brief Owns an append-only file; parent directories must already exist.
class FileSink final : public Sink
{
public:
    /// @brief Opens a file, returning an actionable diagnostic on failure.
    /// @return An owning sink or an error containing the requested path.
    [[nodiscard]] static auto open(const std::filesystem::path& path)
        -> std::expected<std::unique_ptr<FileSink>, std::string>;

    /// @brief Formats and appends an entry; stream or formatting failure returns WriteFailed.
    [[nodiscard]] auto write(const Entry& entry) noexcept -> std::expected<void, SinkError> override;

    /// @brief Flushes pending file output; stream failure returns FlushFailed.
    [[nodiscard]] auto flush() noexcept -> std::expected<void, SinkError> override;

private:
    explicit FileSink(std::ofstream stream);

    std::ofstream _stream;
};

/// @brief Thread-safe synchronous logger. Writes/configuration can block on sinks.
/// Logging allocates; no worker thread or implicit periodic flush is used.
/// Sink failures go to stderr and do not prevent delivery to other sinks.
class Logger final
{
public:
    using SinkId = std::size_t;

    /// @brief Starts enabled at Info with an optional stderr sink.
    explicit Logger(bool console = true);

    Logger(const Logger&) = delete;

    Logger(Logger&&) = delete;

    auto operator=(const Logger&) -> Logger& = delete;

    auto operator=(Logger&&) -> Logger& = delete;

    /// @brief Flushes before destroying sinks; no concurrent users may remain.
    ~Logger();

    /// @brief Process logger, initialized on first use.
    static auto instance() -> Logger&;

    /// @brief Sets the minimum severity; Off disables ordinary entries.
    void setLevel(Level level) noexcept;

    /// @brief Checks the current filter; concurrent changes can affect the eventual write.
    [[nodiscard]] auto shouldLog(Level level) const noexcept -> bool;

    /// @brief Transfers sink ownership. A null sink is a fatal programming error.
    /// @return An identifier valid for this logger's lifetime.
    [[nodiscard]] auto addSink(std::unique_ptr<Sink> sink) -> SinkId;

    /// @brief Flushes and removes a sink; returns false for an unknown identifier.
    [[nodiscard]] auto removeSink(SinkId sinkId) -> bool;

    /// @brief Delivers owned text and metadata. Error entries are flushed immediately.
    void write(Level level, std::string message, std::source_location location = std::source_location::current());

    /// @brief Adds up to 32 current-thread frames to an enabled entry, then delivers it.
    /// Trace collection/formatting failure preserves the original message. Other logging
    /// allocation failures may propagate. A catch-site trace does not identify a throw site.
    void writeWithStacktrace(Level level,
                             std::string message,
                             std::source_location location = std::source_location::current());

    /// @brief Flushes all sinks regardless of filtering.
    void flush();

private:
    std::atomic<Level> _level {Level::Info};
    std::mutex _mutex;
    std::vector<std::pair<SinkId, std::unique_ptr<Sink>>> _sinks;
    SinkId _nextId {0};
};

/// @brief Captures the caller while checking a format string at compile time.
template <typename... Args>
struct Format
{
    // Implicit literal conversion captures the call site before entering a variadic function.
    template <typename Text>
    // cppcheck-suppress noExplicitConstructor
    explicit(false) consteval Format(const Text& pattern,
                                     std::source_location callSite = std::source_location::current())
        : text {pattern},
          location {callSite}
    {
    }

    std::format_string<Args...> text;
    std::source_location location;
};

/// @brief Formats only enabled entries. Argument expressions are still evaluated by C++.
/// @param logger Destination logger.
/// @param level Entry severity.
/// @param format Checked text and caller location.
/// @param args Formatting arguments; formatting/allocation exceptions may propagate.
template <typename... Args>
void write(Logger& logger, Level level, Format<std::type_identity_t<Args>...> format, Args&&... args)
{
    if (logger.shouldLog(level))
    {
        logger.write(level, std::format(format.text, std::forward<Args>(args)...), format.location);
    }
}

/// @brief Formats and gathers a current-thread stack trace only for an enabled entry.
/// Argument expressions are still evaluated; formatting/allocation failures may propagate.
template <typename... Args>
void writeWithStacktrace(Logger& logger, Level level, Format<std::type_identity_t<Args>...> format, Args&&... args)
{
    if (logger.shouldLog(level))
    {
        logger.writeWithStacktrace(level, std::format(format.text, std::forward<Args>(args)...), format.location);
    }
}

/// @brief Writes an explicitly traced entry to the process logger at the requested severity.
/// Uses the current thread's stack, not the stack from an exception's throw site.
template <typename... Args>
void writeWithStacktrace(Level level, Format<std::type_identity_t<Args>...> format, Args&&... args)
{
    writeWithStacktrace(Logger::instance(), level, format, std::forward<Args>(args)...);
}

/// @brief Writes a debug entry to the process logger.
template <typename... Args>
void debug(Format<std::type_identity_t<Args>...> format, Args&&... args)
{
    write(Logger::instance(), Level::Debug, format, std::forward<Args>(args)...);
}

/// @brief Writes an informational entry to the process logger.
template <typename... Args>
void info(Format<std::type_identity_t<Args>...> format, Args&&... args)
{
    write(Logger::instance(), Level::Info, format, std::forward<Args>(args)...);
}

/// @brief Writes a warning entry to the process logger.
template <typename... Args>
void warning(Format<std::type_identity_t<Args>...> format, Args&&... args)
{
    write(Logger::instance(), Level::Warning, format, std::forward<Args>(args)...);
}

/// @brief Writes and immediately flushes an error entry to the process logger.
template <typename... Args>
void error(Format<std::type_identity_t<Args>...> format, Args&&... args)
{
    write(Logger::instance(), Level::Error, format, std::forward<Args>(args)...);
}

}
