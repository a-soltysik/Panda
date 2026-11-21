// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include <algorithm>
#include <bit>
#include <cstdint>
#include <expected>
#include <memory>
#include <panda/base/Error.hpp>
#include <panda/resources/PixelFormat.hpp>
#include <panda/resources/Texture.hpp>
#include <utility>

#include "ResourceMappings.hpp"
#include "ScopedMock.hpp"
#include "panda/core/TextureResourceMock.hpp"
#include "resources/TextureResource.hpp"
#include "resources/memory/MemoryAllocation.hpp"
#include "resources/memory/MemoryPool.hpp"
#include "upload/UploadBatchId.hpp"

namespace panda::detail
{
auto TextureResource::getResource(const Texture& texture) -> const std::shared_ptr<TextureResource>&
{
    return texture._resource;
}

auto TextureResource::create(const vk::raii::Device& device,
                             texture_resources::MemoryPool& pool,
                             const TextureDesc& description) -> Result<std::shared_ptr<TextureResource>>
{
    return create(device, pool, description, vk::SampleCountFlagBits::e1);
}

auto TextureResource::create(const vk::raii::Device& device,
                             texture_resources::MemoryPool& pool,
                             const TextureDesc& description,
                             vk::SampleCountFlagBits samples) -> Result<std::shared_ptr<TextureResource>>
{
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
    auto& mock = test::ScopedMock<test::TextureResourceMock>::getActiveMock();
    const auto created = mock.create(device, pool, description);
    if (!created)
    {
        return std::unexpected {created.error()};
    }
    const auto nativeDescription = TextureResourceCreateInfo {
        .extent = vk::Extent2D {.width = description.extent.x, .height = description.extent.y},
        .format = *format,
        .usage = *usage,
        .mipLevels =
            description.mipPolicy == TextureMipPolicy::SingleLevel
                ? 1U
                : static_cast<std::uint32_t>(std::bit_width(std::max(description.extent.x, description.extent.y))),
        .arrayLayers = description.type == TextureType::CubeMap ? 6U : 1U,
        .cubeCompatible = description.type == TextureType::CubeMap
    };
    auto resource = std::shared_ptr<TextureResource> {
        new TextureResource {pool, vk::raii::Image {nullptr}, nativeDescription, samples}
    };
    resource->_publicFormat = description.format;
    resource->_publicType = description.type;
    resource->_publicUsage = description.usage;
    resource->_mipPolicy = description.mipPolicy;
    resource->_specularEnvironmentSamplesPerTexel = description.specularEnvironmentSamplesPerTexel;
    resource->_addressMode = description.addressMode;
    return resource;
}

auto TextureResource::createNative(const vk::raii::Device& device,
                                   texture_resources::MemoryPool& pool,
                                   const TextureResourceCreateInfo& description,
                                   vk::SampleCountFlagBits samples) -> Result<std::shared_ptr<TextureResource>>
{
    auto& mock = test::ScopedMock<test::TextureResourceMock>::getActiveMock();
    const auto created = mock.createNative(device, pool, description, samples);
    if (!created)
    {
        return std::unexpected {created.error()};
    }
    return std::shared_ptr<TextureResource> {
        new TextureResource {pool, vk::raii::Image {nullptr}, description, samples}
    };
}

TextureResource::TextureResource(texture_resources::MemoryPool& pool,
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

auto TextureResource::getExtent() const -> vk::Extent2D
{
    return test::ScopedMock<test::TextureResourceMock>::getActiveMock().getExtent(*this);
}

auto TextureResource::getFormat() const -> vk::Format
{
    return test::ScopedMock<test::TextureResourceMock>::getActiveMock().getFormat(*this);
}

auto TextureResource::getUsage() const -> vk::ImageUsageFlags
{
    return test::ScopedMock<test::TextureResourceMock>::getActiveMock().getUsage(*this);
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
    return test::ScopedMock<test::TextureResourceMock>::getActiveMock().getNative(*this);
}

auto TextureResource::getNativeView() const -> vk::ImageView
{
    return test::ScopedMock<test::TextureResourceMock>::getActiveMock().getNativeView(*this);
}

auto TextureResource::getNativeArrayView() const -> vk::ImageView
{
    return getNativeView();
}

auto TextureResource::getLayerView(std::uint32_t layer) const -> vk::ImageView
{
    static_cast<void>(layer);
    return getNativeView();
}

auto TextureResource::getArrayLayers() const noexcept -> std::uint32_t
{
    return _arrayLayers;
}

auto TextureResource::belongsTo(const texture_resources::MemoryPool& pool) const -> bool
{
    return test::ScopedMock<test::TextureResourceMock>::getActiveMock().belongsTo(*this, pool);
}

// NOLINTNEXTLINE(readability-make-member-function-const)
auto TextureResource::reserveUpload(texture_upload::UploadBatchId batchId) -> Result<void>
{
    return test::ScopedMock<test::TextureResourceMock>::getActiveMock().reserveUpload(*this, batchId);
}

// NOLINTNEXTLINE(readability-make-member-function-const)
void TextureResource::cancelUpload(texture_upload::UploadBatchId batchId)
{
    test::ScopedMock<test::TextureResourceMock>::getActiveMock().cancelUpload(*this, batchId);
}

// NOLINTNEXTLINE(readability-make-member-function-const)
void TextureResource::commitUpload(texture_upload::UploadBatchId batchId)
{
    test::ScopedMock<test::TextureResourceMock>::getActiveMock().commitUpload(*this, batchId);
}
}
