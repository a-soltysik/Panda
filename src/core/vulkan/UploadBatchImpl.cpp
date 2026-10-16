// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
// clang-format on

#include "UploadBatchImpl.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
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
constexpr auto stagingPageSize = std::uint64_t {4} * 1024;

auto alignOffset(std::uint64_t offset, std::uint64_t alignment) -> std::optional<std::uint64_t>
{
    const auto remainder = offset % alignment;
    const auto padding = remainder == 0 ? 0 : alignment - remainder;
    if (padding > std::numeric_limits<std::uint64_t>::max() - offset)
    {
        return std::nullopt;
    }
    return offset + padding;
}

auto fitsPageRange(std::uint64_t offset, std::size_t size, std::uint64_t pageSize) -> bool
{
    return offset <= pageSize && size <= pageSize - offset;
}

auto validateStageRequest(std::span<const std::byte> bytes, std::uint64_t alignment) -> Result<void>
{
    if (bytes.empty() || alignment == 0)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Staging writes must be nonempty and aligned")};
    }
    return {};
}

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

auto UploadBatchImpl::stage(std::span<const std::byte> bytes, std::uint64_t alignment) -> Result<StagingSlice>
{
    const auto valid = validateStageRequest(bytes, alignment);
    if (!valid)
    {
        return std::unexpected {valid.error()};
    }
    if (const auto collected = _manager.collect(); !collected)
    {
        return std::unexpected {collected.error()};
    }
    auto current = stageInCurrentPage(bytes, alignment);
    if (!current)
    {
        return std::unexpected {current.error()};
    }
    if (*current)
    {
        return std::move(**current);
    }
    return createStagingPage(bytes);
}

auto UploadBatchImpl::stageInCurrentPage(std::span<const std::byte> bytes, std::uint64_t alignment)
    -> Result<std::optional<StagingSlice>>
{
    if (_stagingPages.empty())
    {
        return std::optional<StagingSlice> {};
    }
    const auto offset = alignOffset(_stagingOffset, alignment);
    if (!offset)
    {
        return std::optional<StagingSlice> {};
    }
    const auto& page = _stagingPages.back();
    if (!fitsPageRange(*offset, bytes.size(), page->getSize()))
    {
        return std::optional<StagingSlice> {};
    }
    const auto copied = page->writeMapped(*offset, bytes);
    if (!copied)
    {
        return std::unexpected {copied.error()};
    }
    _stagingOffset = *offset + bytes.size();
    return std::optional<StagingSlice> {
        StagingSlice {.buffer = page, .offset = *offset}
    };
}

auto UploadBatchImpl::createStagingPage(std::span<const std::byte> bytes) -> Result<StagingSlice>
{
    const auto pageSize = std::max<std::uint64_t>(stagingPageSize, bytes.size());
    auto page = _manager.createStaging(pageSize);
    if (!page)
    {
        return std::unexpected {page.error()};
    }
    const auto copied = (*page)->writeMapped(0, bytes);
    if (!copied)
    {
        return std::unexpected {copied.error()};
    }
    _stagingPages.push_back(*page);
    _stagingOffset = bytes.size();
    return StagingSlice {.buffer = std::move(*page), .offset = 0};
}

auto UploadBatchImpl::createBufferUpload(const std::shared_ptr<BufferResource>& destination,
                                         std::span<const std::byte> bytes) -> Result<BufferUpload>
{
    auto staging = stage(bytes, 1);
    if (!staging)
    {
        return std::unexpected {staging.error()};
    }
    return BufferUpload {.staging = std::move(staging->buffer),
                         .stagingOffset = staging->offset,
                         .destination = destination,
                         .offset = 0,
                         .size = bytes.size()};
}

auto UploadBatchImpl::createMeshUploads(const std::shared_ptr<MeshResource>& destination,
                                        std::span<const std::byte> vertexBytes,
                                        std::span<const std::byte> indexBytes) -> Result<std::vector<UploadOperation>>
{
    const auto valid = validateMeshWrite(_manager, destination, vertexBytes, indexBytes);
    if (!valid)
    {
        return std::unexpected {valid.error()};
    }
    auto writes = std::vector<UploadOperation> {};
    writes.reserve(destination->getIndexBuffer() ? 2 : 1);
    auto vertexUpload = createBufferUpload(destination->getVertexBuffer(), vertexBytes);
    if (!vertexUpload)
    {
        return std::unexpected {vertexUpload.error()};
    }
    writes.emplace_back(std::move(*vertexUpload));
    const auto& indexBuffer = destination->getIndexBuffer();
    if (indexBuffer)
    {
        auto indexUpload = createBufferUpload(indexBuffer, indexBytes);
        if (!indexUpload)
        {
            return std::unexpected {indexUpload.error()};
        }
        writes.emplace_back(std::move(*indexUpload));
    }
    return writes;
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
    auto upload = createBufferUpload(destination, bytes);
    if (!upload)
    {
        return std::unexpected {upload.error()};
    }
    upload->offset = offset;
    _writes.emplace_back(std::move(*upload));
    return {};
}

auto UploadBatchImpl::write(const std::shared_ptr<MeshResource>& destination,
                            std::span<const std::byte> vertexBytes,
                            std::span<const std::byte> indexBytes) -> Result<void>
{
    auto uploads = createMeshUploads(destination, vertexBytes, indexBytes);
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
    const auto layout = _manager.validateTextureWrite(destination, region, bytes.size());
    if (!layout)
    {
        return std::unexpected {layout.error()};
    }
    const auto reserved = reserveTexture(destination, _id, hasTexture(destination));
    if (!reserved)
    {
        return std::unexpected {reserved.error()};
    }
    auto staging = stage(bytes, layout->bufferOffsetAlignment);
    if (!staging)
    {
        cancelReservation(destination, _id, *reserved);
        return std::unexpected {staging.error()};
    }
    _writes.emplace_back(TextureUpload {.staging = std::move(staging->buffer),
                                        .stagingOffset = staging->offset,
                                        .destination = destination,
                                        .region = region,
                                        .bufferRowLength = layout->rowLength,
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
    auto point = _manager.submit(_writes, _stagingPages);
    if (point)
    {
        commitUploadReservations(_writes, _id);
        _writes.clear();
        _stagingPages.clear();
        _stagingOffset = 0;
    }
    return point;
}
}
