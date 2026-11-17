// clang-format off
#include "vulkan/VulkanHpp.hpp" // IWYU pragma: keep
// clang-format on

#include <chrono>
#include <cstdint>
#include <expected>
#include <panda/base/Error.hpp>
#include <panda/submission/CompletionPoint.hpp>
#include <panda/submission/SubmissionManager.hpp>
#include <panda/submission/UploadBatch.hpp>

#include "vulkan/SubmissionQueue.hpp"
#include "vulkan/upload/UploadBatchImpl.hpp"
#include "vulkan/upload/UploadManager.hpp"

namespace panda
{
SubmissionManager::SubmissionManager(detail::SubmissionQueue& submissions,
                                     core::vulkan::upload::UploadManager& uploads) noexcept
    : _submissions {submissions},
      _uploads {uploads}
{
}

auto SubmissionManager::createUploadBatch() const -> Result<UploadBatch>
{
    return UploadBatch {_uploads.createBatch()};
}

auto SubmissionManager::poll(CompletionPoint point) const -> Result<bool>
{
    return _submissions.poll(point).transform([this](bool completed) {
        _uploads.collectCompleted();
        return completed;
    });
}

auto SubmissionManager::wait(CompletionPoint point, std::chrono::nanoseconds timeout) const -> Result<WaitStatus>
{
    if (timeout.count() < 0)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Invalid completion wait timeout")};
    }
    return _submissions.wait(point, static_cast<std::uint64_t>(timeout.count())).transform([this](WaitStatus status) {
        _uploads.collectCompleted();
        return status;
    });
}
}
