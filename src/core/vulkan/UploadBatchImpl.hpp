#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <panda/CompletionPoint.hpp>
#include <panda/Error.hpp>
#include <panda/UploadBatch.hpp>
#include <span>
#include <vector>

#include "UploadBatchId.hpp"
#include "UploadOperation.hpp"

namespace panda::detail
{
class BufferResource;
class MeshResource;
class TextureResource;
class UploadManager;

// Unsubmitted writes retain both sides. A successful submit copies these owners
// into the manager's pending packet before this state can release them.
class UploadBatchImpl final
{
public:
    UploadBatchImpl(UploadManager& manager, UploadBatchId id);
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
    [[nodiscard]] auto stage(std::span<const std::byte> bytes, std::uint64_t alignment) -> Result<StagingSlice>;
    [[nodiscard]] auto stageInCurrentPage(std::span<const std::byte> bytes, std::uint64_t alignment)
        -> Result<std::optional<StagingSlice>>;
    [[nodiscard]] auto createStagingPage(std::span<const std::byte> bytes) -> Result<StagingSlice>;
    [[nodiscard]] auto createBufferUpload(const std::shared_ptr<BufferResource>& destination,
                                          std::span<const std::byte> bytes) -> Result<BufferUpload>;
    [[nodiscard]] auto createMeshUploads(const std::shared_ptr<MeshResource>& destination,
                                         std::span<const std::byte> vertexBytes,
                                         std::span<const std::byte> indexBytes) -> Result<std::vector<UploadOperation>>;
    [[nodiscard]] auto hasTexture(const std::shared_ptr<TextureResource>& texture) const -> bool;

    UploadManager& _manager;
    UploadBatchId _id;
    std::vector<UploadOperation> _writes;
    std::vector<std::shared_ptr<BufferResource>> _stagingPages;
    std::uint64_t _stagingOffset {0};
};
}
