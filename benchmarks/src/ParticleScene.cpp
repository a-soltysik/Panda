#include "ParticleScene.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <demo/particle_shader/ShaderResources.hpp>
#include <expected>
#include <glm/ext/vector_float4.hpp>
#include <glm/ext/vector_uint2.hpp>
#include <glm/trigonometric.hpp>
#include <panda/Engine.hpp>
#include <panda/base/Error.hpp>
#include <panda/rendering/Camera.hpp>
#include <panda/rendering/RenderView.hpp>
#include <panda/rendering/ShaderProgram.hpp>
#include <panda/rendering/ShaderResources.hpp>
#include <panda/resources/Buffer.hpp>
#include <panda/submission/CompletionPoint.hpp>
#include <panda/submission/UploadBatch.hpp>
#include <span>
#include <utility>
#include <vector>

#include "SmokeRunBackend.hpp"

namespace panda_bench
{
namespace
{
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

auto particleFailureFromPanda(const panda::Error& error) -> Failure
{
    return {.outcome = error.code == panda::ErrorCode::Unsupported ? "unsupported" : "failed",
            .message = error.message};
}

auto nextRandomUnit(std::uint32_t& state) -> float
{
    state ^= state << xorshiftLeftFirst;
    state ^= state >> xorshiftRight;
    state ^= state << xorshiftLeftSecond;
    return static_cast<float>(state >> randomHighBitsToDiscard) * (1.0F / static_cast<float>(1U << randomMantissaBits));
}

auto makeParticleInstances(std::uint32_t count) -> std::vector<ParticleInstance>
{
    auto particles = std::vector<ParticleInstance> {};
    particles.reserve(count);
    auto state = particleFixtureSeed;
    for (auto index = std::uint32_t {}; index < count; ++index)
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

auto waitForUpload(const panda::Engine& engine, panda::CompletionPoint completion) -> std::expected<void, Failure>
{
    const auto waited = engine.getSubmissionManager().wait(completion, std::chrono::seconds {30});
    if (!waited)
    {
        return std::unexpected {particleFailureFromPanda(waited.error())};
    }
    if (*waited == panda::WaitStatus::Timeout)
    {
        return std::unexpected {Failure {.message = "Particle buffer upload did not complete within 30 seconds"}};
    }
    return {};
}

auto uploadBuffer(const panda::Engine& engine, panda::Buffer& buffer, std::span<const std::byte> bytes)
    -> std::expected<void, Failure>
{
    auto upload = engine.getSubmissionManager().createUploadBatch();
    if (!upload)
    {
        return std::unexpected {particleFailureFromPanda(upload.error())};
    }
    if (const auto written = upload->write(buffer, 0, bytes); !written)
    {
        return std::unexpected {particleFailureFromPanda(written.error())};
    }
    const auto completion = upload->submit();
    if (!completion)
    {
        return std::unexpected {particleFailureFromPanda(completion.error())};
    }
    return waitForUpload(engine, *completion);
}

auto createParticleBuffer(const panda::Engine& engine, std::uint32_t count) -> std::expected<panda::Buffer, Failure>
{
    const auto particles = makeParticleInstances(count);
    auto buffer = engine.getResourceManager().createBuffer(
        {.size = particles.size() * sizeof(ParticleInstance),
         .usage = panda::BufferUsage::Storage | panda::BufferUsage::TransferDestination});
    if (!buffer)
    {
        return std::unexpected {particleFailureFromPanda(buffer.error())};
    }
    if (const auto uploaded = uploadBuffer(engine, *buffer, std::as_bytes(std::span {particles})); !uploaded)
    {
        return std::unexpected {uploaded.error()};
    }
    return std::move(*buffer);
}

auto createParticleResources(const panda::Engine& engine, std::uint32_t count, glm::uvec2 extent)
    -> std::expected<panda::ShaderResources, Failure>
{
    auto particleBuffer = createParticleBuffer(engine, count);
    if (!particleBuffer)
    {
        return std::unexpected {particleBuffer.error()};
    }
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
                .resource = panda::ShaderBufferRange {.buffer = std::move(*particleBuffer),
                                                      .size = count * sizeof(ParticleInstance)}}}
    };
}

auto createParticleProgram(const panda::Engine& engine) -> std::expected<panda::ShaderProgram, Failure>
{
    const auto vertex = demo::particle_shader::findShader("particle.vert");
    const auto fragment = demo::particle_shader::findShader("particle.frag");
    if (!vertex || !fragment)
    {
        return std::unexpected {Failure {.message = "Could not load the GPU particle shaders"}};
    }
    auto program = engine.getResourceManager().createShaderProgram({
        .vertex = {.words = vertex->words,   .stage = panda::ShaderStage::Vertex,   .label = vertex->key  },
        .fragment = {.words = fragment->words, .stage = panda::ShaderStage::Fragment, .label = fragment->key},
        .vertexInput = panda::ShaderVertexInput::Procedural,
        .shadow = panda::ShaderShadowMode::None
    });
    if (!program)
    {
        return std::unexpected {particleFailureFromPanda(program.error())};
    }
    return std::move(*program);
}

auto createParticleCamera(glm::uvec2 extent) -> std::expected<panda::RenderCamera, Failure>
{
    const auto camera = panda::createPerspectiveCamera({
        .position = {0.0F, 0.0F, cameraDistance},
        .target = {0.0F, 0.0F, 0.0F          },
        .up = {0.0F, 1.0F, 0.0F          },
        .verticalFieldOfViewRadians = glm::radians(cameraFieldOfViewDegrees),
        .aspectRatio = static_cast<float>(extent.x) / static_cast<float>(extent.y),
        .nearPlane = cameraNearPlane,
        .farPlane = cameraFarPlane
    });
    if (!camera)
    {
        return std::unexpected {particleFailureFromPanda(camera.error())};
    }
    return *camera;
}
}

auto createParticleScene(const panda::Engine& engine, glm::uvec2 extent, std::uint32_t particleCount)
    -> std::expected<panda::RenderView, Failure>
{
    auto camera = createParticleCamera(extent);
    if (!camera)
    {
        return std::unexpected {camera.error()};
    }
    auto program = createParticleProgram(engine);
    if (!program)
    {
        return std::unexpected {program.error()};
    }
    auto resources = createParticleResources(engine, particleCount, extent);
    if (!resources)
    {
        return std::unexpected {resources.error()};
    }
    return panda::RenderView {.camera = *camera,
                              .lights = {},
                              .meshes = {},
                              .environment = {},
                              .skybox = {},
                              .proceduralDraws = {{.shaderProgram = std::move(*program),
                                                   .shaderResources = std::move(*resources),
                                                   .vertexCount = spriteVertexCount,
                                                   .instanceCount = particleCount}}};
}
}
