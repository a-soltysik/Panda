#include <demo/shadow_shader/ShaderResources.hpp>
#include <exception>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_uint2.hpp>
#include <glm/trigonometric.hpp>
#include <panda/Engine.hpp>
#include <panda/diagnostics/Assert.hpp>
#include <panda/geometry/Cube.hpp>
#include <panda/geometry/Geometry.hpp>
#include <panda/geometry/Plane.hpp>
#include <panda/rendering/Camera.hpp>
#include <panda/rendering/RenderView.hpp>
#include <panda/rendering/ShaderProgram.hpp>
#include <panda/resources/Mesh.hpp>
#include <panda/resources/OffscreenTarget.hpp>
#include <panda/submission/UploadBatch.hpp>
#include <panda/tools/image/Writer.hpp>
#include <utility>

namespace
{
constexpr auto extent = glm::uvec2 {1024, 768};

auto createUploadedMesh(const panda::Engine& engine, const panda::Geometry& geometry) -> panda::Mesh
{
    auto mesh = panda::expect(engine.getResourceManager().createMeshFromGeometry(geometry), "Create mesh");
    auto upload = panda::expect(engine.getSubmissionManager().createUploadBatch(), "Create upload batch");
    panda::expect(upload.write(mesh, geometry), "Upload mesh");
    std::ignore = panda::expect(upload.submit(), "Submit mesh upload");
    return mesh;
}

auto createProgram(const panda::Engine& engine) -> panda::ShaderProgram
{
    const auto vertex = panda::expect(demo::shadow_shader::findShader("object.vert"), "Find object vertex shader");
    const auto fragment = panda::expect(demo::shadow_shader::findShader("object.frag"), "Find object fragment shader");
    const auto shadowVertex =
        panda::expect(demo::shadow_shader::findShader("shadow.vert"), "Find shadow vertex shader");
    const auto shadowFragment =
        panda::expect(demo::shadow_shader::findShader("shadow.frag"), "Find shadow fragment shader");
    return panda::expect(
        engine.getResourceManager().createShaderProgram({
            .vertex = {.words = vertex.words, .stage = panda::ShaderStage::Vertex, .label = vertex.key},
            .fragment = {.words = fragment.words, .stage = panda::ShaderStage::Fragment, .label = fragment.key},
            .vertexInput = panda::ShaderVertexInput::StandardMesh,
            .shadow = panda::ShaderShadowStages {.vertex = {.words = shadowVertex.words,
                                                            .stage = panda::ShaderStage::Vertex,
                                                            .label = shadowVertex.key},
                       .fragment = panda::ShaderCode {.words = shadowFragment.words,
                                                                                .stage = panda::ShaderStage::Fragment,
                                                                                .label = shadowFragment.key}}
    }),
        "Create custom shadow program");
}

auto createCamera() -> panda::RenderCamera
{
    return panda::expect(panda::createPerspectiveCamera({
                             .position = {4.0F, 4.5F, 6.0F},
                             .target = {0.0F, 0.5F, 0.0F},
                             .up = {0.0F, 1.0F, 0.0F},
                             .verticalFieldOfViewRadians = glm::radians(50.0F),
                             .aspectRatio = static_cast<float>(extent.x) / static_cast<float>(extent.y),
                             .nearPlane = 0.1F,
                             .farPlane = 60.0F
    }),
                         "Create camera");
}

auto createDraw(const panda::Mesh& mesh, glm::vec3 position, glm::vec3 scale, panda::Material material)
    -> panda::MeshDraw
{
    return {.mesh = mesh,
            .material = std::move(material),
            .instances = {panda::MeshInstance {.modelMatrix = glm::translate(glm::mat4 {1.0F}, position) *
                                                              glm::scale(glm::mat4 {1.0F}, scale)}},
            .shaderResources = {}};
}

auto createView(const panda::Engine& engine, const panda::Mesh& floor, const panda::Mesh& cube) -> panda::RenderView
{
    auto floorMaterial = panda::Material {};
    floorMaterial.baseColor = {0.32F, 0.35F, 0.4F, 1.0F};
    auto customMaterial = panda::Material {};
    customMaterial.shaderProgram = createProgram(engine);
    return panda::RenderView {
        .camera = createCamera(),
        .lights = {panda::DirectionalLight {
            .direction = {-0.82F, -0.32F, -0.5F},
            .shadow = panda::ShadowSettings {.resolution = 1024U, .depthBias = 0.002F, .normalOffset = 0.02F},
            .shadowCoverageMeters = 12.0F}},
        .meshes = {createDraw(floor, {0.0F, -0.03F, 0.0F}, {12.0F, 1.0F, 12.0F}, std::move(floorMaterial)),
                   createDraw(cube, {1.2F, 0.52F, -0.5F}, {1.1F, 1.1F, 1.1F}, std::move(customMaterial))},
        .environment = {},
        .skybox = {},
        .proceduralDraws = {}
    };
}

void renderExample(const panda::Engine& engine, const panda::RenderView& view, const panda::OffscreenTarget& target)
{
    const auto& renderer = engine.getOffscreenRenderer();
    const auto frame = panda::expect(renderer.render(view, target), "Render shadow example");
    const auto image = panda::expect(renderer.readPixels(target, frame.completion), "Read the rendered image");
    panda::expect(panda::tools::image::writePng(image, "custom_shader_shadows.png"), "Write the shadow image");
}
}

auto main() -> int
{
    try
    {
        const auto engine =
            panda::expect(panda::Engine::create({.applicationName = "Panda custom shader shadows"}), "Create Engine");
        const auto floor = createUploadedMesh(engine, panda::createPlaneGeometry());
        const auto cube = createUploadedMesh(engine, panda::createCubeGeometry());
        const auto view = createView(engine, floor, cube);
        const auto target = panda::expect(engine.getResourceManager().createOffscreenTarget({.extent = extent}),
                                          "Create offscreen target");
        renderExample(engine, view, target);
    }
    catch (const std::exception& error)
    {
        panda::panic(error.what());
    }
}
