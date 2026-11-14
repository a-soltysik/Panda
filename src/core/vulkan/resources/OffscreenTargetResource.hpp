#pragma once

#include <cstdint>
#include <memory>
#include <panda/Error.hpp>
#include <panda/OffscreenTarget.hpp>

#include "VulkanHpp.hpp"
#include "resources/TextureResource.hpp"
#include "resources/memory/MemoryPool.hpp"

namespace panda::detail
{
class OffscreenTargetResource final
{
public:
    [[nodiscard]] static auto getResource(const OffscreenTarget& target)
        -> const std::shared_ptr<OffscreenTargetResource>&;
    [[nodiscard]] static auto create(const vk::raii::Device& device,
                                     core::vulkan::resources::MemoryPool& pool,
                                     const OffscreenTargetDesc& description)
        -> Result<std::shared_ptr<OffscreenTargetResource>>;
    [[nodiscard]] auto getExtent() const noexcept -> vk::Extent2D;
    [[nodiscard]] auto getFormat() const noexcept -> vk::Format;
    [[nodiscard]] auto getImage() const noexcept -> vk::Image;
    [[nodiscard]] auto getImageView() const noexcept -> vk::ImageView;
    [[nodiscard]] auto belongsTo(const core::vulkan::resources::MemoryPool& pool) const -> bool;
    [[nodiscard]] auto getLastRenderCompletion() const noexcept -> std::uint64_t;
    [[nodiscard]] auto isInitialized() const noexcept -> bool;
    void markRendered(std::uint64_t completion) noexcept;

private:
    explicit OffscreenTargetResource(std::shared_ptr<TextureResource> color) noexcept;

    std::shared_ptr<TextureResource> _color;
    std::uint64_t _lastRenderCompletion {0};
};
}
