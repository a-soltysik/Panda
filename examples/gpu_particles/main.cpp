#include <cstddef>
#include <cstdint>
#include <demo/particle_shader/ShaderResources.hpp>
#include <exception>
#include <glm/ext/vector_float4.hpp>
#include <glm/ext/vector_uint2.hpp>
#include <glm/trigonometric.hpp>
#include <panda/Engine.hpp>
#include <panda/diagnostics/Assert.hpp>
#include <panda/rendering/Camera.hpp>
#include <panda/rendering/OffscreenRenderer.hpp>
#include <panda/rendering/RenderView.hpp>
#include <panda/rendering/ShaderProgram.hpp>
#include <panda/rendering/ShaderResources.hpp>
#include <panda/resources/Buffer.hpp>
#include <panda/resources/OffscreenTarget.hpp>
#include <panda/tools/image/Writer.hpp>
#include <span>
#include <utility>
#include <vector>

namespace
{
constexpr auto extent = glm::uvec2 {1024, 768};
constexpr auto particleCount = std::uint32_t {10'000};
constexpr auto particleDomainExtent = 8.0F;
constexpr auto particleMaximumSpeed = 2.0F;
constexpr auto particleStorageAlignment = std::size_t {16};
constexpr auto particleRecordVectorCount = std::size_t {2};
constexpr auto particleFixtureSeed = std::uint32_t {0x50414E44};
constexpr auto xorshiftLeftFirst = std::uint32_t {13};
constexpr auto xorshiftRight = std::uint32_t {17};
constexpr auto xorshiftLeftSecond = std::uint32_t {5};
constexpr auto randomHighBitsToDiscard = std::uint32_t {8};
constexpr auto randomMantissaBits = std::uint32_t {24};
constexpr auto cameraDistance = 15.0F;
constexpr auto cameraFieldOfViewDegrees = 50.0F;
constexpr auto cameraNearPlane = 0.1F;
constexpr auto cameraFarPlane = 60.0F;
constexpr auto spriteVertexCount = std::uint32_t {6};

struct alignas(particleStorageAlignment) ParticleInstance
{
    glm::vec4 position;
    glm::vec4 velocity;
};

static_assert(sizeof(ParticleInstance) == particleRecordVectorCount * sizeof(glm::vec4));

auto nextRandomUnit(std::uint32_t& state) -> float
{
    state ^= state << xorshiftLeftFirst;
    state ^= state >> xorshiftRight;
    state ^= state << xorshiftLeftSecond;
    return static_cast<float>(state >> randomHighBitsToDiscard) * (1.0F / static_cast<float>(1U << randomMantissaBits));
}

auto makeParticleInstances() -> std::vector<ParticleInstance>
{
    auto particles = std::vector<ParticleInstance> {};
    particles.reserve(particleCount);
    auto state = particleFixtureSeed;
    for (auto index = std::uint32_t {}; index < particleCount; ++index)
    {
        const auto x = (nextRandomUnit(state) - 0.5F) * particleDomainExtent;
        const auto y = (nextRandomUnit(state) - 0.5F) * particleDomainExtent;
        const auto z = (nextRandomUnit(state) - 0.5F) * particleDomainExtent;
        const auto speed = nextRandomUnit(state) * particleMaximumSpeed;
        particles.push_back({
            .position = {x,     y,    z,    1.0F},
            .velocity = {speed, 0.0F, 0.0F, 0.0F}
        });
    }
    return particles;
}

auto createParticleResources(const panda::Engine& engine) -> panda::ShaderResources
{
    const auto particles = makeParticleInstances();
    auto particleBuffer =
        panda::expect(engine.getResourceManager().createBuffer(
                          {.size = particles.size() * sizeof(ParticleInstance),
                           .usage = panda::BufferUsage::Storage | panda::BufferUsage::TransferDestination}),
                      "Create the particle buffer");
    auto upload = panda::expect(engine.getSubmissionManager().createUploadBatch(), "Create the upload batch");
    panda::expect(upload.write(particleBuffer, 0, std::as_bytes(std::span {particles})), "Upload particles");
    std::ignore = panda::expect(upload.submit(), "Submit the particle upload");

    const auto maximumSpeedAndAspectRatio =
        glm::vec4 {particleMaximumSpeed, static_cast<float>(extent.x) / static_cast<float>(extent.y), 0.0F, 0.0F};
    const auto parameterBytes = std::as_bytes(std::span {&maximumSpeedAndAspectRatio, std::size_t {1}});
    return panda::ShaderResources {
        std::vector {
                     panda::ShaderResourceBinding {
                .name = "particleParameters",
                .resource = panda::ShaderUniformBlock {.bytes = {parameterBytes.begin(), parameterBytes.end()}}},
                     panda::ShaderResourceBinding {
                .name = "particleInstances",
                .resource = panda::ShaderBufferRange {.buffer = std::move(particleBuffer),
                                                      .size = particleCount * sizeof(ParticleInstance)}}}
    };
}

auto createParticleProgram(const panda::Engine& engine) -> panda::ShaderProgram
{
    const auto vertex = panda::expect(demo::particle_shader::findShader("particle.vert"), "Find the vertex shader");
    const auto fragment = panda::expect(demo::particle_shader::findShader("particle.frag"), "Find the fragment shader");
    return panda::expect(
        engine.getResourceManager().createShaderProgram({
            .vertex = {.words = vertex.words,   .stage = panda::ShaderStage::Vertex,   .label = vertex.key  },
            .fragment = {.words = fragment.words, .stage = panda::ShaderStage::Fragment, .label = fragment.key},
            .vertexInput = panda::ShaderVertexInput::Procedural,
            .shadow = panda::ShaderShadowMode::None
    }),
        "Create the particle shader program");
}

auto createCamera() -> panda::RenderCamera
{
    return panda::expect(panda::createPerspectiveCamera({
                             .position = {0.0F, 0.0F, cameraDistance},
                             .target = {0.0F, 0.0F, 0.0F          },
                             .up = {0.0F, 1.0F, 0.0F          },
                             .verticalFieldOfViewRadians = glm::radians(cameraFieldOfViewDegrees),
                             .aspectRatio = static_cast<float>(extent.x) / static_cast<float>(extent.y),
                             .nearPlane = cameraNearPlane,
                             .farPlane = cameraFarPlane
    }),
                         "Create the particle camera");
}

auto createView(const panda::ShaderProgram& program, panda::ShaderResources resources) -> panda::RenderView
{
    return panda::RenderView {.camera = createCamera(),
                              .lights = {},
                              .meshes = {},
                              .environment = {},
                              .skybox = {},
                              .proceduralDraws = {{.shaderProgram = program,
                                                   .shaderResources = std::move(resources),
                                                   .vertexCount = spriteVertexCount,
                                                   .instanceCount = particleCount}}};
}

void renderExample(const panda::Engine& engine, const panda::RenderView& view, const panda::OffscreenTarget& target)
{
    const auto& renderer = engine.getOffscreenRenderer();
    const auto frame = panda::expect(renderer.render(view, target), "Render particles");
    const auto image = panda::expect(renderer.readPixels(target, frame.completion), "Read the rendered image");
    panda::expect(panda::tools::image::writePng(image, "gpu_particles.png"), "Write the particle image");
}
}

auto main() -> int
{
    try
    {
        const auto engine =
            panda::expect(panda::Engine::create({.applicationName = "Panda GPU particles"}), "Create the Engine");
        const auto program = createParticleProgram(engine);
        const auto view = createView(program, createParticleResources(engine));
        const auto target = panda::expect(engine.getResourceManager().createOffscreenTarget({.extent = extent}),
                                          "Create the offscreen target");
        renderExample(engine, view, target);
    }
    catch (const std::exception& error)
    {
        panda::panic(error.what());
    }
}
