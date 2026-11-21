// clang-format off
#include <panda/base/Error.hpp>
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
// clang-format on

#include "TextureResource.hpp"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <expected>
#include <memory>
#include <panda/diagnostics/Assert.hpp>
#include <panda/resources/PixelFormat.hpp>
#include <panda/resources/Texture.hpp>
#include <panda/vulkan/Error.hpp>
#include <utility>
#include <vector>

#include "ResourceMappings.hpp"
#include "resources/memory/MemoryAllocation.hpp"
#include "resources/memory/MemoryPool.hpp"
#include "upload/UploadBatchId.hpp"

namespace panda::detail
{
using core::vulkan::resources::MemoryPool;
using core::vulkan::resources::MemoryRequest;
using core::vulkan::upload::UploadBatchId;

namespace
{
auto fullMipLevelCount(vk::Extent2D extent) -> std::uint32_t
{
    return static_cast<std::uint32_t>(std::bit_width(std::max(extent.width, extent.height)));
}

auto isValidAddressMode(TextureAddressMode mode) -> bool
{
    return mode == TextureAddressMode::Repeat || mode == TextureAddressMode::MirroredRepeat ||
           mode == TextureAddressMode::ClampToEdge;
}

auto isValidTextureType(TextureType type) -> bool
{
    return type == TextureType::Texture2D || type == TextureType::CubeMap;
}

auto isSupportedGeneratedMipFormat(PixelFormat format) -> bool
{
    return format == PixelFormat::Rgba8Unorm || format == PixelFormat::Rgba8Srgb || format == PixelFormat::Bgra8Unorm ||
           format == PixelFormat::Bgra8Srgb;
}

auto isValidMipPolicy(const TextureDesc& description) -> bool
{
    return description.mipPolicy == TextureMipPolicy::SingleLevel ||
           description.mipPolicy == TextureMipPolicy::Supplied ||
           (description.mipPolicy == TextureMipPolicy::Generate && description.type == TextureType::Texture2D &&
            isSupportedGeneratedMipFormat(description.format)) ||
           (description.mipPolicy == TextureMipPolicy::GenerateSpecularEnvironment &&
            description.type == TextureType::CubeMap && description.specularEnvironmentSamplesPerTexel != 0);
}

auto validateDescription(const TextureResourceCreateInfo& description) -> Result<void>
{
    if (description.extent.width == 0 || description.extent.height == 0 || description.arrayLayers == 0 ||
        description.format == vk::Format::eUndefined || !description.usage || description.mipLevels == 0 ||
        description.mipLevels > fullMipLevelCount(description.extent) ||
        (description.cubeCompatible &&
         (description.extent.width != description.extent.height || description.arrayLayers != 6)))
    {
        return std::unexpected {
            makeError(ErrorCode::InvalidArgument, "Invalid texture extent, layers, format or usage")};
    }
    constexpr auto viewCapable =
        vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eColorAttachment |
        vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eInputAttachment;
    if (!(description.usage & viewCapable))
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Texture usage cannot create an image view")};
    }
    return {};
}

auto validateImageLimits(const vk::raii::PhysicalDevice& physicalDevice,
                         const TextureResourceCreateInfo& description,
                         const vk::ImageFormatProperties& support) -> Result<void>
{
    if (description.extent.width > support.maxExtent.width || description.extent.height > support.maxExtent.height ||
        (description.cubeCompatible && description.mipLevels > support.maxMipLevels))
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Texture extent exceeds device limits")};
    }
    if (description.arrayLayers > 1 &&
        (description.arrayLayers > support.maxArrayLayers ||
         description.arrayLayers > physicalDevice.getProperties().limits.maxImageArrayLayers))
    {
        return std::unexpected {makeError(ErrorCode::Unsupported, "Texture array layer count exceeds device limits")};
    }
    return {};
}

auto validateCubeSampling(const vk::raii::PhysicalDevice& physicalDevice, const TextureResourceCreateInfo& description)
    -> Result<void>
{
    if (description.cubeCompatible && !(physicalDevice.getFormatProperties(description.format).optimalTilingFeatures &
                                        vk::FormatFeatureFlagBits::eSampledImageFilterLinear))
    {
        return std::unexpected {makeError(ErrorCode::Unsupported, "Cubemap format does not support linear sampling")};
    }
    return {};
}

auto validateFormatAndExtent(const vk::raii::PhysicalDevice& physicalDevice,
                             const TextureResourceCreateInfo& description) -> Result<void>
{
    const auto support = physicalDevice.getImageFormatProperties(
        description.format,
        vk::ImageType::e2D,
        vk::ImageTiling::eOptimal,
        description.usage,
        description.cubeCompatible ? vk::ImageCreateFlagBits::eCubeCompatible : vk::ImageCreateFlags {});
    if (support.result == vk::Result::eErrorFormatNotSupported)
    {
        return std::unexpected {makeError(ErrorCode::Unsupported, "Texture format and usage are unsupported")};
    }
    if (support.result != vk::Result::eSuccess)
    {
        return std::unexpected {makeVulkanError(support.result)};
    }
    if (const auto valid = validateImageLimits(physicalDevice, description, support.value); !valid)
    {
        return valid;
    }
    if (const auto valid = validateCubeSampling(physicalDevice, description); !valid)
    {
        return valid;
    }
    return {};
}

auto validateTextureRequest(const vk::raii::PhysicalDevice& physicalDevice,
                            const TextureResourceCreateInfo& description) -> Result<void>
{
    if (const auto valid = validateDescription(description); !valid)
    {
        return std::unexpected {valid.error()};
    }
    return validateFormatAndExtent(physicalDevice, description);
}

auto makeTextureCreateInfo(const TextureDesc& description) -> Result<TextureResourceCreateInfo>
{
    if (description.extent.x == 0 || description.extent.y == 0 || !isValidTextureType(description.type) ||
        !isValidMipPolicy(description) || !isValidAddressMode(description.addressMode) ||
        (description.type == TextureType::CubeMap &&
         (description.extent.x != description.extent.y || description.format != PixelFormat::Rgba16Float)))
    {
        return std::unexpected {
            makeError(ErrorCode::InvalidArgument, "Invalid texture extent, mip policy or address mode")};
    }
    const auto format = mapPixelFormat(description.format);
    if (!format)
    {
        return std::unexpected {format.error()};
    }
    const auto usage = mapTextureUsage(description.usage);
    if (!usage)
    {
        return std::unexpected {usage.error()};
    }
    const auto extent = vk::Extent2D {.width = description.extent.x, .height = description.extent.y};
    return TextureResourceCreateInfo {
        .extent = extent,
        .format = *format,
        .usage = *usage,
        .mipLevels = description.mipPolicy == TextureMipPolicy::SingleLevel ? 1U : fullMipLevelCount(extent),
        .arrayLayers = description.type == TextureType::CubeMap ? 6U : 1U,
        .cubeCompatible = description.type == TextureType::CubeMap};
}

auto validateSampleCount(vk::ImageUsageFlags usage, vk::SampleCountFlagBits samples) -> Result<void>
{
    const auto value = static_cast<std::uint32_t>(samples);
    if (value == 0 || value > static_cast<std::uint32_t>(vk::SampleCountFlagBits::e64) || (value & (value - 1)) != 0)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Invalid texture sample count")};
    }
    constexpr auto attachmentUsages = vk::ImageUsageFlagBits::eColorAttachment |
                                      vk::ImageUsageFlagBits::eDepthStencilAttachment |
                                      vk::ImageUsageFlagBits::eInputAttachment;
    if (samples != vk::SampleCountFlagBits::e1 && (usage & ~attachmentUsages))
    {
        return std::unexpected {makeError(ErrorCode::Unsupported, "Multisampled textures must be attachments")};
    }
    return {};
}

auto createNativeImage(const vk::raii::Device& device,
                       const TextureResourceCreateInfo& description,
                       vk::SampleCountFlagBits samples) -> Result<vk::raii::Image>
{
    const auto info = vk::ImageCreateInfo {
        .flags = description.cubeCompatible ? vk::ImageCreateFlagBits::eCubeCompatible : vk::ImageCreateFlags {},
        .imageType = vk::ImageType::e2D,
        .format = description.format,
        .extent = vk::Extent3D {.width = description.extent.width, .height = description.extent.height, .depth = 1},
        .mipLevels = description.mipLevels,
        .arrayLayers = description.arrayLayers,
        .samples = samples,
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

auto imageAspect(vk::Format format) -> vk::ImageAspectFlags
{
    if (format == vk::Format::eS8Uint)
    {
        return vk::ImageAspectFlagBits::eStencil;
    }
    if (format == vk::Format::eD16Unorm || format == vk::Format::eX8D24UnormPack32 ||
        format == vk::Format::eD32Sfloat || format == vk::Format::eD16UnormS8Uint ||
        format == vk::Format::eD24UnormS8Uint || format == vk::Format::eD32SfloatS8Uint)
    {
        // Depth-only views work for both depth sampling and depth/stencil attachments.
        return vk::ImageAspectFlagBits::eDepth;
    }
    return vk::ImageAspectFlagBits::eColor;
}

struct NativeImageViewDescription
{
    vk::ImageViewType type {vk::ImageViewType::e2D};
    std::uint32_t levelCount {1};
    std::uint32_t baseArrayLayer {0};
    std::uint32_t layerCount {1};
};

auto createNativeView(const vk::raii::Device& device,
                      vk::Image image,
                      vk::Format format,
                      NativeImageViewDescription description = {}) -> Result<vk::raii::ImageView>
{
    const auto info = vk::ImageViewCreateInfo {
        .image = image,
        .viewType = description.type,
        .format = format,
        .components = {.r = vk::ComponentSwizzle::eIdentity,
                       .g = vk::ComponentSwizzle::eIdentity,
                       .b = vk::ComponentSwizzle::eIdentity,
                       .a = vk::ComponentSwizzle::eIdentity},
        .subresourceRange = {.aspectMask = imageAspect(format),
                       .baseMipLevel = 0,
                       .levelCount = description.levelCount,
                       .baseArrayLayer = description.baseArrayLayer,
                       .layerCount = description.layerCount}
    };
    auto imageView = vk::ImageView {};
    const auto result = (*device).createImageView(&info, nullptr, &imageView, *device.getDispatcher());
    if (result != vk::Result::eSuccess)
    {
        return std::unexpected {makeVulkanError(result)};
    }
    return Result<vk::raii::ImageView> {std::in_place, device, imageView};
}
}

auto TextureResource::getResource(const Texture& texture) -> const std::shared_ptr<TextureResource>&
{
    return texture._resource;
}

auto TextureResource::create(const vk::raii::Device& device, MemoryPool& pool, const TextureDesc& description)
    -> Result<std::shared_ptr<TextureResource>>
{
    return create(device, pool, description, vk::SampleCountFlagBits::e1);
}

auto TextureResource::createBoundResource(const vk::raii::Device& device,
                                          MemoryPool& pool,
                                          const TextureResourceCreateInfo& description,
                                          vk::SampleCountFlagBits samples) -> Result<std::shared_ptr<TextureResource>>
{
    auto image = createNativeImage(device, description, samples);
    if (!image)
    {
        return std::unexpected {std::move(image.error())};
    }
    auto resource = std::shared_ptr<TextureResource> {
        new TextureResource {pool, std::move(*image), description, samples}
    };
    const auto bound = resource->bind(device);
    if (!bound)
    {
        return std::unexpected {bound.error()};
    }
    return resource;
}

auto TextureResource::create(const vk::raii::Device& device,
                             MemoryPool& pool,
                             const TextureDesc& description,
                             vk::SampleCountFlagBits samples) -> Result<std::shared_ptr<TextureResource>>
{
    const auto nativeDescription = makeTextureCreateInfo(description);
    if (!nativeDescription)
    {
        return std::unexpected {nativeDescription.error()};
    }
    auto resource = createNative(device, pool, *nativeDescription, samples);
    if (resource)
    {
        (*resource)->_publicFormat = description.format;
        (*resource)->_publicType = description.type;
        (*resource)->_publicUsage = description.usage;
        (*resource)->_mipPolicy = description.mipPolicy;
        (*resource)->_specularEnvironmentSamplesPerTexel = description.specularEnvironmentSamplesPerTexel;
        (*resource)->_addressMode = description.addressMode;
    }
    return resource;
}

auto TextureResource::createNative(const vk::raii::Device& device,
                                   MemoryPool& pool,
                                   const TextureResourceCreateInfo& description,
                                   vk::SampleCountFlagBits samples) -> Result<std::shared_ptr<TextureResource>>
{
    if (const auto validSamples = validateSampleCount(description.usage, samples); !validSamples)
    {
        return std::unexpected {validSamples.error()};
    }
    if (const auto valid = validateTextureRequest(pool.getPhysicalDevice(), description); !valid)
    {
        return std::unexpected {valid.error()};
    }
    auto resource = createBoundResource(device, pool, description, samples);
    if (!resource)
    {
        return std::unexpected {std::move(resource.error())};
    }
    if (const auto views = (*resource)->createViews(device, description); !views)
    {
        return std::unexpected {views.error()};
    }
    return std::move(*resource);
}

TextureResource::TextureResource(MemoryPool& pool,
                                 vk::raii::Image image,
                                 const TextureResourceCreateInfo& description,
                                 vk::SampleCountFlagBits samples)
    : _image {std::move(image)},
      _pool {pool},
      _extent {description.extent},
      _arrayLayers {description.arrayLayers},
      _format {description.format},
      _usage {description.usage},
      _samples {samples},
      _mipLevels {description.mipLevels}
{
}

auto TextureResource::createViews(const vk::raii::Device& device, const TextureResourceCreateInfo& description)
    -> Result<void>
{
    const auto viewDescription = description.cubeCompatible
                                     ? NativeImageViewDescription {.type = vk::ImageViewType::eCube,
                                                                   .levelCount = description.mipLevels,
                                                                   .layerCount = 6}
                                     : NativeImageViewDescription {.levelCount = description.mipLevels};
    auto view = createNativeView(device, *_image, description.format, viewDescription);
    if (!view)
    {
        return std::unexpected {std::move(view.error())};
    }
    _view = std::move(*view);
    if (description.createArrayView || (description.arrayLayers > 1 && !description.cubeCompatible))
    {
        return createArrayViews(device, description);
    }
    return {};
}

auto TextureResource::createArrayViews(const vk::raii::Device& device, const TextureResourceCreateInfo& description)
    -> Result<void>
{
    auto arrayView = createNativeView(device,
                                      *_image,
                                      description.format,
                                      {.type = vk::ImageViewType::e2DArray,
                                       .levelCount = description.mipLevels,
                                       .layerCount = description.arrayLayers});
    if (!arrayView)
    {
        return std::unexpected {std::move(arrayView.error())};
    }
    _arrayView = std::move(*arrayView);
    _layerViews.reserve(description.arrayLayers - 1U);
    for (auto layer = std::uint32_t {1}; layer < description.arrayLayers; ++layer)
    {
        auto layerView = createNativeView(device,
                                          *_image,
                                          description.format,
                                          {.levelCount = description.mipLevels, .baseArrayLayer = layer});
        if (!layerView)
        {
            return std::unexpected {std::move(layerView.error())};
        }
        _layerViews.push_back(std::move(*layerView));
    }
    return {};
}

auto TextureResource::bind(const vk::raii::Device& device) -> Result<void>
{
    const auto requirements =
        device.getImageMemoryRequirements2<vk::MemoryRequirements2, vk::MemoryDedicatedRequirements>(
            vk::ImageMemoryRequirementsInfo2 {.image = *_image});
    auto request = MemoryRequest {.requirements = requirements.get<vk::MemoryRequirements2>().memoryRequirements,
                                  .dedicated = requirements.get<vk::MemoryDedicatedRequirements>(),
                                  .resource = vk::MemoryDedicatedAllocateInfo {.image = *_image},
                                  .exportHandleTypes = {},
                                  .requiredProperties = vk::MemoryPropertyFlagBits::eDeviceLocal,
                                  .preferredProperties = {}};
    auto allocation = _pool.allocate(request);
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

auto TextureResource::getPublicFormat() const -> PixelFormat
{
    return _publicFormat;
}

auto TextureResource::getPublicType() const -> TextureType
{
    return _publicType;
}

auto TextureResource::getPublicUsage() const -> TextureUsage
{
    return _publicUsage;
}

auto TextureResource::getNative() const -> vk::Image
{
    return *_image;
}

auto TextureResource::getNativeView() const -> vk::ImageView
{
    return *_view;
}

auto TextureResource::getNativeArrayView() const -> vk::ImageView
{
    return *_arrayView;
}

auto TextureResource::getLayerView(std::uint32_t layer) const -> vk::ImageView
{
    if (layer == 0)
    {
        return *_view;
    }
    return *_layerViews.at(layer - 1U);
}

auto TextureResource::getArrayLayers() const noexcept -> std::uint32_t
{
    return _arrayLayers;
}

auto TextureResource::belongsTo(const MemoryPool& pool) const -> bool
{
    return &_pool == &pool;
}

auto TextureResource::isUploaded() const -> bool
{
    return _uploaded;
}

auto TextureResource::reserveUpload(UploadBatchId batchId) -> Result<void>
{
    if (_samples != vk::SampleCountFlagBits::e1)
    {
        return std::unexpected {makeError(ErrorCode::Unsupported, "Multisampled textures cannot be uploaded")};
    }
    if (_uploaded || (_uploadBatchId && *_uploadBatchId != batchId))
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Texture is already uploaded or reserved")};
    }
    _uploadBatchId = batchId;
    return {};
}

void TextureResource::cancelUpload(UploadBatchId batchId)
{
    expect(_uploadBatchId == batchId, "Texture upload reservation was lost");
    _uploadBatchId.reset();
}

void TextureResource::commitUpload(UploadBatchId batchId)
{
    expect(_uploadBatchId == batchId, "Texture upload reservation was lost");
    _uploadBatchId.reset();
    _uploaded = true;
}

}
