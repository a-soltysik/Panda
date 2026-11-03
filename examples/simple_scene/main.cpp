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
#include <panda/Plane.hpp>
#include <panda/RenderView.hpp>
#include <panda/Transform.hpp>
#include <panda/UploadBatch.hpp>
#include <panda/tools/Camera.hpp>
#include <panda/tools/gui/Window.hpp>
#include <tuple>

namespace
{
auto makeRenderView(const panda::Mesh& cube, const panda::Mesh& ground) -> panda::RenderView
{
    const auto groundTransform = panda::expect(panda::createModelMatrix(panda::Transform {
                                                   .position = {0.0F,  -0.5F, 0.0F },
                                                   .scale = {10.0F, 1.0F,  10.0F}
    }),
                                               "Create ground transform");
    return panda::RenderView {
        .camera = {},
        .directionalLight = {.direction = {0.55F, -0.6F, 0.58F}},
        .meshes = {panda::MeshDraw {.mesh = ground, .modelMatrix = groundTransform, .tint = {0.65F, 0.67F, 0.7F, 1.0F}},
                   panda::MeshDraw {.mesh = cube}}
    };
}

void run(const panda::tools::Window& window,
         const panda::Context& context,
         const panda::Mesh& cube,
         const panda::Mesh& ground)
{
    static constexpr auto cameraPosition = glm::vec3 {3.0F, 2.5F, 4.0F};
    auto view = makeRenderView(cube, ground);
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
        const auto frame = panda::expect(context.render(view), "Render the scene");
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
        const auto groundGeometry = panda::createPlaneGeometry();
        const auto ground = panda::expect(context.createMeshFromGeometry(groundGeometry), "Create the ground mesh");
        auto batch = panda::expect(context.createUploadBatch(), "Create the upload batch");
        panda::expect(batch.write(cube, geometry), "Stage the cube geometry");
        panda::expect(batch.write(ground, groundGeometry), "Stage the ground geometry");
        const auto completion = panda::expect(batch.submit(), "Submit the cube upload");
        const auto status =
            panda::expect(context.wait(completion, std::chrono::seconds {10}), "Wait for the cube upload");
        panda::expect(status == panda::WaitStatus::Completed, "Cube upload did not finish in time");
        run(window, context, cube, ground);
        return EXIT_SUCCESS;
    }
    catch (const std::exception& error)
    {
        panda::panic(error.what());
    }
}
