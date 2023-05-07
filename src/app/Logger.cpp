#include <fmt/chrono.h>

#include <filesystem>

namespace panda::log
{

namespace
{

[[nodiscard]] constexpr auto getLevelTag(Level level) -> std::string_view
{
    using namespace std::string_view_literals;
    switch (level)
    {
    case Level::Debug:
        return "[DBG]"sv;
    case Level::Info:
        return "[INF]"sv;
    case Level::Warning:
        return "[WRN]"sv;
    case Level::Error:
        return "[ERR]"sv;
    default:
        [[unlikely]] return "[???]"sv;
    }
}

[[nodiscard]] constexpr auto getFunctionName(std::string_view function) -> std::string_view
{
    const auto nameWithReturnType = function.substr(0, function.find('('));
    return nameWithReturnType.substr(nameWithReturnType.rfind(' ') + 1);
}

}

namespace internal
{

auto LogDispatcher::log([[maybe_unused]] Level level, [[maybe_unused]] std::string_view message, [[maybe_unused]] const std::source_location& location) noexcept -> void
{
#ifndef PD_DISABLE_LOGS
    try
    {
        Config::instance().console.log(level, message);
        Config::instance().file.log(level, message, location);
    } catch (...) {
        log::Warning("Exception thrown during logging");
    }
#endif
}

}

auto Config::instance() -> Config&
{
    static Config config;
    return config;
}

auto Config::Console::log(Level level, std::string_view message) -> void
{
    if (!isStarted)
    {
        return;
    }
    if (levels.contains(level))
    {
        fmt::println(stderr, "{} {}", getLevelTag(level), message);
    }
}

auto Config::Console::setLevels(std::span<const Level> newLevels) -> void
{
    levels = {newLevels.begin(), newLevels.end()};
}

auto Config::Console::start() -> void
{
    isStarted = true;
}

auto Config::Console::stop() -> void
{
    isStarted = false;
}

auto Config::File::log(Level level, std::string_view message, const std::source_location& location) -> void
{
    if (!isStarted)
    {
        return;
    }
    if (levels.contains(level))
    {
        const auto time = std::chrono::system_clock::now();
        buffer.push_back(fmt::format("{:%H:%M:%S} {} {}:{}, {}\n",
                                     std::chrono::floor<std::chrono::microseconds>(time),
                                     getLevelTag(level),
                                     getFunctionName(location.function_name()),
                                     location.line(),
                                     message));

        if (buffer.size() >= bufferSize)
        {
            flush();
        }
    }
}

auto Config::File::setLevels(std::span<const Level> newLevels) -> void
{
    levels = {newLevels.begin(), newLevels.end()};
}

auto Config::File::start() -> void
{
    if (file)
    {
        isStarted = true;
        return;
    }

    const auto time = std::chrono::system_clock::now();
    static constexpr auto logsDir = std::string_view {"logs"};

    try
    {
        std::filesystem::create_directory(logsDir);
        const auto filename =
            fmt::format("{}/{:%F_%H_%M_%S.pdlog}", logsDir, std::chrono::floor<std::chrono::seconds>(time));
        file = std::make_unique<fmt::ostream>(fmt::output_file(filename));
        isStarted = true;

        Info("File logger started");
    }
    catch (const std::system_error& e)
    {
        isStarted = false;
        Error(e.what());
    }
}

auto Config::File::stop() -> void
{
    isStarted = false;
    flush();
}

auto Config::File::setBufferSize(size_t size) -> void
{
    bufferSize = size;
}

auto Config::File::flush() -> void
{
    for (const auto& line : buffer)
    {
        file->print("{}", line);
    }
    buffer.clear();
}

Config::File::~File()
{
    flush();
}

auto Config::File::terminate() -> void {
    flush();
    file->close();
}

}
