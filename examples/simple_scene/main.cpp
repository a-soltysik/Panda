#include <cstdlib>
#include <exception>
#include <expected>
#include <glm/ext/vector_uint2.hpp>
#include <panda/Assert.hpp>
#include <panda/Context.hpp>
#include <panda/Error.hpp>
#include <panda/Logger.hpp>
#include <panda/tools/gui/Window.hpp>
#include <string_view>

namespace
{
auto fail(std::string_view message) noexcept -> int
{
    try
    {
        panda::log::error("{}", message);
    }
    catch (...)
    {
        panda::panic(message);
    }
    return EXIT_FAILURE;
}

auto run(panda::tools::Window& window, const panda::Context& context) -> panda::Result<void>
{
    while (!window.shouldClose())
    {
        const auto frame = context.presentClearFrame();
        if (!frame)
        {
            return std::unexpected {frame.error()};
        }
        // Sleep while minimized; otherwise poll without blocking.
        const auto input = frame->status == panda::FrameResult::Status::Suspended ? panda::tools::Window::waitForInput()
                                                                                  : window.processInput();
        if (!input)
        {
            return input;
        }
    }
    return {};
}
}

auto main() -> int
{
    try
    {
        static constexpr auto windowSize = glm::uvec2 {1280, 720};
        const auto result =
            panda::tools::Window::create(windowSize, "Panda - Simple scene").and_then([](panda::tools::Window window) {
                return panda::Context::createWithSurface(
                           window,
                           {.applicationName = "Panda simple scene", .enableValidation = true})
                    .and_then([&window](const panda::Context& context) {
                        return run(window, context);
                    });
            });
        return result ? EXIT_SUCCESS : fail(result.error().message);
    }
    catch (const std::exception& error)
    {
        return fail(error.what());
    }
    catch (...)
    {
        return fail("Unexpected application failure");
    }
}
