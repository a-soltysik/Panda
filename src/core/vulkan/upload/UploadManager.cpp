// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include "upload/UploadManager.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <memory>
#include <numeric>
#include <panda/Assert.hpp>
#include <panda/Buffer.hpp>
#include <panda/CompletionPoint.hpp>
#include <panda/Error.hpp>
#include <panda/UploadBatch.hpp>
#include <span>
#include <utility>
#include <vulkan/vulkan_format_traits.hpp>

#include "SubmissionQueue.hpp"
#include "resources/BufferResource.hpp"
#include "resources/TextureResource.hpp"
#include "resources/memory/MemoryPool.hpp"
#include "upload/UploadBatchImpl.hpp"
#include "upload/UploadCommand.hpp"
#include "upload/UploadOperation.hpp"

namespace panda::core::vulkan::upload
{
using api_detail::BufferResource;
using api_detail::SubmissionQueue;
using api_detail::TextureResource;
using api_detail::UploadBatchImpl;
using resources::MemoryPool;

namespace
{
struct TextureBlock
{
    std::uint64_t size;
    std::uint32_t width;
    std::uint32_t height;
    std::uint64_t bufferOffsetAlignment;
};

struct TextureCopyLayout
{
    std::uint32_t rowLength;
    std::uint64_t rowBytes;
    std::uint64_t rowPitch;
    std::uint64_t blockRows;
    std::uint64_t bufferOffsetAlignment;
};

auto getTextureBlock(vk::Format format) -> Result<TextureBlock>
{
    const auto [width, height, depth] = vk::blockExtent(format);
    const auto size = static_cast<std::uint64_t>(vk::blockSize(format));
    if (!vk::isColor(format) || vk::planeCount(format) != 1 || size == 0 || width == 0 || height == 0 || depth != 1)
    {
        return std::unexpected {
            makeError(ErrorCode::Unsupported, "Texture upload requires a single-plane 2D color format")};
    }
    return TextureBlock {.size = size,
                         .width = width,
                         .height = height,
                         .bufferOffsetAlignment = std::lcm(std::uint64_t {4}, size)};
}

auto validateTextureRegion(vk::Extent2D textureExtent, const TextureUploadRegion& region, const TextureBlock& block)
    -> Result<void>
{
    if (region.extent.width == 0 || region.extent.height == 0 || region.offset.x < 0 || region.offset.y < 0)
    {
        return std::unexpected {
            makeError(ErrorCode::InvalidArgument, "Texture upload region must be nonempty and nonnegative")};
    }
    const auto x = static_cast<std::uint64_t>(region.offset.x);
    const auto y = static_cast<std::uint64_t>(region.offset.y);
    if (x > textureExtent.width || y > textureExtent.height || region.extent.width > textureExtent.width - x ||
        region.extent.height > textureExtent.height - y)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Texture upload region exceeds image bounds")};
    }
    const auto reachesRightEdge = x + region.extent.width == textureExtent.width;
    const auto reachesBottomEdge = y + region.extent.height == textureExtent.height;
    if (x % block.width != 0 || y % block.height != 0 ||
        (region.extent.width % block.width != 0 && !reachesRightEdge) ||
        (region.extent.height % block.height != 0 && !reachesBottomEdge))
    {
        return std::unexpected {
            makeError(ErrorCode::InvalidArgument, "Texture upload region is not aligned to format blocks")};
    }
    return {};
}

auto makeTextureCopyLayout(const TextureBlock& block, const TextureUploadRegion& region) -> Result<TextureCopyLayout>
{
    const auto blockColumns = (static_cast<std::uint64_t>(region.extent.width) + block.width - 1) / block.width;
    const auto blockRows = (static_cast<std::uint64_t>(region.extent.height) + block.height - 1) / block.height;
    const auto rowBytes = blockColumns * block.size;
    const auto rowPitch = region.rowPitchBytes == 0 ? rowBytes : region.rowPitchBytes;
    if (rowPitch < rowBytes || rowPitch % block.size != 0)
    {
        return std::unexpected {
            makeError(ErrorCode::InvalidArgument, "Texture source row pitch is too small or misaligned")};
    }
    const auto blocksPerRow = rowPitch / block.size;
    const auto maxBlocksPerRow = std::numeric_limits<std::uint32_t>::max() / block.width;
    if (blocksPerRow > maxBlocksPerRow)
    {
        return std::unexpected {
            makeError(ErrorCode::InvalidArgument, "Texture source row pitch overflows Vulkan limits")};
    }
    const auto rowLength = blocksPerRow * block.width;
    return TextureCopyLayout {.rowLength = region.rowPitchBytes == 0 ? 0U : static_cast<std::uint32_t>(rowLength),
                              .rowBytes = rowBytes,
                              .rowPitch = rowPitch,
                              .blockRows = blockRows,
                              .bufferOffsetAlignment = block.bufferOffsetAlignment};
}

auto validateTextureSourceSize(const TextureCopyLayout& layout, std::size_t size) -> Result<void>
{
    const auto maxSize = std::numeric_limits<std::uint64_t>::max();
    if (layout.blockRows > 1 && layout.rowPitch > (maxSize - layout.rowBytes) / (layout.blockRows - 1))
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Texture source data size overflows")};
    }
    const auto requiredBytes = ((layout.blockRows - 1) * layout.rowPitch) + layout.rowBytes;
    if (requiredBytes > size)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Texture upload source data is too small")};
    }
    return {};
}

auto validateTextureCopy(vk::Format format,
                         vk::Extent2D textureExtent,
                         const TextureUploadRegion& region,
                         std::size_t size) -> Result<TextureCopyLayout>
{
    const auto block = getTextureBlock(format);
    if (!block)
    {
        return std::unexpected {block.error()};
    }
    const auto validRegion = validateTextureRegion(textureExtent, region, *block);
    if (!validRegion)
    {
        return std::unexpected {validRegion.error()};
    }
    auto layout = makeTextureCopyLayout(*block, region);
    if (!layout)
    {
        return std::unexpected {layout.error()};
    }
    const auto validSize = validateTextureSourceSize(*layout, size);
    if (!validSize)
    {
        return std::unexpected {validSize.error()};
    }
    return layout;
}
}

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
    expect(_nextBatchId != std::numeric_limits<std::uint64_t>::max(), "Upload batch identities exhausted");
    const auto id = UploadBatchId {_nextBatchId++};
    return std::make_unique<UploadBatchImpl>(*this, id);
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

auto UploadManager::validateTextureWrite(const std::shared_ptr<TextureResource>& destination,
                                         const TextureUploadRegion& region,
                                         std::size_t size) const -> Result<TextureUploadLayout>
{
    if (!destination || !destination->belongsTo(_pool) || size == 0)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument,
                                          "Texture upload needs nonempty data and a texture from this Context")};
    }
    if (!(destination->getUsage() & vk::ImageUsageFlagBits::eTransferDst) ||
        !(destination->getUsage() & vk::ImageUsageFlagBits::eSampled))
    {
        return std::unexpected {
            makeError(ErrorCode::InvalidArgument, "Texture upload requires transfer-destination and sampled usage")};
    }
    auto layout = validateTextureCopy(destination->getFormat(), destination->getExtent(), region, size);
    if (!layout)
    {
        return std::unexpected {layout.error()};
    }
    return TextureUploadLayout {.rowLength = layout->rowLength, .bufferOffsetAlignment = layout->bufferOffsetAlignment};
}

auto UploadManager::createStaging(std::uint64_t size) const -> Result<std::shared_ptr<BufferResource>>
{
    const auto description =
        BufferDesc {.size = size, .usage = vk::BufferUsageFlagBits::eTransferSrc, .memory = BufferMemory::Upload};
    return BufferResource::create(_device, _pool, description);
}

auto UploadManager::submit(std::span<const UploadOperation> writes,
                           std::span<const std::shared_ptr<BufferResource>> stagingPages) -> Result<CompletionPoint>
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
    auto packet = UploadCommand::create(_device, _family, writes, stagingPages);
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
