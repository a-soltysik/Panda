#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <panda/CompletionPoint.hpp>
#include <panda/Error.hpp>
#include <span>
#include <vector>

namespace panda::detail
{
class BufferResource;
class UploadManager;

struct BufferUpload
{
    std::shared_ptr<BufferResource> staging;
    std::shared_ptr<BufferResource> destination;
    std::uint64_t offset;
    std::uint64_t size;
};

// Unsubmitted writes retain both sides. A successful submit copies these owners
// into the manager's pending packet before this state can release them.
class UploadBatchImpl final
{
public:
    explicit UploadBatchImpl(UploadManager& manager);
    ~UploadBatchImpl();
    UploadBatchImpl(const UploadBatchImpl&) = delete;
    auto operator=(const UploadBatchImpl&) -> UploadBatchImpl& = delete;
    UploadBatchImpl(UploadBatchImpl&&) = delete;
    auto operator=(UploadBatchImpl&&) -> UploadBatchImpl& = delete;
    [[nodiscard]] auto write(const std::shared_ptr<BufferResource>& destination,
                             std::uint64_t offset,
                             std::span<const std::byte> bytes) -> Result<void>;
    [[nodiscard]] auto submit() -> Result<CompletionPoint>;

private:
    UploadManager& _manager;
    std::vector<BufferUpload> _writes;
};
}
