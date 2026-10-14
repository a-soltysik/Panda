// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include "UploadManager.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <memory>
#include <panda/Assert.hpp>
#include <panda/Buffer.hpp>
#include <panda/CompletionPoint.hpp>
#include <panda/Error.hpp>
#include <span>
#include <utility>

#include "SubmissionQueue.hpp"
#include "UploadBatchImpl.hpp"
#include "UploadCommand.hpp"
#include "VulkanError.hpp"
#include "resources/BufferResource.hpp"
#include "resources/MemoryPool.hpp"

namespace panda::detail
{
UploadManager::UploadManager(const vk::raii::Device& device,
                             MemoryPool& pool,
                             SubmissionQueue& submissions,
                             std::uint32_t family)
    : _device {device},
      _pool {pool},
      _submissions {submissions},
      _family {family}
{
}

UploadManager::~UploadManager()
{
    expect(_activeBatches == 0, "Upload manager destroyed with live batches");
    expect(_pending.empty(), "Upload manager destroyed with pending GPU uses");
}

auto UploadManager::createBatch() -> std::unique_ptr<UploadBatchImpl>
{
    return std::make_unique<UploadBatchImpl>(*this);
}

auto UploadManager::hasActiveBatches() const -> bool
{
    return _activeBatches != 0;
}

void UploadManager::retainBatch()
{
    ++_activeBatches;
}

void UploadManager::releaseBatch()
{
    expect(_activeBatches != 0, "Upload batch ownership was lost");
    --_activeBatches;
}

auto UploadManager::getStats() const -> UploadStats
{
    return {.pendingBatches = _pending.size(),
            .pendingStagingBytes = _pendingStagingBytes,
            .peakStagingBytes = _peakStagingBytes};
}

void UploadManager::collectCompleted()
{
    while (!_pending.empty() && _pending.front().completion <= _submissions.getCompletedValue())
    {
        _pendingStagingBytes -= _pending.front().stagingBytes;
        _pending.pop_front();
    }
}

auto UploadManager::collect() -> Result<void>
{
    if (_submissions.isDeviceLost())
    {
        return std::unexpected {makeVulkanError(vk::Result::eErrorDeviceLost)};
    }
    if (_pending.empty())
    {
        return {};
    }
    return _submissions.poll(_submissions.getPoint(_pending.back().completion)).transform([&](bool) {
        collectCompleted();
    });
}

auto UploadManager::drain(std::uint64_t timeout) -> Result<void>
{
    if (_pending.empty())
    {
        return {};
    }
    return _submissions.waitValue(_pending.back().completion, timeout).and_then([&](WaitStatus status) -> Result<void> {
        if (status == WaitStatus::Timeout)
        {
            return std::unexpected {makeError(ErrorCode::Timeout, "Upload completion wait timed out")};
        }
        collectCompleted();
        return {};
    });
}

auto UploadManager::validateWrite(const std::shared_ptr<BufferResource>& destination,
                                  std::uint64_t offset,
                                  std::size_t size) const -> Result<void>
{
    if (!destination || !destination->belongsTo(_pool) || size == 0)
    {
        return std::unexpected {
            makeError(ErrorCode::InvalidArgument, "Upload needs a nonempty buffer and bytes from this Context")};
    }
    if (!(destination->getUsage() & vk::BufferUsageFlagBits::eTransferDst) || offset > destination->getSize() ||
        size > destination->getSize() - offset)
    {
        return std::unexpected {
            makeError(ErrorCode::InvalidArgument, "Upload exceeds buffer size or requires transfer-destination usage")};
    }
    return {};
}

auto UploadManager::createStaging(std::span<const std::byte> bytes) const -> Result<std::shared_ptr<BufferResource>>
{
    const auto description = BufferDesc {.size = bytes.size(),
                                         .usage = vk::BufferUsageFlagBits::eTransferSrc,
                                         .memory = BufferMemory::Upload};
    auto staging = BufferResource::create(_device, _pool, description);
    if (!staging)
    {
        return std::unexpected {std::move(staging.error())};
    }
    const auto copied = (*staging)->writeMapped(bytes);
    if (!copied)
    {
        return std::unexpected {copied.error()};
    }
    return staging;
}

auto UploadManager::submit(std::span<const BufferUpload> writes) -> Result<CompletionPoint>
{
    if (writes.empty())
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Cannot submit an upload batch without writes")};
    }
    if (const auto collected = collect(); !collected)
    {
        return std::unexpected {collected.error()};
    }
    if (const auto ready = _submissions.prepareSubmit(); !ready)
    {
        return std::unexpected {ready.error()};
    }
    auto packet = UploadCommand::create(_device, _family, writes);
    if (!packet)
    {
        return std::unexpected {std::move(packet.error())};
    }
    return commit(std::move(*packet));
}

auto UploadManager::commit(std::unique_ptr<UploadCommand> packet) -> Result<CompletionPoint>
{
    const auto stagingBytes = packet->getStagingSize();
    if (stagingBytes > std::numeric_limits<std::uint64_t>::max() - _pendingStagingBytes)
    {
        return std::unexpected {makeError(ErrorCode::ResourceExhausted, "Pending staging accounting exhausted")};
    }
    _pending.push_back(PendingUpload {.completion = 0, .stagingBytes = stagingBytes, .packet = std::move(packet)});
    const auto submitted = _submissions.submit(_pending.back().packet->getCommand(), {}, {});
    if (!submitted)
    {
        _pending.pop_back();
        return std::unexpected {submitted.error()};
    }
    _pending.back().completion = *submitted;
    _pendingStagingBytes += stagingBytes;
    _peakStagingBytes = std::max(_peakStagingBytes, _pendingStagingBytes);
    return _submissions.getPoint(*submitted);
}
}
