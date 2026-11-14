// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include "resources/OffscreenTargetResource.hpp"

#include <cstdint>
#include <expected>
#include <memory>
#include <panda/Error.hpp>
#include <panda/OffscreenTarget.hpp>
#include <utility>

#include "resources/TextureResource.hpp"
#include "resources/memory/MemoryPool.hpp"

namespace panda::detail
{
OffscreenTargetResource::OffscreenTargetResource(std::shared_ptr<TextureResource> color) noexcept
    : _color {std::move(color)}
{
}

auto OffscreenTargetResource::getResource(const OffscreenTarget& target)
    -> const std::shared_ptr<OffscreenTargetResource>&
{
    return target._resource;
}

auto OffscreenTargetResource::create(const vk::raii::Device& device,
                                     core::vulkan::resources::MemoryPool& pool,
                                     const OffscreenTargetDesc& description)
    -> Result<std::shared_ptr<OffscreenTargetResource>>
{
    if (description.extent.width == 0 || description.extent.height == 0)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Offscreen target extent must be nonzero")};
    }
    auto color = TextureResource::create(
        device,
        pool,
        {.extent = description.extent,
         .format = vk::Format::eR8G8B8A8Srgb,
         .usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc});
    if (!color)
    {
        return std::unexpected {color.error()};
    }
    return std::shared_ptr<OffscreenTargetResource> {new OffscreenTargetResource {std::move(*color)}};
}

auto OffscreenTargetResource::getExtent() const noexcept -> vk::Extent2D
{
    return _color->getExtent();
}

auto OffscreenTargetResource::getFormat() const noexcept -> vk::Format
{
    return _color->getFormat();
}

auto OffscreenTargetResource::getImage() const noexcept -> vk::Image
{
    return _color->getNative();
}

auto OffscreenTargetResource::getImageView() const noexcept -> vk::ImageView
{
    return _color->getNativeView();
}

auto OffscreenTargetResource::belongsTo(const core::vulkan::resources::MemoryPool& pool) const -> bool
{
    return _color->belongsTo(pool);
}

auto OffscreenTargetResource::getLastRenderCompletion() const noexcept -> std::uint64_t
{
    return _lastRenderCompletion;
}

auto OffscreenTargetResource::isInitialized() const noexcept -> bool
{
    return _lastRenderCompletion != 0;
}

void OffscreenTargetResource::markRendered(std::uint64_t completion) noexcept
{
    _lastRenderCompletion = completion;
}
}
