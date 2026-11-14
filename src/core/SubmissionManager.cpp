#include <chrono>
#include <panda/CompletionPoint.hpp>
#include <panda/Context.hpp>
#include <panda/Error.hpp>
#include <panda/SubmissionManager.hpp>
#include <panda/UploadBatch.hpp>

namespace panda
{
SubmissionManager::SubmissionManager(const Context& context) noexcept
    : _context {&context}
{
}

auto SubmissionManager::createUploadBatch() const -> Result<UploadBatch>
{
    return _context->createUploadBatch();
}

auto SubmissionManager::poll(CompletionPoint point) const -> Result<bool>
{
    return _context->poll(point);
}

auto SubmissionManager::wait(CompletionPoint point, std::chrono::nanoseconds timeout) const -> Result<WaitStatus>
{
    return _context->wait(point, timeout);
}
}
