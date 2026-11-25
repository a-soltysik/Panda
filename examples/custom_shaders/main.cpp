#include <array>
#include <demo/shader/ShaderResources.hpp>
#include <exception>
#include <filesystem>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_uint2.hpp>
#include <glm/trigonometric.hpp>
#include <panda/Engine.hpp>
#include <panda/diagnostics/Assert.hpp>
#include <panda/geometry/Cube.hpp>
#include <panda/rendering/Camera.hpp>
#include <panda/rendering/OffscreenRenderer.hpp>
#include <panda/rendering/RenderView.hpp>
#include <panda/rendering/ShaderProgram.hpp>
#include <panda/rendering/ShaderResources.hpp>
#include <panda/resources/Mesh.hpp>
#include <panda/resources/OffscreenTarget.hpp>
#include <panda/tools/image/Writer.hpp>
#include <ranges>
#include <span>
#include <tuple>
#include <vector>

namespace
{
constexpr auto extent = glm::uvec2 {2000, 1000};

auto uploadCube(const panda::Engine& engine) -> panda::Mesh
{
    const auto geometry = panda::createCubeGeometry();
    const auto cube =
        panda::expect(engine.getResourceManager().createMeshFromGeometry(geometry), "Create the cube mesh");
    auto batch = panda::expect(engine.getSubmissionManager().createUploadBatch(), "Create the upload batch");
    panda::expect(batch.write(cube, geometry), "Stage the cube geometry");
    std::ignore = panda::expect(batch.submit(), "Submit the cube upload");
    return cube;
}

auto createGyroidProgram(const panda::Engine& engine) -> panda::ShaderProgram
{
    const auto vertex = panda::expect(demo::shader::findShader("gyroid.vert"), "Find the embedded vertex shader");
    const auto fragment = panda::expect(demo::shader::findShader("gyroid.frag"), "Find the embedded fragment shader");
    return panda::expect(
        engine.getResourceManager().createShaderProgram({
            .vertex = {.words = vertex.words,   .stage = panda::ShaderStage::Vertex,   .label = vertex.key  },
            .fragment = {.words = fragment.words, .stage = panda::ShaderStage::Fragment, .label = fragment.key},
            .vertexInput = panda::ShaderVertexInput::StandardMesh,
            .shadow = panda::ShaderShadowMode::None
    }),
        "Create the gyroid shader program");
}

auto createCamera() -> panda::RenderCamera
{
    return panda::expect(panda::createPerspectiveCamera({
                             .position = {0.0F, 1.4F, 4.0F},
                             .target = {},
                             .up = {0.0F, 1.0F, 0.0F},
                             .verticalFieldOfViewRadians = glm::radians(43.0F),
                             .aspectRatio = static_cast<float>(extent.x) / static_cast<float>(extent.y),
                             .nearPlane = 0.1F,
                             .farPlane = 20.0F
    }),
                         "Create the camera");
}

auto createView(const panda::Mesh& cube, const panda::ShaderProgram& gyroid) -> panda::RenderView
{
    auto gyroidMaterial = panda::Material {};
    gyroidMaterial.shaderProgram = gyroid;
    auto litMaterial = panda::Material {};
    litMaterial.baseColor = {0.035F, 0.32F, 0.68F, 1.0F};
    const auto rotation = glm::rotate(glm::mat4 {1.0F}, glm::radians(24.0F), glm::vec3 {0.0F, 1.0F, 0.0F});
    return {
        .camera = createCamera(),
        .lights = {panda::DirectionalLight {}},
        .meshes = {{.mesh = cube,
                    .material = gyroidMaterial,
                    .instances = {panda::MeshInstance {
                        .modelMatrix = glm::translate(glm::mat4 {1.0F}, {-0.78F, 0.0F, 0.0F}) * rotation}},
                    .shaderResources = {}},
                   {.mesh = cube,
                    .material = litMaterial,
                    .instances = {panda::MeshInstance {
                        .modelMatrix = glm::translate(glm::mat4 {1.0F}, {0.78F, 0.0F, 0.0F}) * rotation}},
                    .shaderResources = {}}},
        .environment = {},
        .skybox = {},
        .proceduralDraws = {},
    };
}

struct alignas(16) GyroidParameters
{
    float elapsedSeconds {};
    std::array<float, 3> padding {};
};

static_assert(sizeof(GyroidParameters) == 16);

auto gyroidResources(float elapsedSeconds) -> panda::ShaderResources
{
    const auto parameters = GyroidParameters {.elapsedSeconds = elapsedSeconds};
    const auto bytes = std::as_bytes(std::span {&parameters, 1});
    return panda::ShaderResources {std::vector {panda::ShaderResourceBinding {
        .name = "params",
        .resource = panda::ShaderUniformBlock {.bytes = std::vector(std::from_range, bytes)}}}};
}

void renderFrame(const panda::OffscreenRenderer& renderer,
                 const panda::OffscreenTarget& target,
                 const panda::RenderView& view,
                 const std::filesystem::path& path)
{
    const auto frame = panda::expect(renderer.render(view, target), "Render both cubes");
    const auto image = panda::expect(renderer.readPixels(target, frame.completion), "Read the rendered image");
    panda::expect(panda::tools::image::writePng(image, path), "Write the rendered frame");
}
}

auto main() -> int
{
    try
    {
        const auto engine =
            panda::expect(panda::Engine::create({.applicationName = "Panda custom shaders"}), "Create the Engine");
        auto view = createView(uploadCube(engine), createGyroidProgram(engine));
        const auto target = panda::expect(engine.getResourceManager().createOffscreenTarget({.extent = extent}),
                                          "Create the offscreen target");
        const auto& renderer = engine.getOffscreenRenderer();
        view.meshes.front().shaderResources = gyroidResources(0.0F);
        renderFrame(renderer, target, view, "custom_shaders.png");
        view.meshes.front().shaderResources = gyroidResources(1.0F);
        renderFrame(renderer, target, view, "custom_shaders_next.png");
    }
    catch (const std::exception& error)
    {
        panda::panic(error.what());
    }
}
