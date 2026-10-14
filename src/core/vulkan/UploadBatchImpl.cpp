// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
// clang-format on

#include "UploadBatchImpl.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <panda/CompletionPoint.hpp>
#include <panda/Error.hpp>
#include <span>
#include <utility>

#include "UploadManager.hpp"
#include "resources/BufferResource.hpp"

namespace panda::detail
{
UploadBatchImpl::UploadBatchImpl(UploadManager& manager)
    : _manager {manager}
{
    _manager.retainBatch();
}

UploadBatchImpl::~UploadBatchImpl()
{
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
    if (const auto collected = _manager.collect(); !collected)
    {
        return collected;
    }
    auto staging = _manager.createStaging(bytes);
    if (!staging)
    {
        return std::unexpected {std::move(staging.error())};
    }
    _writes.push_back(BufferUpload {.staging = std::move(*staging),
                                    .destination = destination,
                                    .offset = offset,
                                    .size = bytes.size()});
    return {};
}

auto UploadBatchImpl::submit() -> Result<CompletionPoint>
{
    auto point = _manager.submit(_writes);
    if (point)
    {
        _writes.clear();
    }
    return point;
}
}
