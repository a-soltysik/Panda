#pragma once

#include <memory>
#include <panda/Error.hpp>
#include <panda/Texture.hpp>

#include "MemoryAllocation.hpp"
#include "MemoryPool.hpp"
#include "VulkanHpp.hpp"

namespace panda::detail
{
// Declaration order destroys the bound image before releasing its allocation.
// The context pool and device outlive this state, including every shared owner.
class TextureResource final
{
public:
    [[nodiscard]] static auto create(const vk::raii::Device& device, MemoryPool& pool, const TextureDesc& description)
        -> Result<std::shared_ptr<TextureResource>>;
    [[nodiscard]] auto getExtent() const -> vk::Extent2D;
    [[nodiscard]] auto getFormat() const -> vk::Format;
    [[nodiscard]] auto getUsage() const -> vk::ImageUsageFlags;

private:
    TextureResource(vk::raii::Image image, const TextureDesc& description);
    [[nodiscard]] auto bind(const vk::raii::Device& device, MemoryPool& pool) -> Result<void>;

    MemoryAllocation _allocation;
    vk::raii::Image _image;
    vk::Extent2D _extent;
    vk::Format _format;
    vk::ImageUsageFlags _usage;
};
}
