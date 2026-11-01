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
#include <panda/Cube.hpp>
#include <panda/Error.hpp>
#include <panda/Geometry.hpp>
#include <panda/Logger.hpp>
#include <panda/Mesh.hpp>
#include <panda/UploadBatch.hpp>
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

auto waitForCompletion(const panda::Context& context, panda::CompletionPoint last) -> panda::Result<void>
{
    if (!last.isValid())
    {
        return {};
    }
    return context.wait(last, frameWaitTimeout).and_then([](panda::WaitStatus status) -> panda::Result<void> {
        if (status == panda::WaitStatus::Timeout)
        {
            return std::unexpected {panda::makeError(panda::ErrorCode::Timeout, "GPU work did not finish in time")};
        }
        return {};
    });
}

auto uploadCube(const panda::Context& context, const panda::Mesh& mesh, const panda::Geometry& geometry)
    -> panda::Result<void>
{
    auto batch = context.createUploadBatch();
    if (!batch)
    {
        return std::unexpected {batch.error()};
    }
    const auto written = batch->write(mesh, geometry);
    if (!written)
    {
        return written;
    }
    return batch->submit().and_then([&context](panda::CompletionPoint completion) {
        return waitForCompletion(context, completion);
    });
}

auto createCube(const panda::Context& context) -> panda::Result<panda::Mesh>
{
    const auto geometry = panda::createCubeGeometry();
    auto mesh = context.createMeshFromGeometry(geometry);
    if (!mesh)
    {
        return std::unexpected {mesh.error()};
    }
    if (const auto uploaded = uploadCube(context, *mesh, geometry); !uploaded)
    {
        return std::unexpected {uploaded.error()};
    }
    return mesh;
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
    return waitForCompletion(context, last);
}

auto runScene(const panda::tools::Window& window, const panda::Context& context) -> panda::Result<void>
{
    const auto cube = createCube(context);
    if (!cube)
    {
        return std::unexpected {cube.error()};
    }
    return run(window, context);
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
                    return runScene(window, context);
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
