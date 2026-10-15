// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
// clang-format on

#include "UploadBatchImpl.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iterator>
#include <memory>
#include <panda/CompletionPoint.hpp>
#include <panda/Error.hpp>
#include <panda/UploadBatch.hpp>
#include <span>
#include <utility>
#include <variant>
#include <vector>

#include "UploadBatchId.hpp"
#include "UploadManager.hpp"
#include "UploadOperation.hpp"
#include "resources/BufferResource.hpp"
#include "resources/MeshResource.hpp"
#include "resources/TextureResource.hpp"

namespace panda::detail
{
namespace
{
void releaseUploadReservation(const UploadOperation& operation, UploadBatchId batchId)
{
    const auto* upload = std::get_if<TextureUpload>(&operation);
    if (upload != nullptr && upload->firstForTexture)
    {
        upload->destination->cancelUpload(batchId);
    }
}

void commitUploadReservation(const UploadOperation& operation, UploadBatchId batchId)
{
    const auto* upload = std::get_if<TextureUpload>(&operation);
    if (upload != nullptr && upload->firstForTexture)
    {
        upload->destination->commitUpload(batchId);
    }
}

void commitUploadReservations(std::span<const UploadOperation> operations, UploadBatchId batchId)
{
    for (const auto& operation : operations)
    {
        commitUploadReservation(operation, batchId);
    }
}

auto refersTo([[maybe_unused]] const BufferUpload& upload,
              [[maybe_unused]] const std::shared_ptr<TextureResource>& texture) -> bool
{
    return false;
}

auto refersTo(const TextureUpload& upload, const std::shared_ptr<TextureResource>& texture) -> bool
{
    return upload.destination == texture;
}

auto reserveTexture(const std::shared_ptr<TextureResource>& texture, UploadBatchId batchId, bool alreadyInBatch)
    -> Result<bool>
{
    if (alreadyInBatch)
    {
        return false;
    }
    const auto reserved = texture->reserveUpload(batchId);
    if (!reserved)
    {
        return std::unexpected {reserved.error()};
    }
    return true;
}

auto createStaging(UploadManager& manager, std::span<const std::byte> bytes) -> Result<std::shared_ptr<BufferResource>>
{
    if (const auto collected = manager.collect(); !collected)
    {
        return std::unexpected {collected.error()};
    }
    return manager.createStaging(bytes);
}

auto validateMeshWriteSizes(const MeshResource& destination,
                            std::span<const std::byte> vertexBytes,
                            std::span<const std::byte> indexBytes) -> Result<void>
{
    const auto& layout = destination.getLayout();
    if (vertexBytes.size() != layout.getVertexSize() || indexBytes.size() != layout.getIndexSize())
    {
        return std::unexpected {
            makeError(ErrorCode::InvalidArgument, "Mesh upload data sizes do not match its description")};
    }
    return {};
}

auto validateMeshWriteBuffers(const UploadManager& manager,
                              const MeshResource& destination,
                              std::span<const std::byte> vertexBytes,
                              std::span<const std::byte> indexBytes) -> Result<void>
{
    const auto validVertex = manager.validateWrite(destination.getVertexBuffer(), 0, vertexBytes.size());
    if (!validVertex)
    {
        return validVertex;
    }
    const auto& indexBuffer = destination.getIndexBuffer();
    if (indexBuffer)
    {
        const auto validIndices = manager.validateWrite(indexBuffer, 0, indexBytes.size());
        if (!validIndices)
        {
            return validIndices;
        }
    }
    return {};
}

auto createBufferUpload(UploadManager& manager,
                        const std::shared_ptr<BufferResource>& destination,
                        std::span<const std::byte> bytes) -> Result<BufferUpload>
{
    auto staging = createStaging(manager, bytes);
    if (!staging)
    {
        return std::unexpected {std::move(staging.error())};
    }
    return BufferUpload {.staging = std::move(*staging), .destination = destination, .offset = 0, .size = bytes.size()};
}

auto validateMeshWrite(const UploadManager& manager,
                       const std::shared_ptr<MeshResource>& destination,
                       std::span<const std::byte> vertexBytes,
                       std::span<const std::byte> indexBytes) -> Result<void>
{
    if (!destination)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Mesh handle is empty")};
    }
    const auto validSizes = validateMeshWriteSizes(*destination, vertexBytes, indexBytes);
    if (!validSizes)
    {
        return std::unexpected {validSizes.error()};
    }
    return validateMeshWriteBuffers(manager, *destination, vertexBytes, indexBytes);
}

auto appendIndexUpload(UploadManager& manager,
                       const MeshResource& destination,
                       std::span<const std::byte> indexBytes,
                       std::vector<UploadOperation>& writes) -> Result<void>
{
    const auto& indexBuffer = destination.getIndexBuffer();
    if (!indexBuffer)
    {
        return {};
    }
    auto indexUpload = createBufferUpload(manager, indexBuffer, indexBytes);
    if (!indexUpload)
    {
        return std::unexpected {std::move(indexUpload.error())};
    }
    writes.emplace_back(std::move(*indexUpload));
    return {};
}

auto createMeshUploads(UploadManager& manager,
                       const std::shared_ptr<MeshResource>& destination,
                       std::span<const std::byte> vertexBytes,
                       std::span<const std::byte> indexBytes) -> Result<std::vector<UploadOperation>>
{
    const auto valid = validateMeshWrite(manager, destination, vertexBytes, indexBytes);
    if (!valid)
    {
        return std::unexpected {valid.error()};
    }

    const auto& vertexBuffer = destination->getVertexBuffer();
    auto vertexUpload = createBufferUpload(manager, vertexBuffer, vertexBytes);
    if (!vertexUpload)
    {
        return std::unexpected {std::move(vertexUpload.error())};
    }
    auto writes = std::vector<UploadOperation> {};
    writes.reserve(destination->getIndexBuffer() ? 2 : 1);
    writes.emplace_back(std::move(*vertexUpload));
    const auto indexUpload = appendIndexUpload(manager, *destination, indexBytes, writes);
    if (!indexUpload)
    {
        return std::unexpected {indexUpload.error()};
    }
    return writes;
}

void cancelReservation(const std::shared_ptr<TextureResource>& destination, UploadBatchId batchId, bool reserved)
{
    if (reserved)
    {
        destination->cancelUpload(batchId);
    }
}
}

UploadBatchImpl::UploadBatchImpl(UploadManager& manager, UploadBatchId id)
    : _manager {manager},
      _id {id}
{
    _manager.retainBatch();
}

UploadBatchImpl::~UploadBatchImpl()
{
    for (const auto& operation : _writes)
    {
        releaseUploadReservation(operation, _id);
    }
    _manager.releaseBatch();
}

auto UploadBatchImpl::write(const std::shared_ptr<BufferResource>& destination,
                            std::uint64_t offset,
                            std::span<const std::byte> bytes) -> Result<void>
{
    const auto valid = _manager.validateWrite(destination, offset, bytes.size());
    if (!valid)
    {
        return valid;
    }
    auto staging = createStaging(_manager, bytes);
    if (!staging)
    {
        return std::unexpected {std::move(staging.error())};
    }
    _writes.emplace_back(BufferUpload {.staging = std::move(*staging),
                                       .destination = destination,
                                       .offset = offset,
                                       .size = bytes.size()});
    return {};
}

auto UploadBatchImpl::write(const std::shared_ptr<MeshResource>& destination,
                            std::span<const std::byte> vertexBytes,
                            std::span<const std::byte> indexBytes) -> Result<void>
{
    auto uploads = createMeshUploads(_manager, destination, vertexBytes, indexBytes);
    if (!uploads)
    {
        return std::unexpected {uploads.error()};
    }
    if (_writes.size() > _writes.max_size() - uploads->size())
    {
        return std::unexpected {makeError(ErrorCode::ResourceExhausted, "Upload batch cannot retain mesh writes")};
    }
    _writes.reserve(_writes.size() + uploads->size());
    std::ranges::move(*uploads, std::back_inserter(_writes));
    return {};
}

auto UploadBatchImpl::write(const std::shared_ptr<TextureResource>& destination,
                            std::span<const std::byte> bytes,
                            const TextureUploadRegion& region) -> Result<void>
{
    const auto rowLength = _manager.validateTextureWrite(destination, region, bytes.size());
    if (!rowLength)
    {
        return std::unexpected {rowLength.error()};
    }
    const auto reserved = reserveTexture(destination, _id, hasTexture(destination));
    if (!reserved)
    {
        return std::unexpected {reserved.error()};
    }
    auto staging = createStaging(_manager, bytes);
    if (!staging)
    {
        cancelReservation(destination, _id, *reserved);
        return std::unexpected {std::move(staging.error())};
    }
    _writes.emplace_back(TextureUpload {.staging = std::move(*staging),
                                        .destination = destination,
                                        .region = region,
                                        .bufferRowLength = *rowLength,
                                        .firstForTexture = *reserved});
    return {};
}

auto UploadBatchImpl::hasTexture(const std::shared_ptr<TextureResource>& texture) const -> bool
{
    return std::ranges::any_of(_writes, [&](const UploadOperation& operation) {
        return std::visit(
            [&](const auto& upload) {
                return refersTo(upload, texture);
            },
            operation);
    });
}

auto UploadBatchImpl::submit() -> Result<CompletionPoint>
{
    auto point = _manager.submit(_writes);
    if (point)
    {
        commitUploadReservations(_writes, _id);
        _writes.clear();
    }
    return point;
}
}
