// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
// clang-format on

#include "UploadBatchImpl.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <panda/CompletionPoint.hpp>
#include <panda/Error.hpp>
#include <panda/UploadBatch.hpp>
#include <span>
#include <utility>
#include <variant>

#include "UploadBatchId.hpp"
#include "UploadManager.hpp"
#include "UploadOperation.hpp"
#include "resources/BufferResource.hpp"
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
