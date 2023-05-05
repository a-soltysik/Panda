#include "App.h"

#include "gfx/api/vulkan/Vulkan.h"

namespace panda
{

auto App::run() -> int
{
    initializeLogger();

    window = std::make_unique<Window>();
    api = std::make_unique<gfx::vulkan::Vulkan>(*window);

    mainLoop();
    return 0;
}

auto App::mainLoop() -> void
{
    while (!window->shouldClose())
    {
        if (!window->isMinimized())
        {
            api->render();
            window->processInput();
        }
        else [[unlikely]]
        {
            window->waitForInput();
        }
    }
}

auto App::initializeLogger() -> void
{
    std::set_terminate([](){
        log::Config::instance().file.terminate();
    });

    if (PD_DEBUG)
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

}