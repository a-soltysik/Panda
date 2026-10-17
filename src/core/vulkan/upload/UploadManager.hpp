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

#include "upload/UploadBatchId.hpp"
#include "upload/UploadOperation.hpp"

namespace panda::core::vulkan::resources
{
class MemoryPool;
}

namespace panda::core::vulkan::upload
{
class UploadCommand;
namespace resources = ::panda::core::vulkan::resources;

struct UploadStats
{
    std::size_t pendingBatches;
    std::uint64_t pendingStagingBytes;
    std::uint64_t peakStagingBytes;
};

struct TextureUploadLayout
{
    std::uint32_t rowLength;
    std::uint64_t bufferOffsetAlignment;
};

// Context owns this service and outlives its unsubmitted batches. Pending packets
// own all GPU uses; no allocation or fallible bookkeeping occurs after submit succeeds.
class UploadManager final
{
public:
    UploadManager(const vk::raii::Device& device,
                  resources::MemoryPool& pool,
                  api_detail::SubmissionQueue& submissions,
                  std::uint32_t family);
    ~UploadManager();
    UploadManager(const UploadManager&) = delete;
    auto operator=(const UploadManager&) -> UploadManager& = delete;
    UploadManager(UploadManager&&) = delete;
    auto operator=(UploadManager&&) -> UploadManager& = delete;
    [[nodiscard]] auto createBatch() -> std::unique_ptr<api_detail::UploadBatchImpl>;
    [[nodiscard]] auto hasActiveBatches() const -> bool;
    [[nodiscard]] auto getStats() const -> UploadStats;
    [[nodiscard]] auto collect() -> Result<void>;
    void collectCompleted();
    [[nodiscard]] auto drain(std::uint64_t timeout) -> Result<void>;
    [[nodiscard]] auto validateWrite(const std::shared_ptr<api_detail::BufferResource>& destination,
                                     std::uint64_t offset,
                                     std::size_t size) const -> Result<void>;
    [[nodiscard]] auto validateTextureWrite(const std::shared_ptr<api_detail::TextureResource>& destination,
                                            const TextureUploadRegion& region,
                                            std::size_t size) const -> Result<TextureUploadLayout>;
    [[nodiscard]] auto createStaging(std::uint64_t size) const -> Result<std::shared_ptr<api_detail::BufferResource>>;
    [[nodiscard]] auto submit(std::span<const UploadOperation> writes,
                              std::span<const std::shared_ptr<api_detail::BufferResource>> stagingPages = {})
        -> Result<CompletionPoint>;
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
    resources::MemoryPool& _pool;
    api_detail::SubmissionQueue& _submissions;
    std::uint32_t _family;
    std::list<PendingUpload> _pending;
    std::size_t _activeBatches {0};
    std::uint64_t _nextBatchId {1};
    std::uint64_t _pendingStagingBytes {0};
    std::uint64_t _peakStagingBytes {0};
};
}
