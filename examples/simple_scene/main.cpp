#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>
#include <glm/ext/vector_uint2.hpp>
#include <glm/trigonometric.hpp>
#include <iostream>
#include <memory>
#include <optional>
#include <panda/Assert.hpp>
#include <panda/Camera.hpp>
#include <panda/CompletionPoint.hpp>
#include <panda/Context.hpp>
#include <panda/Cube.hpp>
#include <panda/Logger.hpp>
#include <panda/Mesh.hpp>
#include <panda/Plane.hpp>
#include <panda/RenderView.hpp>
#include <panda/Transform.hpp>
#include <panda/UploadBatch.hpp>
#include <panda/tools/gui/Window.hpp>
#include <span>
#include <tuple>

namespace
{
struct ColoredVertex
{
    glm::vec3 position;
    glm::vec4 color;
};

struct SceneMeshes
{
    panda::Mesh cube;
    panda::Mesh ground;
    panda::Mesh triangle;
};

auto createColoredTriangleVertices() -> std::array<ColoredVertex, 3>
{
    return {
        ColoredVertex {.position = {-0.4F, 0.0F, 0.0F}, .color = {1.0F, 0.2F, 0.1F, 1.0F}},
        ColoredVertex {.position = {0.4F, 0.0F, 0.0F},  .color = {0.1F, 1.0F, 0.2F, 1.0F}},
        ColoredVertex {.position = {0.0F, 0.8F, 0.0F},  .color = {0.2F, 0.3F, 1.0F, 1.0F}}
    };
}

auto makeRenderView(const SceneMeshes& meshes) -> panda::RenderView
{
    const auto groundTransform = panda::expect(panda::createModelMatrix(panda::Transform {
                                                   .position = {0.0F,  -0.5F, 0.0F },
                                                   .scale = {10.0F, 1.0F,  10.0F}
    }),
                                               "Create ground transform");
    const auto triangleTransform = panda::expect(panda::createModelMatrix(panda::Transform {
                                                     .position = {-1.3F, -0.49F, 0.0F}
    }),
                                                 "Create triangle transform");
    return panda::RenderView {
        .camera = {},
        .directionalLight = {.direction = {0.55F, -0.6F, 0.58F}},
        .meshes = {
                   panda::MeshDraw {.mesh = meshes.ground, .modelMatrix = groundTransform, .tint = {0.65F, 0.67F, 0.7F, 1.0F}},
                   panda::MeshDraw {.mesh = meshes.cube},
                   panda::MeshDraw {.mesh = meshes.triangle,
                             .modelMatrix = triangleTransform,
                             .material = {.model = panda::MaterialModel::Unlit}}}
    };
}

auto createSceneMeshes(const panda::Context& context) -> SceneMeshes
{
    const auto geometry = panda::createCubeGeometry();
    const auto cube = panda::expect(context.createMeshFromGeometry(geometry), "Create the cube mesh");
    const auto groundGeometry = panda::createPlaneGeometry();
    const auto ground = panda::expect(context.createMeshFromGeometry(groundGeometry), "Create the ground mesh");
    const auto triangleVertices = createColoredTriangleVertices();
    const auto triangle =
        panda::expect(context.createMesh({
                          .vertexCount = static_cast<std::uint32_t>(triangleVertices.size()),
                          .vertexStride = static_cast<std::uint32_t>(sizeof(ColoredVertex)),
                          .indices = std::nullopt,
                          .attributes = {{.semantic = panda::VertexAttributeSemantic::Position,
                                          .format = panda::VertexAttributeFormat::Float3,
                                          .offset = static_cast<std::uint32_t>(offsetof(ColoredVertex, position))},
                                         {.semantic = panda::VertexAttributeSemantic::Color,
                                          .format = panda::VertexAttributeFormat::Float4,
                                          .offset = static_cast<std::uint32_t>(offsetof(ColoredVertex, color))}}
    }),
                      "Create the unlit triangle mesh");
    auto batch = panda::expect(context.createUploadBatch(), "Create the upload batch");
    panda::expect(batch.write(cube, geometry), "Stage the cube geometry");
    panda::expect(batch.write(ground, groundGeometry), "Stage the ground geometry");
    panda::expect(batch.write(triangle, std::as_bytes(std::span {triangleVertices})), "Stage the triangle vertices");
    const auto completion = panda::expect(batch.submit(), "Submit the cube upload");
    const auto status = panda::expect(context.wait(completion, std::chrono::seconds {10}), "Wait for the cube upload");
    panda::expect(status == panda::WaitStatus::Completed, "Cube upload did not finish in time");
    return {.cube = cube, .ground = ground, .triangle = triangle};
}

void run(const panda::tools::Window& window, const panda::Context& context, const SceneMeshes& meshes)
{
    static constexpr auto cameraPosition = glm::vec3 {3.0F, 2.5F, 4.0F};
    auto view = makeRenderView(meshes);
    while (!window.shouldClose())
    {
        const auto extent = panda::expect(window.getFramebufferExtent(), "Read framebuffer size");
        if (extent.width == 0 || extent.height == 0)
        {
            panda::expect(panda::tools::Window::waitForInput(), "Wait while framebuffer is suspended");
            continue;
        }
        view.camera =
            panda::expect(panda::createPerspectiveCamera({
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
        const auto meshes = createSceneMeshes(context);
        run(window, context, meshes);
        return EXIT_SUCCESS;
    }
    catch (const std::exception& error)
    {
        panda::panic(error.what());
    }
}
