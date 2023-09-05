#pragma once

#include <fmt/core.h>
#include <fmt/os.h>

#include <set>
#include <source_location>
#include <span>
#include <vector>

namespace panda::log
{

enum class Level
{
    Debug,
    Info,
    Warning,
    Error
};

template <typename... Args>
struct Debug
{
    explicit Debug(std::string_view format,
                   Args&&... args,
                   std::source_location location = std::source_location::current()) noexcept;
};

template <typename... Args>
struct Info
{
    explicit Info(std::string_view format,
                  Args&&... args,
                  std::source_location location = std::source_location::current()) noexcept;
};

template <typename... Args>
struct Warning
{
    explicit Warning(std::string_view format,
                     Args&&... args,
                     std::source_location location = std::source_location::current()) noexcept;
};

template <typename... Args>
struct Error
{
    explicit Error(std::string_view format,
                   Args&&... args,
                   std::source_location location = std::source_location::current()) noexcept;
};

namespace internal
{
struct LogDispatcher;
}

class Config
{
public:
    class Console
    {
    public:
        auto setLevels(std::span<const Level> newLevels) -> void;
        auto start() -> void;
        auto stop() -> void;

    private:
        friend struct internal::LogDispatcher;

        auto log(Level level, std::string_view message) -> void;  //cppcheck-suppress unusedPrivateFunction

        std::set<Level> levels = {Level::Debug, Level::Info, Level::Warning, Level::Error};
        bool isStarted = false;
    };

    class File
    {
    public:
        File() = default;
        File(const File&) = delete;
        File(File&&) = delete;
        auto operator=(const File&) -> File& = delete;
        auto operator=(File&&) -> File& = delete;
        ~File();

        auto setLevels(std::span<const Level> newLevels) -> void;
        auto start() -> void;
        auto stop() -> void;
        auto terminate() -> void;
        auto setBufferSize(size_t size) -> void;

    private:
        friend struct internal::LogDispatcher;

        auto log(Level level, std::string_view message, const std::source_location& location) -> void;
        auto flush() -> void;

        std::set<Level> _levels = {Level::Debug, Level::Info, Level::Warning, Level::Error};
        std::vector<std::string> _buffer;
        std::unique_ptr<fmt::ostream> _file;
        size_t _bufferSize = 100;
        bool _isStarted = false;
    };

    static auto instance() -> Config&;

    Console console;
    File file;

private:
    Config() = default;
};

namespace internal
{

struct LogDispatcher
{
    static auto log(Level level, std::string_view message, const std::source_location& location) noexcept -> void;
};

}

template <typename... Args>
Debug<Args...>::Debug(std::string_view format, Args&&... args, std::source_location location) noexcept
{
    internal::LogDispatcher::log(Level::Debug,
                                 fmt::format(fmt::runtime(format), std::forward<Args>(args)...),
                                 location);
}

template <typename... Args>
Info<Args...>::Info(std::string_view format, Args&&... args, std::source_location location) noexcept
{
    internal::LogDispatcher::log(Level::Info, fmt::format(fmt::runtime(format), std::forward<Args>(args)...), location);
}

template <typename... Args>
Warning<Args...>::Warning(std::string_view format, Args&&... args, std::source_location location) noexcept
{
    internal::LogDispatcher::log(Level::Warning,
                                 fmt::format(fmt::runtime(format), std::forward<Args>(args)...),
                                 location);
}

template <typename... Args>
Error<Args...>::Error(std::string_view format, Args&&... args, std::source_location location) noexcept
{
    internal::LogDispatcher::log(Level::Error,
                                 fmt::format(fmt::runtime(format), std::forward<Args>(args)...),
                                 location);
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