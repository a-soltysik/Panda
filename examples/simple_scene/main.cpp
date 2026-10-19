#include <chrono>
#include <cstdlib>
#include <exception>
#include <expected>
#include <glm/ext/vector_uint2.hpp>
#include <iostream>
#include <memory>
#include <panda/Assert.hpp>
#include <panda/CompletionPoint.hpp>
#include <panda/Context.hpp>
#include <panda/Error.hpp>
#include <panda/Logger.hpp>
#include <panda/tools/gui/Window.hpp>
#include <string_view>
#include <tuple>

namespace
{
constexpr auto frameWaitTimeout = std::chrono::seconds {10};

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

auto finishFrames(const panda::Context& context, panda::CompletionPoint last) -> panda::Result<void>
{
    if (!last.isValid())
    {
        return {};
    }
    return context.wait(last, frameWaitTimeout).and_then([](panda::WaitStatus status) -> panda::Result<void> {
        if (status == panda::WaitStatus::Timeout)
        {
            return std::unexpected {panda::makeError(panda::ErrorCode::Timeout, "Last frame did not finish in time")};
        }
        return {};
    });
}

auto run(const panda::tools::Window& window, const panda::Context& context) -> panda::Result<void>
{
    auto last = panda::CompletionPoint {};
    while (!window.shouldClose())
    {
        const auto frame = context.presentClearFrame();
        if (!frame)
        {
            return std::unexpected {frame.error()};
        }
        if (frame->completion.isValid())
        {
            last = frame->completion;
        }
        // Sleep while minimized; otherwise poll without blocking.
        const auto input = frame->status == panda::FrameResult::Status::Suspended ? panda::tools::Window::waitForInput()
                                                                                  : window.processInput();
        if (!input)
        {
            return input;
        }
    }
    return finishFrames(context, last);
}

auto runApplication() -> int
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
}

auto main() -> int
{
    try
    {
        std::ignore = panda::log::Logger::instance().addSink(std::make_unique<panda::log::StreamSink>(std::clog));
        return runApplication();
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
