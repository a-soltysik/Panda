// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include <expected>
#include <memory>
#include <panda/Error.hpp>
#include <panda/Texture.hpp>
#include <utility>

#include "ScopedMock.hpp"
#include "panda/core/TextureResourceMock.hpp"
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
    auto& mock = test::ScopedMock<test::TextureResourceMock>::getActiveMock();
    const auto created = mock.create(device, pool, description);
    if (!created)
    {
        return std::unexpected {created.error()};
    }
    return std::shared_ptr<TextureResource> {
        new TextureResource {pool, vk::raii::Image {nullptr}, description}
    };
}

TextureResource::TextureResource(texture_resources::MemoryPool& pool,
                                 vk::raii::Image image,
                                 const TextureDesc& description)
    : _image {std::move(image)},
      _pool {pool},
      _extent {description.extent},
      _format {description.format},
      _usage {description.usage}
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

auto TextureResource::getNative() const -> vk::Image
{
    return test::ScopedMock<test::TextureResourceMock>::getActiveMock().getNative(*this);
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
