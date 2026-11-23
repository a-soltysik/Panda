// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include "renderer/FrameBindings.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <memory>
#include <panda/base/Error.hpp>
#include <panda/diagnostics/Assert.hpp>
#include <panda/rendering/RenderView.hpp>
#include <panda/rendering/TextureFilteringPreset.hpp>
#include <utility>
#include <vector>

#include "renderer/ShaderData.hpp"
#include "renderer/ShaderReflection.hpp"
#include "renderer/ShadowAtlas.hpp"

namespace panda::core::vulkan::renderer
{
FrameBindings::FrameBindings(const FrameBindingsCreateInfo& info, const vk::PhysicalDeviceLimits& limits)
    : _maxLights {info.maxLights},
      _buffers {info.device, info.pool, limits, info.frameCount},
      _descriptors {info.device},
      _textures {info.device, info.physicalDevice, limits.maxSamplerAnisotropy, info.textureFilteringPreset},
      _shadowPlans(info.frameCount)
{
}

FrameBindings::~FrameBindings() = default;

auto FrameBindings::validateDeviceLimits(vk::PhysicalDevice physicalDevice, std::uint32_t maxLights)
    -> std::expected<void, Error>
{
    return validateDeviceLimits(physicalDevice.getProperties().limits, maxLights);
}

auto FrameBindings::validateDeviceLimits(const vk::PhysicalDeviceLimits& limits, std::uint32_t maxLights)
    -> std::expected<void, Error>
{
    if (limits.maxStorageBufferRange < sizeof(MaterialRecord) ||
        limits.maxStorageBufferRange / sizeof(LightRecord) < maxLights || limits.maxDescriptorSetStorageBuffers < 3 ||
        limits.maxPerStageDescriptorStorageBuffers < 3 ||
        limits.minUniformBufferOffsetAlignment > std::numeric_limits<std::uint32_t>::max())
    {
        return std::unexpected {
            makeError(ErrorCode::Unsupported, "Device descriptor-buffer limits are below renderer needs")};
    }
    return {};
}

auto FrameBindings::create(const FrameBindingsCreateInfo& info) -> std::expected<std::unique_ptr<FrameBindings>, Error>
{
    const auto limits = info.physicalDevice.getProperties().limits;
    if (const auto supported = validateDeviceLimits(limits, info.maxLights); !supported)
    {
        return std::unexpected {supported.error()};
    }
    if (info.frameCount == 0 || info.frameCount > std::numeric_limits<std::uint32_t>::max() / 3U)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Invalid frame binding count")};
    }
    auto bindings = std::unique_ptr<FrameBindings> {
        new FrameBindings {info, limits}
    };
    return bindings->initialize(limits).transform([&] {
        return std::move(bindings);
    });
}

auto FrameBindings::initialize(const vk::PhysicalDeviceLimits& limits) -> Result<void>
{
    return _descriptors.initialize(limits, _shadowPlans.size())
        .and_then([this] {
            return _descriptors.initializeShadowSampler();
        })
        .and_then([this] {
            return _textures.initialize(_descriptors.textureLayout(),
                                        _descriptors.materialTextureBindings(),
                                        _shadowPlans.size());
        })
        .and_then([this] {
            return _descriptors.initializeEnvironmentSampler();
        })
        .and_then([this] {
            return _buffers.initializeFrameBuffers();
        });
}

auto FrameBindings::getPipelineLayout() const noexcept -> vk::PipelineLayout
{
    return _descriptors.getPipelineLayout();
}

auto FrameBindings::getFrameDescriptorSetIndex() const noexcept -> std::uint32_t
{
    return _descriptors.getFrameDescriptorSetIndex();
}

auto FrameBindings::getTextureDescriptorSetIndex() const noexcept -> std::uint32_t
{
    return _descriptors.getTextureDescriptorSetIndex();
}

auto FrameBindings::getFrameDescriptorSet(std::size_t frameIndex) const -> vk::DescriptorSet
{
    return _descriptors.getFrameDescriptorSet(frameIndex);
}

auto FrameBindings::getShaderDescriptorSchema() const noexcept -> const std::vector<ShaderDescriptor>&
{
    return _descriptors.getShaderDescriptorSchema();
}

auto FrameBindings::getTextureDescriptorSet(std::size_t frameIndex, std::size_t drawIndex) const -> vk::DescriptorSet
{
    return _textures.getSet(frameIndex, drawIndex);
}

auto FrameBindings::getShadowAtlasPlan(std::size_t frameIndex) const noexcept -> const ShadowAtlasPlan&
{
    return _shadowPlans.at(frameIndex);
}

auto FrameBindings::getShadowViewOffset(std::size_t frameIndex, std::uint32_t shadowViewIndex) const -> std::uint32_t
{
    const auto& shadowViews = _shadowPlans.at(frameIndex).views;
    ::panda::expect(shadowViewIndex < shadowViews.size(), "Shadow view index is out of range");
    return _buffers.shadowViewOffset(shadowViewIndex);
}

auto FrameBindings::getEffectiveAnisotropy(TextureFilteringPreset preset) const noexcept -> float
{
    return _textures.getEffectiveAnisotropy(preset);
}

auto FrameBindings::updateFrame(std::size_t frameIndex, const RenderView& view, vk::ImageView shadowView)
    -> std::expected<void, Error>
{
    return updateFrame(frameIndex, view, shadowView, shadowView);
}

auto FrameBindings::updateFrame(std::size_t frameIndex,
                                const RenderView& view,
                                vk::ImageView shadowArrayView,
                                vk::ImageView textureFallbackView) -> std::expected<void, Error>
{
    if (view.lights.size() > _maxLights)
    {
        return std::unexpected {
            makeError(ErrorCode::ResourceExhausted, "Render view light count exceeds the Engine maxLights budget")};
    }
    return buildShadowAtlasPlan(view).and_then([&](auto plan) {
        return _buffers.update(frameIndex, view, plan)
            .and_then([&] {
                return _textures.ensureSets(frameIndex, view.meshes.size());
            })
            .and_then([&] {
                _descriptors.write(frameIndex, _buffers.descriptors(frameIndex), shadowArrayView, view);
                return _textures.write(frameIndex, view, textureFallbackView);
            })
            .transform([&] {
                _shadowPlans.at(frameIndex) = std::move(plan);
            });
    });
}
}
