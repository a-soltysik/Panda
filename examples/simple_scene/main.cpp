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
#include <panda/Geometry.hpp>
#include <panda/Logger.hpp>
#include <panda/Mesh.hpp>
#include <panda/Plane.hpp>
#include <panda/RenderView.hpp>
#include <panda/Texture.hpp>
#include <panda/Transform.hpp>
#include <panda/UploadBatch.hpp>
#include <panda/tools/gui/Window.hpp>
#include <span>
#include <tuple>
#include <vulkan/vulkan.hpp>

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
    panda::Texture baseColorMap;
    panda::Texture metallicRoughnessMap;
    panda::Texture emissiveMap;
    panda::Texture normalMap;
    panda::Texture occlusionMap;
};

struct SceneGeometry
{
    panda::Geometry cube;
    panda::Geometry ground;
    std::array<ColoredVertex, 3> triangle {};
};

constexpr auto baseColorPixels = std::array {std::byte {0xff},
                                             std::byte {0x30},
                                             std::byte {0x30},
                                             std::byte {0xff},
                                             std::byte {0xff},
                                             std::byte {0xff},
                                             std::byte {0xff},
                                             std::byte {0xff},
                                             std::byte {0x30},
                                             std::byte {0x50},
                                             std::byte {0xff},
                                             std::byte {0xff},
                                             std::byte {0xff},
                                             std::byte {0xff},
                                             std::byte {0xff},
                                             std::byte {0xff}};
constexpr auto metallicRoughnessPixels =
    std::array {std::byte {0xff}, std::byte {0xff}, std::byte {0xff}, std::byte {0xff}};
constexpr auto emissivePixels = std::array {std::byte {0xff},
                                            std::byte {0xa0},
                                            std::byte {0x20},
                                            std::byte {0xff},
                                            std::byte {0x00},
                                            std::byte {0x00},
                                            std::byte {0x00},
                                            std::byte {0xff},
                                            std::byte {0x00},
                                            std::byte {0x00},
                                            std::byte {0x00},
                                            std::byte {0xff},
                                            std::byte {0xff},
                                            std::byte {0xa0},
                                            std::byte {0x20},
                                            std::byte {0xff}};
constexpr auto normalStripeRow = std::array {std::byte {0xd9},
                                             std::byte {0x80},
                                             std::byte {0xe6},
                                             std::byte {0xff},
                                             std::byte {0x27},
                                             std::byte {0x80},
                                             std::byte {0xe6},
                                             std::byte {0xff},
                                             std::byte {0xd9},
                                             std::byte {0x80},
                                             std::byte {0xe6},
                                             std::byte {0xff},
                                             std::byte {0x27},
                                             std::byte {0x80},
                                             std::byte {0xe6},
                                             std::byte {0xff}};
constexpr auto normalPixels = std::array {normalStripeRow, normalStripeRow, normalStripeRow, normalStripeRow};
constexpr auto whiteOcclusionPixel =
    std::array {std::byte {0xff}, std::byte {0xff}, std::byte {0xff}, std::byte {0xff}};
constexpr auto darkOcclusionPixel = std::array {std::byte {0x30}, std::byte {0xff}, std::byte {0xff}, std::byte {0xff}};
constexpr auto occlusionPixels = std::array {
    std::array {whiteOcclusionPixel, whiteOcclusionPixel, whiteOcclusionPixel, whiteOcclusionPixel},
    std::array {whiteOcclusionPixel, darkOcclusionPixel,  darkOcclusionPixel,  whiteOcclusionPixel},
    std::array {whiteOcclusionPixel, darkOcclusionPixel,  darkOcclusionPixel,  whiteOcclusionPixel},
    std::array {whiteOcclusionPixel, whiteOcclusionPixel, whiteOcclusionPixel, whiteOcclusionPixel}
};

auto createColoredTriangleVertices() -> std::array<ColoredVertex, 3>
{
    return {
        ColoredVertex {.position = {-0.4F, 0.0F, 0.0F}, .color = {1.0F, 0.2F, 0.1F, 1.0F}},
        ColoredVertex {.position = {0.4F, 0.0F, 0.0F},  .color = {0.1F, 1.0F, 0.2F, 1.0F}},
        ColoredVertex {.position = {0.0F, 0.8F, 0.0F},  .color = {0.2F, 0.3F, 1.0F, 1.0F}}
    };
}

auto createColoredTriangleMesh(const panda::Context& context, const std::array<ColoredVertex, 3>& vertices)
    -> panda::Mesh
{
    return panda::expect(context.getResourceManager().createMesh({
                             .vertexCount = static_cast<std::uint32_t>(vertices.size()),
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
}

auto createBaseColorMap(const panda::Context& context) -> panda::Texture
{
    return panda::expect(context.getResourceManager().createTexture({
                             .extent = {2, 2},
                             .format = vk::Format::eR8G8B8A8Srgb,
                             .usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled
    }),
                         "Create the cube base-color map");
}

auto createMetallicRoughnessMap(const panda::Context& context) -> panda::Texture
{
    return panda::expect(context.getResourceManager().createTexture({
                             .extent = {1, 1},
                             .format = vk::Format::eR8G8B8A8Unorm,
                             .usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled
    }),
                         "Create the cube metallic-roughness map");
}

auto createEmissiveMap(const panda::Context& context) -> panda::Texture
{
    return panda::expect(context.getResourceManager().createTexture({
                             .extent = {2, 2},
                             .format = vk::Format::eR8G8B8A8Srgb,
                             .usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled
    }),
                         "Create the cube emissive map");
}

auto createNormalMap(const panda::Context& context) -> panda::Texture
{
    return panda::expect(context.getResourceManager().createTexture({
                             .extent = {4, 4},
                             .format = vk::Format::eR8G8B8A8Unorm,
                             .usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled
    }),
                         "Create the cube normal map");
}

auto createOcclusionMap(const panda::Context& context) -> panda::Texture
{
    return panda::expect(context.getResourceManager().createTexture({
                             .extent = {4, 4},
                             .format = vk::Format::eR8G8B8A8Unorm,
                             .usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled
    }),
                         "Create the cube occlusion map");
}

void uploadSceneMeshes(const panda::Context& context, const SceneMeshes& meshes, const SceneGeometry& geometry)
{
    auto batch = panda::expect(context.getSubmissionManager().createUploadBatch(), "Create the upload batch");
    panda::expect(batch.write(meshes.cube, geometry.cube), "Stage the cube geometry");
    panda::expect(batch.write(meshes.ground, geometry.ground), "Stage the ground geometry");
    panda::expect(batch.write(meshes.triangle, std::as_bytes(std::span {geometry.triangle})),
                  "Stage the triangle vertices");
    panda::expect(batch.write(meshes.baseColorMap, baseColorPixels), "Stage the cube base-color map");
    panda::expect(batch.write(meshes.metallicRoughnessMap, metallicRoughnessPixels),
                  "Stage the cube metallic-roughness map");
    panda::expect(batch.write(meshes.emissiveMap, emissivePixels), "Stage the cube emissive map");
    panda::expect(batch.write(meshes.normalMap, std::as_bytes(std::span {normalPixels})), "Stage the normal map");
    panda::expect(batch.write(meshes.occlusionMap, std::as_bytes(std::span {occlusionPixels})),
                  "Stage the occlusion map");
    const auto completion = panda::expect(batch.submit(), "Submit the scene upload");
    const auto status = panda::expect(context.getSubmissionManager().wait(completion, std::chrono::seconds {10}),
                                      "Wait for the scene upload");
    panda::expect(status == panda::WaitStatus::Completed, "Scene upload did not finish in time");
}

auto makeNormalMapDemoDraw(const SceneMeshes& meshes) -> panda::MeshDraw
{
    const auto transform = panda::expect(panda::createModelMatrix(panda::Transform {
                                             .position = {2.0F, 0.0F, 0.0F}
    }),
                                         "Create normal-mapped cube transform");
    auto draw = panda::MeshDraw {.mesh = meshes.cube, .modelMatrix = transform};
    draw.material.baseColor = {0.55F, 0.55F, 0.55F, 1.0F};
    draw.material.normalTexture = meshes.normalMap;
    draw.material.occlusionTexture = meshes.occlusionMap;
    return draw;
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
    auto triangleDraw = panda::MeshDraw {.mesh = meshes.triangle, .modelMatrix = triangleTransform};
    triangleDraw.material.model = panda::MaterialModel::Unlit;
    auto cubeDraw = panda::MeshDraw {};
    cubeDraw.mesh = meshes.cube;
    cubeDraw.material.baseColorTexture = meshes.baseColorMap;
    cubeDraw.material.metallicRoughnessTexture = meshes.metallicRoughnessMap;
    cubeDraw.material.emissiveTexture = meshes.emissiveMap;
    cubeDraw.material.emissive = {1.0F, 0.8F, 0.3F};
    return panda::RenderView {
        .camera = {},
        .lights = {panda::DirectionalLight {.direction = {-0.55F, -0.6F, -0.58F}, .shadow = panda::ShadowSettings {}}},
        .meshes = {
                   panda::MeshDraw {.mesh = meshes.ground, .modelMatrix = groundTransform, .tint = {0.65F, 0.67F, 0.7F, 1.0F}},
                   cubeDraw, makeNormalMapDemoDraw(meshes),
                   triangleDraw}
    };
}

auto createSceneMeshes(const panda::Context& context) -> SceneMeshes
{
    const auto geometry = SceneGeometry {.cube = panda::createCubeGeometry(),
                                         .ground = panda::createPlaneGeometry(),
                                         .triangle = createColoredTriangleVertices()};
    const auto cube =
        panda::expect(context.getResourceManager().createMeshFromGeometry(geometry.cube), "Create the cube mesh");
    const auto ground =
        panda::expect(context.getResourceManager().createMeshFromGeometry(geometry.ground), "Create the ground mesh");
    auto meshes = SceneMeshes {.cube = cube,
                               .ground = ground,
                               .triangle = createColoredTriangleMesh(context, geometry.triangle),
                               .baseColorMap = createBaseColorMap(context),
                               .metallicRoughnessMap = createMetallicRoughnessMap(context),
                               .emissiveMap = createEmissiveMap(context),
                               .normalMap = createNormalMap(context),
                               .occlusionMap = createOcclusionMap(context)};
    uploadSceneMeshes(context, meshes, geometry);
    return meshes;
}

void run(const panda::tools::Window& window, const panda::Context& context, const SceneMeshes& meshes)
{
    static constexpr auto cameraPosition = glm::vec3 {3.0F, 2.5F, 4.0F};
    const auto renderer = panda::expect(context.getWindowRenderer(), "Window context has no window renderer");
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
        const auto frame = panda::expect(renderer.get().render(view), "Render the scene");
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
