#include <chrono>
#include <cstdlib>
#include <exception>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_uint2.hpp>
#include <glm/trigonometric.hpp>
#include <iostream>
#include <memory>
#include <panda/Assert.hpp>
#include <panda/CompletionPoint.hpp>
#include <panda/Context.hpp>
#include <panda/Cube.hpp>
#include <panda/Logger.hpp>
#include <panda/Mesh.hpp>
#include <panda/RenderView.hpp>
#include <panda/UploadBatch.hpp>
#include <panda/tools/Camera.hpp>
#include <panda/tools/gui/Window.hpp>
#include <tuple>

namespace
{
void run(const panda::tools::Window& window, const panda::Context& context, const panda::Mesh& cube)
{
    static constexpr auto cameraPosition = glm::vec3 {3.0F, 2.5F, 4.0F};
    auto view = panda::RenderView {};
    view.meshes.push_back({.mesh = cube});
    while (!window.shouldClose())
    {
        const auto extent = panda::expect(window.getFramebufferExtent(), "Read framebuffer size");
        if (extent.width == 0 || extent.height == 0)
        {
            panda::expect(panda::tools::Window::waitForInput(), "Wait while framebuffer is suspended");
            continue;
        }
        view.camera =
            panda::expect(panda::tools::createPerspectiveCamera({
                              .position = cameraPosition,
                              .target = {},
                              .up = {0, 1, 0},
                              .verticalFieldOfViewRadians = glm::radians(50.0F),
                              .aspectRatio = static_cast<float>(extent.width) / static_cast<float>(extent.height),
                              .nearPlane = 0.1F,
                              .farPlane = 100.0F
        }),
                          "Create the camera");
        const auto frame = panda::expect(context.render(view), "Render the cube");
        // A resize can suspend rendering after the extent check above.
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
        run(window, context, cube);
        return EXIT_SUCCESS;
    }
    catch (const std::exception& error)
    {
        panda::panic(error.what());
    }
}
