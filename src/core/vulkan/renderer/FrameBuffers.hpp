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
#include <vector>

#include "renderer/ShaderData.hpp"
#include "renderer/ShadowAtlas.hpp"
#include "resources/memory/MemoryPool.hpp"

namespace panda::detail
{
class BufferResource;
}

namespace panda::core::vulkan::renderer
{
struct FrameBufferDescriptors
{
    vk::DescriptorBufferInfo uniform;
    vk::DescriptorBufferInfo material;
    vk::DescriptorBufferInfo lights;
    vk::DescriptorBufferInfo shadows;
    vk::DescriptorBufferInfo shadowView;
};

class FrameBuffers final
{
public:
    FrameBuffers(const vk::raii::Device& device,
                 resources::MemoryPool& pool,
                 const vk::PhysicalDeviceLimits& limits,
                 std::size_t frameCount);
    [[nodiscard]] auto initializeFrameBuffers() -> Result<void>;
    [[nodiscard]] auto update(std::size_t index, const RenderView& view, const ShadowAtlasPlan& plan) -> Result<void>;
    [[nodiscard]] auto descriptors(std::size_t index) const -> FrameBufferDescriptors;
    [[nodiscard]] auto shadowViewOffset(std::uint32_t index) const noexcept -> std::uint32_t;

private:
    struct FrameResources
    {
        std::shared_ptr<::panda::detail::BufferResource> uniformBuffer;
        std::shared_ptr<::panda::detail::BufferResource> materialBuffer;
        std::shared_ptr<::panda::detail::BufferResource> lightBuffer;
        std::shared_ptr<::panda::detail::BufferResource> shadowBuffer;
        std::shared_ptr<::panda::detail::BufferResource> shadowViewBuffer;
        std::uint64_t materialBufferCapacity {0};
        std::uint64_t lightBufferCapacity {0};
        std::uint64_t shadowBufferCapacity {0};
        std::uint64_t shadowViewBufferCapacity {0};
    };

    [[nodiscard]] auto initializeFrameBuffersForSlot(FrameResources& frame) -> std::expected<void, Error>;
    [[nodiscard]] auto createUploadBuffer(vk::DeviceSize size, vk::BufferUsageFlagBits usage)
        -> std::expected<std::shared_ptr<::panda::detail::BufferResource>, Error>;
    [[nodiscard]] auto ensureBufferCapacity(std::shared_ptr<::panda::detail::BufferResource>& buffer,
                                            std::uint64_t& capacity,
                                            std::uint64_t requiredCapacity,
                                            vk::BufferUsageFlagBits usage) -> std::expected<void, Error>;
    [[nodiscard]] static auto writeFrameUniform(FrameResources& frame, const RenderView& view, std::uint32_t lightCount)
        -> std::expected<void, Error>;
    [[nodiscard]] auto ensureMaterialBuffer(FrameResources& frame, std::size_t drawCount) -> std::expected<void, Error>;
    [[nodiscard]] auto ensureLightBuffer(FrameResources& frame, std::size_t lightCount) -> std::expected<void, Error>;
    [[nodiscard]] auto ensureShadowBuffers(FrameResources& frame, std::size_t shadowViewCount)
        -> std::expected<void, Error>;
    [[nodiscard]] auto ensureShadowRecordBuffer(FrameResources& frame, std::size_t bufferCount)
        -> std::expected<void, Error>;
    [[nodiscard]] auto ensureShadowViewBuffer(FrameResources& frame, std::size_t bufferCount)
        -> std::expected<void, Error>;
    [[nodiscard]] auto writeMaterialRecords(FrameResources& frame, const RenderView& view)
        -> std::expected<void, Error>;
    [[nodiscard]] auto writeLightRecords(FrameResources& frame, const RenderView& view, const ShadowAtlasPlan& plan)
        -> std::expected<void, Error>;
    [[nodiscard]] auto writeShadowRecords(FrameResources& frame, const ShadowAtlasPlan& plan) const
        -> std::expected<void, Error>;
    const vk::raii::Device& _device;
    resources::MemoryPool& _pool;
    std::uint64_t _maxStorageBufferRange;
    std::uint64_t _shadowViewStride;
    std::vector<FrameResources> _frames;
    std::vector<MaterialRecord> _materialRecords;
    std::vector<LightRecord> _lightRecords;
};
}
