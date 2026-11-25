#pragma once

// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <panda/base/Error.hpp>
#include <panda/rendering/RenderView.hpp>
#include <panda/rendering/TextureFilteringPreset.hpp>
#include <vector>

#include "renderer/FrameBuffers.hpp"
#include "renderer/FrameDescriptors.hpp"
#include "renderer/MaterialTextureBindings.hpp"
#include "renderer/ShadowAtlas.hpp"
#include "resources/memory/MemoryPool.hpp"

namespace panda::core::vulkan::renderer
{
struct FrameBindingsCreateInfo
{
    vk::PhysicalDevice physicalDevice;
    const vk::raii::Device& device;
    resources::MemoryPool& pool;
    std::size_t frameCount;
    std::uint32_t maxLights;
    std::shared_ptr<TextureFilteringPreset> textureFilteringPreset;
};

class FrameBindings final
{
public:
    [[nodiscard]] static auto validateDeviceLimits(vk::PhysicalDevice physicalDevice, std::uint32_t maxLights)
        -> std::expected<void, Error>;
    [[nodiscard]] static auto validateDeviceLimits(const vk::PhysicalDeviceLimits& limits, std::uint32_t maxLights)
        -> std::expected<void, Error>;
    [[nodiscard]] static auto create(const FrameBindingsCreateInfo& info)
        -> std::expected<std::unique_ptr<FrameBindings>, Error>;

    FrameBindings(const FrameBindings&) = delete;
    auto operator=(const FrameBindings&) -> FrameBindings& = delete;
    FrameBindings(FrameBindings&&) = delete;
    auto operator=(FrameBindings&&) -> FrameBindings& = delete;
    ~FrameBindings();

    [[nodiscard]] auto getPipelineLayout() const noexcept -> vk::PipelineLayout;
    [[nodiscard]] auto getFrameDescriptors() const noexcept -> const FrameDescriptors&;
    [[nodiscard]] auto getFrameDescriptorSetIndex() const noexcept -> std::uint32_t;
    [[nodiscard]] auto getTextureDescriptorSetIndex() const noexcept -> std::uint32_t;
    [[nodiscard]] auto getFrameDescriptorSet(std::size_t frameIndex) const -> vk::DescriptorSet;
    [[nodiscard]] auto getShaderDescriptorSchema() const noexcept -> const std::vector<ShaderDescriptor>&;
    [[nodiscard]] auto getTextureDescriptorSet(std::size_t frameIndex, std::size_t drawIndex) const
        -> vk::DescriptorSet;
    [[nodiscard]] auto getTextureSampler(std::size_t frameIndex, const Texture& texture)
        -> std::expected<vk::Sampler, Error>;
    [[nodiscard]] auto getShadowAtlasPlan(std::size_t frameIndex) const noexcept -> const ShadowAtlasPlan&;
    [[nodiscard]] auto getShadowViewOffset(std::size_t frameIndex, std::uint32_t shadowViewIndex) const
        -> std::uint32_t;
    [[nodiscard]] auto getMeshInstanceBuffer(std::size_t frameIndex) const -> vk::Buffer;
    [[nodiscard]] auto getMeshInstanceOffset(std::size_t frameIndex, std::size_t drawIndex) const -> vk::DeviceSize;
    [[nodiscard]] auto getEffectiveAnisotropy(TextureFilteringPreset preset) const noexcept -> float;
    [[nodiscard]] auto updateFrame(std::size_t frameIndex, const RenderView& view, vk::ImageView shadowView)
        -> std::expected<void, Error>;
    [[nodiscard]] auto updateFrame(std::size_t frameIndex,
                                   const RenderView& view,
                                   vk::ImageView shadowArrayView,
                                   vk::ImageView textureFallbackView) -> std::expected<void, Error>;

private:
    FrameBindings(const FrameBindingsCreateInfo& info, const vk::PhysicalDeviceLimits& limits);
    [[nodiscard]] auto initialize(const vk::PhysicalDeviceLimits& limits) -> Result<void>;

    std::uint32_t _maxLights;
    FrameBuffers _buffers;
    FrameDescriptors _descriptors;
    MaterialTextureBindings _textures;
    std::vector<ShadowAtlasPlan> _shadowPlans;
};
}
