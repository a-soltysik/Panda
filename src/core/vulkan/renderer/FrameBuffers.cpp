// clang-format off
#include <panda/base/Error.hpp>
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include "renderer/FrameBuffers.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/geometric.hpp>
#include <iterator>
#include <limits>
#include <memory>
#include <panda/rendering/RenderView.hpp>
#include <panda/resources/Buffer.hpp>
#include <span>
#include <utility>
#include <variant>
#include <vector>

#include "Overloaded.hpp"
#include "renderer/ShaderData.hpp"
#include "renderer/ShadowAtlas.hpp"
#include "resources/BufferResource.hpp"
#include "resources/memory/MemoryPool.hpp"

namespace panda::core::vulkan::renderer
{
namespace
{
auto shadowViewStride(std::uint64_t alignment) -> std::uint64_t
{
    const auto matrixSize = sizeof(glm::mat4);
    const auto effectiveAlignment = std::max(alignment, std::uint64_t {1});
    return matrixSize + ((effectiveAlignment - (matrixSize % effectiveAlignment)) % effectiveAlignment);
}

constexpr auto ambientIntensity = 0.06F;

auto makeFrameUniform(const RenderView& view, std::uint32_t lightCount) -> FrameUniform
{
    return FrameUniform {
        .viewProjection = view.camera.viewProjection,
        .cameraPosition = glm::vec4 {view.camera.position, 1.0F},
        .ambient = glm::vec4 {1.0F, 1.0F, 1.0F, ambientIntensity},
        .lightInfo = glm::uvec4 {lightCount, 0U, 0U, 0U}
    };
}

auto makeDirectionalLightRecord(const DirectionalLight& light,
                                std::uint32_t shadowViewIndex,
                                std::uint32_t shadowViewCount) -> LightRecord
{
    return LightRecord {
        .positionRange = {},
        .directionOuterConeCos = glm::vec4 {glm::normalize(light.direction), 0.0F},
        .colorIntensity = glm::vec4 {light.color, light.intensity},
        .innerConeCos = 0.0F,
        .type = static_cast<std::uint32_t>(ShaderLightType::Directional),
        .shadowViewIndex = shadowViewIndex,
        .shadowViewCount = shadowViewCount
    };
}

auto makePointLightRecord(const PointLight& light, std::uint32_t shadowViewIndex, std::uint32_t shadowViewCount)
    -> LightRecord
{
    return LightRecord {
        .positionRange = glm::vec4 {light.position, light.range},
        .directionOuterConeCos = {},
        .colorIntensity = glm::vec4 {light.color, light.intensity},
        .innerConeCos = 0.0F,
        .type = static_cast<std::uint32_t>(ShaderLightType::Point),
        .shadowViewIndex = shadowViewIndex,
        .shadowViewCount = shadowViewCount
    };
}

auto makeSpotLightRecord(const SpotLight& light, std::uint32_t shadowViewIndex, std::uint32_t shadowViewCount)
    -> LightRecord
{
    return LightRecord {
        .positionRange = glm::vec4 {light.position,                  light.range                          },
        .directionOuterConeCos = glm::vec4 {glm::normalize(light.direction), std::cos(light.outerConeAngleRadians)},
        .colorIntensity = glm::vec4 {light.color,                     light.intensity                      },
        .innerConeCos = std::cos(light.innerConeAngleRadians),
        .type = static_cast<std::uint32_t>(ShaderLightType::Spot),
        .shadowViewIndex = shadowViewIndex,
        .shadowViewCount = shadowViewCount
    };
}

auto makeLightRecord(const Light& light, std::uint32_t shadowViewIndex, std::uint32_t shadowViewCount) -> LightRecord
{
    return std::visit(
        ::panda::detail::Overloaded {[=](const DirectionalLight& value) {
                                         return makeDirectionalLightRecord(value, shadowViewIndex, shadowViewCount);
                                     },
                                     [=](const PointLight& value) {
                                         return makePointLightRecord(value, shadowViewIndex, shadowViewCount);
                                     },
                                     [=](const SpotLight& value) {
                                         return makeSpotLightRecord(value, shadowViewIndex, shadowViewCount);
                                     }},
        light);
}

auto makeMaterialRecord(const Material& material) -> MaterialRecord
{
    return MaterialRecord {
        .metallic = std::clamp(material.metallic, 0.0F, 1.0F),
        .roughness = std::clamp(material.roughness, 0.0F, 1.0F),
        .isLit = material.model == MaterialModel::Lit ? 1U : 0U,
        .renderFace = static_cast<std::uint32_t>(material.renderFace),
        .alphaCutoff = material.alphaCutoff,
        .hasBaseColorTexture = material.baseColorTexture.isValid() ? 1U : 0U,
        .hasMetallicRoughnessTexture = material.metallicRoughnessTexture.isValid() ? 1U : 0U,
        .hasEmissiveTexture = material.emissiveTexture.isValid() ? 1U : 0U,
        .emissiveAndNormalScale = glm::vec4 {material.emissive, material.normalScale},
        .hasOcclusionTexture = material.occlusionTexture.isValid() ? 1U : 0U,
        .occlusionStrength = std::clamp(material.occlusionStrength, 0.0F, 1.0F)
    };
}

auto makeShadowRecord(const ShadowAtlasView& view) -> ShadowRecord
{
    const auto resolution = static_cast<float>(view.tile.resolution);
    return {
        .viewProjection = view.viewProjection,
        .atlasScaleBias = glm::vec4 {resolution / static_cast<float>(shadowAtlasExtent),
                                     resolution / static_cast<float>(shadowAtlasExtent),
                                     static_cast<float>(view.tile.x) / static_cast<float>(shadowAtlasExtent),
                                     static_cast<float>(view.tile.y) / static_cast<float>(shadowAtlasExtent)                                         },
        .shadowParameters = glm::vec4 {view.depthBias,                                     view.normalOffset, static_cast<float>(view.tile.layer), 0.0F}
    };
}

auto makeShadowViewMatrices(const ShadowAtlasPlan& plan, std::uint64_t stride) -> std::vector<std::byte>
{
    auto matrices = std::vector<std::byte>(stride * plan.views.size());
    for (auto index = std::size_t {}; index < plan.views.size(); ++index)
    {
        const auto matrixBytes = std::as_bytes(std::span {&plan.views.at(index).viewProjection, std::size_t {1}});
        auto destination = std::span {matrices}.subspan(index * stride, matrixBytes.size());
        std::ranges::copy(matrixBytes, destination.begin());
    }
    return matrices;
}
}

FrameBuffers::FrameBuffers(const vk::raii::Device& device,
                           resources::MemoryPool& pool,
                           const vk::PhysicalDeviceLimits& limits,
                           std::size_t frameCount)
    : _device {device},
      _pool {pool},
      _maxStorageBufferRange {limits.maxStorageBufferRange},
      _shadowViewStride {shadowViewStride(limits.minUniformBufferOffsetAlignment)},
      _frames(frameCount)
{
}

auto FrameBuffers::descriptors(std::size_t index) const -> FrameBufferDescriptors
{
    const auto& frame = _frames.at(index);
    return {
        .uniform = {.buffer = frame.uniformBuffer->getNative(),    .range = sizeof(FrameUniform)        },
        .material = {.buffer = frame.materialBuffer->getNative(),   .range = frame.materialBufferCapacity},
        .lights = {.buffer = frame.lightBuffer->getNative(),      .range = frame.lightBufferCapacity   },
        .shadows = {.buffer = frame.shadowBuffer->getNative(),     .range = frame.shadowBufferCapacity  },
        .shadowView = {.buffer = frame.shadowViewBuffer->getNative(), .range = sizeof(glm::mat4)           }
    };
}

auto FrameBuffers::shadowViewOffset(std::uint32_t index) const noexcept -> std::uint32_t
{
    return static_cast<std::uint32_t>(index * _shadowViewStride);
}

auto FrameBuffers::initializeFrameBuffers() -> std::expected<void, Error>
{
    for (auto& frame : _frames)
    {
        if (const auto initialized = initializeFrameBuffersForSlot(frame); !initialized)
        {
            return initialized;
        }
    }
    return {};
}

auto FrameBuffers::initializeFrameBuffersForSlot(FrameResources& frame) -> Result<void>
{
    return createUploadBuffer(sizeof(FrameUniform), vk::BufferUsageFlagBits::eUniformBuffer)
        .transform([&](auto buffer) {
            frame.uniformBuffer = std::move(buffer);
        })
        .and_then([&] {
            return ensureMaterialBuffer(frame, 0);
        })
        .and_then([&] {
            return ensureLightBuffer(frame, 0);
        })
        .and_then([&] {
            return ensureShadowRecordBuffer(frame, 1);
        })
        .and_then([&] {
            return ensureShadowViewBuffer(frame, 1);
        });
}

auto FrameBuffers::createUploadBuffer(vk::DeviceSize size, vk::BufferUsageFlagBits usage)
    -> std::expected<std::shared_ptr<::panda::detail::BufferResource>, Error>
{
    return ::panda::detail::BufferResource::createNative(
        _device,
        _pool,
        ::panda::detail::BufferResourceCreateInfo {.size = size, .usage = usage, .memory = BufferMemory::Upload});
}

auto FrameBuffers::writeFrameUniform(FrameResources& frame, const RenderView& view, std::uint32_t lightCount)
    -> std::expected<void, Error>
{
    const auto uniform = makeFrameUniform(view, lightCount);
    return frame.uniformBuffer->writeMapped(0, std::as_bytes(std::span {&uniform, std::size_t {1}}));
}

auto FrameBuffers::ensureBufferCapacity(std::shared_ptr<::panda::detail::BufferResource>& buffer,
                                        std::uint64_t& capacity,
                                        std::uint64_t requiredCapacity,
                                        vk::BufferUsageFlagBits usage) -> std::expected<void, Error>
{
    if (capacity >= requiredCapacity)
    {
        return {};
    }
    return createUploadBuffer(requiredCapacity, usage).transform([&](auto replacement) {
        buffer = std::move(replacement);
        capacity = requiredCapacity;
    });
}

auto FrameBuffers::ensureLightBuffer(FrameResources& frame, std::size_t lightCount) -> std::expected<void, Error>
{
    if (lightCount > _maxStorageBufferRange / sizeof(LightRecord))
    {
        return std::unexpected {
            makeError(ErrorCode::ResourceExhausted, "Render view light count exceeds storage-buffer limits")};
    }
    const auto capacity = sizeof(LightRecord) * std::max<std::size_t>(lightCount, 1U);
    return ensureBufferCapacity(frame.lightBuffer,
                                frame.lightBufferCapacity,
                                capacity,
                                vk::BufferUsageFlagBits::eStorageBuffer);
}

auto FrameBuffers::ensureShadowBuffers(FrameResources& frame, std::size_t shadowViewCount) -> std::expected<void, Error>
{
    const auto bufferCount = std::max<std::size_t>(shadowViewCount, 1U);
    if (bufferCount > _maxStorageBufferRange / sizeof(ShadowRecord) ||
        (shadowViewCount > 0 && (shadowViewCount - 1U) > std::numeric_limits<std::uint32_t>::max() / _shadowViewStride))
    {
        return std::unexpected {makeError(ErrorCode::ResourceExhausted, "Shadow views exceed Vulkan buffer limits")};
    }
    return ensureShadowRecordBuffer(frame, bufferCount).and_then([&] {
        return ensureShadowViewBuffer(frame, bufferCount);
    });
}

auto FrameBuffers::ensureShadowRecordBuffer(FrameResources& frame, std::size_t bufferCount)
    -> std::expected<void, Error>
{
    const auto capacity = sizeof(ShadowRecord) * bufferCount;
    return ensureBufferCapacity(frame.shadowBuffer,
                                frame.shadowBufferCapacity,
                                capacity,
                                vk::BufferUsageFlagBits::eStorageBuffer);
}

auto FrameBuffers::ensureShadowViewBuffer(FrameResources& frame, std::size_t bufferCount) -> std::expected<void, Error>
{
    const auto capacity = _shadowViewStride * bufferCount;
    return ensureBufferCapacity(frame.shadowViewBuffer,
                                frame.shadowViewBufferCapacity,
                                capacity,
                                vk::BufferUsageFlagBits::eUniformBuffer);
}

auto FrameBuffers::ensureMaterialBuffer(FrameResources& frame, std::size_t drawCount) -> std::expected<void, Error>
{
    if (drawCount > _maxStorageBufferRange / sizeof(MaterialRecord))
    {
        return std::unexpected {
            makeError(ErrorCode::ResourceExhausted, "Mesh draw count exceeds material storage buffer limits")};
    }
    const auto capacity = sizeof(MaterialRecord) * std::max<std::size_t>(drawCount, 1U);
    return ensureBufferCapacity(frame.materialBuffer,
                                frame.materialBufferCapacity,
                                capacity,
                                vk::BufferUsageFlagBits::eStorageBuffer);
}

auto FrameBuffers::writeMaterialRecords(FrameResources& frame, const RenderView& view) -> std::expected<void, Error>
{
    _materialRecords.clear();
    _materialRecords.reserve(view.meshes.size());
    std::ranges::transform(view.meshes, std::back_inserter(_materialRecords), [](const auto& draw) {
        return makeMaterialRecord(draw.material);
    });
    if (_materialRecords.empty())
    {
        return {};
    }
    return frame.materialBuffer->writeMapped(0, std::as_bytes(std::span {_materialRecords}));
}

auto FrameBuffers::writeLightRecords(FrameResources& frame, const RenderView& view, const ShadowAtlasPlan& plan)
    -> std::expected<void, Error>
{
    if (view.lights.empty())
    {
        return {};
    }
    _lightRecords.clear();
    _lightRecords.reserve(view.lights.size());
    for (auto index = std::size_t {}; index < view.lights.size(); ++index)
    {
        _lightRecords.push_back(
            makeLightRecord(view.lights.at(index), plan.firstViewByLight.at(index), plan.viewCountByLight.at(index)));
    }
    return frame.lightBuffer->writeMapped(0, std::as_bytes(std::span {_lightRecords}));
}

auto FrameBuffers::writeShadowRecords(FrameResources& frame, const ShadowAtlasPlan& plan) const
    -> std::expected<void, Error>
{
    if (plan.views.empty())
    {
        return {};
    }
    auto records = std::vector<ShadowRecord> {};
    records.reserve(plan.views.size());
    std::ranges::transform(plan.views, std::back_inserter(records), makeShadowRecord);
    const auto matrices = makeShadowViewMatrices(plan, _shadowViewStride);
    if (const auto written = frame.shadowBuffer->writeMapped(0, std::as_bytes(std::span {records})); !written)
    {
        return written;
    }
    return frame.shadowViewBuffer->writeMapped(0, matrices);
}

auto FrameBuffers::update(std::size_t index, const RenderView& view, const ShadowAtlasPlan& plan)
    -> std::expected<void, Error>
{
    auto& frame = _frames.at(index);
    const auto lightCount = static_cast<std::uint32_t>(view.lights.size());
    return ensureLightBuffer(frame, lightCount)
        .and_then([&] {
            return ensureShadowBuffers(frame, plan.views.size());
        })
        .and_then([&] {
            return writeFrameUniform(frame, view, lightCount);
        })
        .and_then([&] {
            return ensureMaterialBuffer(frame, view.meshes.size());
        })
        .and_then([&] {
            return writeMaterialRecords(frame, view);
        })
        .and_then([&] {
            return writeLightRecords(frame, view, plan);
        })
        .and_then([&] {
            return writeShadowRecords(frame, plan);
        });
}
}
