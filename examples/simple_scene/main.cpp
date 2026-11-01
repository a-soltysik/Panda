#include <chrono>
#include <cstdlib>
#include <exception>
#include <glm/ext/vector_uint2.hpp>
#include <iostream>
#include <memory>
#include <panda/Assert.hpp>
#include <panda/CompletionPoint.hpp>
#include <panda/Context.hpp>
#include <panda/Cube.hpp>
#include <panda/Logger.hpp>
#include <panda/UploadBatch.hpp>
#include <panda/tools/gui/Window.hpp>
#include <tuple>

namespace
{
void run(const panda::tools::Window& window, const panda::Context& context)
{
    while (!window.shouldClose())
    {
        const auto frame = panda::expect(context.presentClearFrame(), "Present a clear frame");
        // Sleep while minimized; otherwise poll without blocking.
        panda::expect(frame.status == panda::FrameResult::Status::Suspended ? panda::tools::Window::waitForInput()
                                                                            : window.processInput(),
                      "Process window input");
    }
}
}

auto main() -> int
{
    try
    {
        std::ignore = panda::log::Logger::instance().addSink(std::make_unique<panda::log::StreamSink>(std::clog));
        const auto window = panda::expect(panda::tools::Window::create(glm::uvec2 {1280, 720}, "Panda - Simple scene"),
                                          "Create the window");
        const auto context = panda::expect(
            panda::Context::createWithSurface(window,
                                              {.applicationName = "Panda simple scene", .enableValidation = true}),
            "Create the Vulkan context");
        const auto geometry = panda::createCubeGeometry();
        const auto cube = panda::expect(context.createMeshFromGeometry(geometry), "Create the cube mesh");
        auto batch = panda::expect(context.createUploadBatch(), "Create the upload batch");
        panda::expect(batch.write(cube, geometry), "Stage the cube geometry");
        const auto completion = panda::expect(batch.submit(), "Submit the cube upload");
        const auto status =
            panda::expect(context.wait(completion, std::chrono::seconds {10}), "Wait for the cube upload");
        panda::expect(status == panda::WaitStatus::Completed, "Cube upload did not finish in time");
        run(window, context);
        return EXIT_SUCCESS;
    }
    catch (const std::exception& error)
    {
        panda::panic(error.what());
    }
}
