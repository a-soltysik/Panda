#include "App.h"

#include <csignal>

#include "gfx/api/vulkan/Vulkan.h"

namespace
{

[[nodiscard]] constexpr auto getSignalName(int signalValue) noexcept -> std::string_view
{
    switch (signalValue)
    {
    case SIGABRT:
        return "SIGABRT";
    case SIGFPE:
        return "SIGFPE";
    case SIGILL:
        return "SIGILL";
    case SIGINT:
        return "SIGINT";
    case SIGSEGV:
        return "SIGSEGV";
    case SIGTERM:
        return "SIGTERM";
    default:
        return "unknown";
    }
}

[[noreturn]] auto signalHandler(int signalValue) -> void
{
    panda::log::Error("Received {} signal", getSignalName(signalValue));
    panda::log::Config::instance().file.terminate();
    std::_Exit(signalValue);
}

}

namespace panda
{

auto App::run() -> int
{
    initializeLogger();
    registerSignalHandlers();

    _window = std::make_unique<Window>(glm::uvec2 {800, 600}, config::targetName.data());
    _api = std::make_unique<gfx::vulkan::Vulkan>(*_window);

    mainLoop();
    return 0;
}

auto App::mainLoop() -> void
{
    while (!_window->shouldClose()) [[likely]]
    {
        if (!_window->isMinimized())
        {
            _api->makeFrame();
            Window::processInput();
        }
        else [[unlikely]]
        {
            Window::waitForInput();
        }
    }
}

auto App::initializeLogger() -> void
{
    if constexpr (config::isDebug)
    {
        log::Config::instance().console.setLevels(std::array {log::Level::Warning, log::Level::Error});
        log::Config::instance().console.start();
    }
    else
    {
        log::Config::instance().file.setLevels(std::array {log::Level::Info, log::Level::Warning, log::Level::Error});
    }

    log::Config::instance().file.start();
}

auto App::registerSignalHandlers() -> void
{
    shouldNotBe(std::signal(SIGABRT, signalHandler), SIG_ERR, "Failed to register signal handler");
    shouldNotBe(std::signal(SIGFPE, signalHandler), SIG_ERR, "Failed to register signal handler");
    shouldNotBe(std::signal(SIGILL, signalHandler), SIG_ERR, "Failed to register signal handler");
    shouldNotBe(std::signal(SIGINT, signalHandler), SIG_ERR, "Failed to register signal handler");
    shouldNotBe(std::signal(SIGSEGV, signalHandler), SIG_ERR, "Failed to register signal handler");
    shouldNotBe(std::signal(SIGTERM, signalHandler), SIG_ERR, "Failed to register signal handler");
}

}
