#include <cstdio>
#include <cstdlib>
#include <exception>
#include <glm/ext/vector_uint2.hpp>
#include <panda/Context.hpp>
#include <panda/Logger.hpp>
#include <panda/tools/gui/Window.hpp>
#include <tuple>

namespace
{
void logError(const panda::Error& error)
{
    if (error.native)
    {
        panda::log::error("{}:{}: {} ({} status {})",
                          error.source.file_name(),
                          error.source.line(),
                          error.message,
                          error.native->api,
                          error.native->code);
        return;
    }
    panda::log::error("{}:{}: {}", error.source.file_name(), error.source.line(), error.message);
}

auto waitForRestoredWindow() -> bool
{
    if (auto waited = panda::tools::Window::waitForInput(); !waited)
    {
        const auto& error = waited.error();
        logError(error);
        return false;
    }
    return true;
}

auto run(panda::tools::Window& window, panda::Context& context) -> int
{
    panda::log::info("Application loop started");
    while (!window.shouldClose())
    {
        if (auto processed = window.processInput(); !processed)
        {
            const auto& error = processed.error();
            logError(error);
            return EXIT_FAILURE;
        }
        if (window.shouldClose())
        {
            break;
        }
        const auto frame = context.presentClearFrame();
        if (!frame)
        {
            const auto& error = frame.error();
            logError(error);
            return EXIT_FAILURE;
        }
        if (frame->status == panda::FrameResult::Status::Suspended && !waitForRestoredWindow())
        {
            return EXIT_FAILURE;
        }
    }
    panda::log::info("Application loop stopped");
    return EXIT_SUCCESS;
}
}

auto main() -> int
{
    try
    {
        static constexpr auto windowSize = glm::uvec2 {1280, 720};
        auto window = panda::tools::Window::create(windowSize, "Panda - Simple scene");
        if (!window)
        {
            const auto& error = window.error();
            logError(error);
            return EXIT_FAILURE;
        }

        auto context =
            panda::Context::createWithSurface(*window,
                                              {.applicationName = "Panda simple scene", .enableValidation = true});
        if (!context)
        {
            const auto& error = context.error();
            logError(error);
            return EXIT_FAILURE;
        }
        panda::log::info("Using Vulkan device '{}'", context->getDeviceInfo().name);

        return run(*window, *context);
    }
    catch (const std::exception& error)
    {
        std::ignore = std::fputs("Unexpected application failure: ", stderr);
        std::ignore = std::fputs(error.what(), stderr);
        std::ignore = std::fputc('\n', stderr);
        return EXIT_FAILURE;
    }
    catch (...)
    {
        std::ignore = std::fputs("Unexpected application failure\n", stderr);
        return EXIT_FAILURE;
    }
}
