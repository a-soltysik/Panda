#pragma once

// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <panda/Context.hpp>
#include <panda/Error.hpp>
#include <panda/RenderView.hpp>
#include <vector>

#include "renderer/ShaderData.hpp"
#include "resources/memory/MemoryPool.hpp"

namespace panda::detail
{
class BufferResource;
}

namespace panda::core::vulkan::renderer
{
struct FrameBindingsCreateInfo
{
    vk::PhysicalDevice physicalDevice;
    const vk::raii::Device& device;
    resources::MemoryPool& pool;
    std::size_t frameCount;
    std::uint32_t maxLights {::panda::ContextOptions::defaultMaxLights};
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
    [[nodiscard]] auto getFrameDescriptorSet(std::size_t frameIndex) const -> vk::DescriptorSet;
    [[nodiscard]] auto getTextureDescriptorSet(std::size_t frameIndex, std::size_t drawIndex) const
        -> vk::DescriptorSet;
    [[nodiscard]] auto updateFrame(std::size_t frameIndex, const RenderView& view, vk::ImageView shadowView)
        -> std::expected<void, Error>;

private:
    struct FrameResources
    {
        std::shared_ptr<::panda::detail::BufferResource> uniformBuffer;
        std::shared_ptr<::panda::detail::BufferResource> materialBuffer;
        std::shared_ptr<::panda::detail::BufferResource> lightBuffer;
        std::uint64_t materialBufferCapacity {0};
        std::uint64_t lightBufferCapacity {0};
        vk::raii::DescriptorSet descriptorSet {nullptr};
        vk::raii::DescriptorPool textureDescriptorPool {nullptr};
        std::vector<vk::raii::DescriptorSet> textureDescriptorSets;
    };

    FrameBindings(vk::PhysicalDevice physicalDevice,
                  const vk::raii::Device& device,
                  resources::MemoryPool& pool,
                  std::size_t frameCount);

    [[nodiscard]] auto initialize(std::uint32_t maxLights) -> std::expected<void, Error>;
    [[nodiscard]] auto initializeDescriptorState() -> std::expected<void, Error>;
    [[nodiscard]] auto initializeDescriptorSetLayout() -> std::expected<void, Error>;
    [[nodiscard]] auto initializeTextureDescriptorSetLayout() -> std::expected<void, Error>;
    [[nodiscard]] auto initializePipelineLayout() -> std::expected<void, Error>;
    [[nodiscard]] auto initializeDescriptorPool() -> std::expected<void, Error>;
    [[nodiscard]] auto allocateFrameDescriptorSets() -> std::expected<void, Error>;
    [[nodiscard]] auto initializeFrameBuffers() -> std::expected<void, Error>;
    [[nodiscard]] auto initializeFrameBuffersForSlot(FrameResources& frame) -> std::expected<void, Error>;
    [[nodiscard]] auto createUploadBuffer(vk::DeviceSize size, vk::BufferUsageFlagBits usage)
        -> std::expected<std::shared_ptr<::panda::detail::BufferResource>, Error>;
    [[nodiscard]] auto initializeSamplers() -> std::expected<void, Error>;
    [[nodiscard]] auto initializeShadowSampler() -> std::expected<void, Error>;
    [[nodiscard]] auto initializeTextureSampler() -> std::expected<void, Error>;
    [[nodiscard]] static auto writeFrameUniform(FrameResources& frame, const RenderView& view, std::uint32_t lightCount)
        -> std::expected<void, Error>;
    [[nodiscard]] auto ensureMaterialBuffer(FrameResources& frame, std::size_t drawCount) -> std::expected<void, Error>;
    [[nodiscard]] auto ensureLightBuffer(FrameResources& frame, std::size_t lightCount) -> std::expected<void, Error>;
    [[nodiscard]] auto ensureTextureDescriptorSets(FrameResources& frame, std::size_t drawCount)
        -> std::expected<void, Error>;
    [[nodiscard]] auto writeMaterialRecords(FrameResources& frame, const RenderView& view)
        -> std::expected<void, Error>;
    [[nodiscard]] auto writeLightRecords(FrameResources& frame, const RenderView& view) -> std::expected<void, Error>;
    void updateFrameDescriptorSet(const FrameResources& frame, vk::ImageView shadowView);
    void writeTextureDescriptors(const FrameResources& frame, const RenderView& view, vk::ImageView shadowView);

    const vk::raii::Device& _device;
    resources::MemoryPool& _pool;
    std::uint64_t _maxStorageBufferRange {0};
    std::uint32_t _maxLights {::panda::ContextOptions::defaultMaxLights};
    vk::raii::DescriptorSetLayout _descriptorSetLayout {nullptr};
    vk::raii::DescriptorSetLayout _textureDescriptorSetLayout {nullptr};
    vk::raii::PipelineLayout _pipelineLayout {nullptr};
    vk::raii::Sampler _shadowSampler {nullptr};
    vk::raii::Sampler _textureSampler {nullptr};
    vk::raii::DescriptorPool _descriptorPool {nullptr};
    std::vector<FrameResources> _frames;
    std::vector<MaterialRecord> _materialRecords;
    std::vector<LightRecord> _lightRecords;
};
}
