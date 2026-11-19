#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <panda/base/Error.hpp>
#include <panda/submission/CompletionPoint.hpp>
#include <panda/submission/UploadBatch.hpp>
#include <span>
#include <vector>

#include "upload/UploadBatchId.hpp"
#include "upload/UploadOperation.hpp"

namespace panda::core::vulkan::upload
{
class UploadManager;
}

namespace panda::detail
{
namespace batch_upload = ::panda::core::vulkan::upload;

class BufferResource;
class MeshResource;
class TextureResource;

// Unsubmitted writes retain both sides. A successful submit copies these owners
// into the manager's pending packet before this state can release them.
class UploadBatchImpl final
{
public:
    UploadBatchImpl(batch_upload::UploadManager& manager, batch_upload::UploadBatchId id);
    ~UploadBatchImpl();
    UploadBatchImpl(const UploadBatchImpl&) = delete;
    auto operator=(const UploadBatchImpl&) -> UploadBatchImpl& = delete;
    UploadBatchImpl(UploadBatchImpl&&) = delete;
    auto operator=(UploadBatchImpl&&) -> UploadBatchImpl& = delete;
    [[nodiscard]] auto write(const std::shared_ptr<BufferResource>& destination,
                             std::uint64_t offset,
                             std::span<const std::byte> bytes) -> Result<void>;
    [[nodiscard]] auto write(const std::shared_ptr<MeshResource>& destination,
                             std::span<const std::byte> vertexBytes,
                             std::span<const std::byte> indexBytes) -> Result<void>;
    [[nodiscard]] auto write(const std::shared_ptr<TextureResource>& destination,
                             std::span<const std::byte> bytes,
                             const TextureUploadRegion& region) -> Result<void>;
    [[nodiscard]] auto submit() -> Result<CompletionPoint>;

private:
    struct TextureMipWriteRequest
    {
        std::shared_ptr<TextureResource> destination;
        std::span<const std::byte> bytes;
        TextureUploadRegion region;
        std::uint32_t rowLength;
        std::uint64_t bufferOffsetAlignment;
        bool reserved;
    };

    [[nodiscard]] auto stage(std::span<const std::byte> bytes, std::uint64_t alignment)
        -> Result<batch_upload::StagingSlice>;
    [[nodiscard]] auto stageInCurrentPage(std::span<const std::byte> bytes, std::uint64_t alignment)
        -> Result<std::optional<batch_upload::StagingSlice>>;
    [[nodiscard]] auto createStagingPage(std::span<const std::byte> bytes) -> Result<batch_upload::StagingSlice>;
    [[nodiscard]] auto createBufferUpload(const std::shared_ptr<BufferResource>& destination,
                                          std::span<const std::byte> bytes) -> Result<batch_upload::BufferUpload>;
    [[nodiscard]] auto createMeshUploads(const std::shared_ptr<MeshResource>& destination,
                                         std::span<const std::byte> vertexBytes,
                                         std::span<const std::byte> indexBytes)
        -> Result<std::vector<batch_upload::UploadOperation>>;
    [[nodiscard]] auto hasTexture(const std::shared_ptr<TextureResource>& texture) const -> bool;
    [[nodiscard]] auto hasTextureMip(const std::shared_ptr<TextureResource>& texture, std::uint32_t mipLevel) const
        -> bool;
    [[nodiscard]] auto appendGeneratedTextureMips(const TextureMipWriteRequest& request) -> Result<void>;
    [[nodiscard]] auto createGeneratedMipUploads(const TextureMipWriteRequest& request,
                                                 const std::vector<std::vector<std::byte>>& mipChain)
        -> Result<std::vector<batch_upload::UploadOperation>>;
    [[nodiscard]] auto appendTextureMip(const TextureMipWriteRequest& request) -> Result<void>;
    [[nodiscard]] auto isFirstSuppliedMipUpload(std::size_t index, const batch_upload::TextureUpload& upload) const
        -> bool;
    [[nodiscard]] auto hasCompleteSuppliedMipChain(const std::shared_ptr<TextureResource>& texture) const -> bool;
    [[nodiscard]] auto validateSuppliedMipChains() const -> Result<void>;

    batch_upload::UploadManager& _manager;
    batch_upload::UploadBatchId _id;
    std::vector<batch_upload::UploadOperation> _writes;
    std::vector<std::shared_ptr<BufferResource>> _stagingPages;
    std::uint64_t _stagingOffset {0};
};
}
