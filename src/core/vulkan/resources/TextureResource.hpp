#pragma once

#include <memory>
#include <optional>
#include <panda/Error.hpp>
#include <panda/Texture.hpp>

#include "MemoryAllocation.hpp"
#include "MemoryPool.hpp"
#include "UploadBatchId.hpp"
#include "VulkanHpp.hpp"

namespace panda::detail
{
// Declaration order destroys the bound image before releasing its allocation.
// The context pool and device outlive this state, including every shared owner.
class TextureResource final
{
public:
    [[nodiscard]] static auto getResource(const Texture& texture) -> const std::shared_ptr<TextureResource>&;
    [[nodiscard]] static auto create(const vk::raii::Device& device, MemoryPool& pool, const TextureDesc& description)
        -> Result<std::shared_ptr<TextureResource>>;
    [[nodiscard]] auto getExtent() const -> vk::Extent2D;
    [[nodiscard]] auto getFormat() const -> vk::Format;
    [[nodiscard]] auto getUsage() const -> vk::ImageUsageFlags;
    [[nodiscard]] auto getNative() const -> vk::Image;
    [[nodiscard]] auto belongsTo(const MemoryPool& pool) const -> bool;
    [[nodiscard]] auto reserveUpload(UploadBatchId batchId) -> Result<void>;
    void cancelUpload(UploadBatchId batchId);
    void commitUpload(UploadBatchId batchId);

private:
    TextureResource(MemoryPool& pool, vk::raii::Image image, const TextureDesc& description);
    [[nodiscard]] auto bind(const vk::raii::Device& device) -> Result<void>;

    MemoryAllocation _allocation;
    vk::raii::Image _image;
    MemoryPool& _pool;
    vk::Extent2D _extent;
    vk::Format _format;
    vk::ImageUsageFlags _usage;
    std::optional<UploadBatchId> _uploadBatchId;
    bool _uploaded {false};
};
}
