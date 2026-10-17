#pragma once

#include <memory>
#include <optional>
#include <panda/Error.hpp>
#include <panda/Texture.hpp>

#include "VulkanHpp.hpp"
#include "resources/memory/MemoryAllocation.hpp"
#include "resources/memory/MemoryPool.hpp"
#include "upload/UploadBatchId.hpp"

namespace panda::detail
{
namespace texture_resources = ::panda::core::vulkan::resources;
namespace texture_upload = ::panda::core::vulkan::upload;

// Declaration order destroys the view and bound image before releasing their allocation.
// The context pool and device outlive this state, including every shared owner.
class TextureResource final
{
public:
    [[nodiscard]] static auto getResource(const Texture& texture) -> const std::shared_ptr<TextureResource>&;
    [[nodiscard]] static auto create(const vk::raii::Device& device,
                                     texture_resources::MemoryPool& pool,
                                     const TextureDesc& description) -> Result<std::shared_ptr<TextureResource>>;
    [[nodiscard]] auto getExtent() const -> vk::Extent2D;
    [[nodiscard]] auto getFormat() const -> vk::Format;
    [[nodiscard]] auto getUsage() const -> vk::ImageUsageFlags;
    [[nodiscard]] auto getNative() const -> vk::Image;
    [[nodiscard]] auto belongsTo(const texture_resources::MemoryPool& pool) const -> bool;
    [[nodiscard]] auto reserveUpload(texture_upload::UploadBatchId batchId) -> Result<void>;
    void cancelUpload(texture_upload::UploadBatchId batchId);
    void commitUpload(texture_upload::UploadBatchId batchId);

private:
    TextureResource(texture_resources::MemoryPool& pool, vk::raii::Image image, const TextureDesc& description);
    [[nodiscard]] auto bind(const vk::raii::Device& device) -> Result<void>;

    texture_resources::MemoryAllocation _allocation;
    vk::raii::Image _image;
    vk::raii::ImageView _view {nullptr};
    texture_resources::MemoryPool& _pool;
    vk::Extent2D _extent;
    vk::Format _format;
    vk::ImageUsageFlags _usage;
    std::optional<texture_upload::UploadBatchId> _uploadBatchId;
    bool _uploaded {false};
};
}
