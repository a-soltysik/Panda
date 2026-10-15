#pragma once

// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include <cstddef>
#include <cstdint>
#include <list>
#include <memory>
#include <panda/CompletionPoint.hpp>
#include <panda/Error.hpp>
#include <panda/UploadBatch.hpp>
#include <span>

#include "UploadBatchId.hpp"
#include "UploadOperation.hpp"

namespace panda::detail
{
class BufferResource;
class MemoryPool;
class SubmissionQueue;
class TextureResource;
class UploadBatchImpl;
class UploadCommand;

struct UploadStats
{
    std::size_t pendingBatches;
    std::uint64_t pendingStagingBytes;
    std::uint64_t peakStagingBytes;
};

// Context owns this service and outlives its unsubmitted batches. Pending packets
// own all GPU uses; no allocation or fallible bookkeeping occurs after submit succeeds.
class UploadManager final
{
public:
    UploadManager(const vk::raii::Device& device, MemoryPool& pool, SubmissionQueue& submissions, std::uint32_t family);
    ~UploadManager();
    UploadManager(const UploadManager&) = delete;
    auto operator=(const UploadManager&) -> UploadManager& = delete;
    UploadManager(UploadManager&&) = delete;
    auto operator=(UploadManager&&) -> UploadManager& = delete;
    [[nodiscard]] auto createBatch() -> std::unique_ptr<UploadBatchImpl>;
    [[nodiscard]] auto hasActiveBatches() const -> bool;
    [[nodiscard]] auto getStats() const -> UploadStats;
    [[nodiscard]] auto collect() -> Result<void>;
    void collectCompleted();
    [[nodiscard]] auto drain(std::uint64_t timeout) -> Result<void>;
    [[nodiscard]] auto validateWrite(const std::shared_ptr<BufferResource>& destination,
                                     std::uint64_t offset,
                                     std::size_t size) const -> Result<void>;
    [[nodiscard]] auto validateTextureWrite(const std::shared_ptr<TextureResource>& destination,
                                            const TextureUploadRegion& region,
                                            std::size_t size) const -> Result<std::uint32_t>;
    [[nodiscard]] auto createStaging(std::span<const std::byte> bytes) const -> Result<std::shared_ptr<BufferResource>>;
    [[nodiscard]] auto submit(std::span<const UploadOperation> writes) -> Result<CompletionPoint>;
    void retainBatch();
    void releaseBatch();

private:
    [[nodiscard]] auto commit(std::unique_ptr<UploadCommand> packet) -> Result<CompletionPoint>;

    struct PendingUpload
    {
        std::uint64_t completion;
        std::uint64_t stagingBytes;
        std::unique_ptr<UploadCommand> packet;
    };

    const vk::raii::Device& _device;
    MemoryPool& _pool;
    SubmissionQueue& _submissions;
    std::uint32_t _family;
    std::list<PendingUpload> _pending;
    std::size_t _activeBatches {0};
    std::uint64_t _nextBatchId {1};
    std::uint64_t _pendingStagingBytes {0};
    std::uint64_t _peakStagingBytes {0};
};
}
