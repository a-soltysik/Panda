// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
// clang-format on

#include "TextureResource.hpp"

#include <expected>
#include <memory>
#include <panda/Error.hpp>
#include <panda/Texture.hpp>
#include <utility>

#include "MemoryAllocation.hpp"
#include "MemoryPool.hpp"
#include "VulkanError.hpp"

namespace panda::detail
{
namespace
{
auto validateDescription(const TextureDesc& description) -> Result<void>
{
    constexpr auto supported =
        vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled |
        vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eColorAttachment |
        vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eInputAttachment;
    if (description.extent.width == 0 || description.extent.height == 0 ||
        description.format == vk::Format::eUndefined || !description.usage)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Invalid texture extent, format or usage")};
    }
    if (description.usage & ~supported)
    {
        return std::unexpected {makeError(ErrorCode::Unsupported, "Texture usage requires an unsupported feature")};
    }
    return {};
}

auto createNativeImage(const vk::raii::Device& device, const TextureDesc& description) -> Result<vk::raii::Image>
{
    const auto info = vk::ImageCreateInfo {
        .imageType = vk::ImageType::e2D,
        .format = description.format,
        .extent = vk::Extent3D {.width = description.extent.width, .height = description.extent.height, .depth = 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = vk::SampleCountFlagBits::e1,
        .tiling = vk::ImageTiling::eOptimal,
        .usage = description.usage,
        .sharingMode = vk::SharingMode::eExclusive,
        .initialLayout = vk::ImageLayout::eUndefined
    };
    auto image = vk::Image {};
    const auto result = (*device).createImage(&info, nullptr, &image, *device.getDispatcher());
    if (result != vk::Result::eSuccess)
    {
        return std::unexpected {makeVulkanError(result)};
    }
    return Result<vk::raii::Image> {std::in_place, device, image};
}
}

auto TextureResource::create(const vk::raii::Device& device, MemoryPool& pool, const TextureDesc& description)
    -> Result<std::shared_ptr<TextureResource>>
{
    const auto valid = validateDescription(description);
    if (!valid)
    {
        return std::unexpected {valid.error()};
    }
    auto image = createNativeImage(device, description);
    if (!image)
    {
        return std::unexpected {std::move(image.error())};
    }
    auto resource = std::shared_ptr<TextureResource> {
        new TextureResource {std::move(*image), description}
    };
    const auto bound = resource->bind(device, pool);
    if (!bound)
    {
        return std::unexpected {bound.error()};
    }
    return resource;
}

TextureResource::TextureResource(vk::raii::Image image, const TextureDesc& description)
    : _image {std::move(image)},
      _extent {description.extent},
      _format {description.format},
      _usage {description.usage}
{
}

auto TextureResource::bind(const vk::raii::Device& device, MemoryPool& pool) -> Result<void>
{
    const auto requirements =
        device.getImageMemoryRequirements2<vk::MemoryRequirements2, vk::MemoryDedicatedRequirements>(
            vk::ImageMemoryRequirementsInfo2 {.image = *_image});
    auto request = MemoryRequest {.requirements = requirements.get<vk::MemoryRequirements2>().memoryRequirements,
                                  .dedicated = requirements.get<vk::MemoryDedicatedRequirements>(),
                                  .resource = vk::MemoryDedicatedAllocateInfo {.image = *_image},
                                  .requiredProperties = vk::MemoryPropertyFlagBits::eDeviceLocal,
                                  .preferredProperties = {}};
    auto allocation = pool.allocate(request);
    if (!allocation)
    {
        return std::unexpected {std::move(allocation.error())};
    }
    _allocation = std::move(*allocation);
    const auto info = vk::BindImageMemoryInfo {.image = *_image,
                                               .memory = _allocation.getMemory(),
                                               .memoryOffset = _allocation.getRange().offset};
    const auto result = device.bindImageMemory2(info);
    if (result != vk::Result::eSuccess)
    {
        return std::unexpected {makeVulkanError(result)};
    }
    return {};
}

auto TextureResource::getExtent() const -> vk::Extent2D
{
    return _extent;
}

auto TextureResource::getFormat() const -> vk::Format
{
    return _format;
}

auto TextureResource::getUsage() const -> vk::ImageUsageFlags
{
    return _usage;
}

}
