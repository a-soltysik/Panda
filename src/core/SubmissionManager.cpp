#include <chrono>
#include <panda/CompletionPoint.hpp>
#include <panda/Engine.hpp>
#include <panda/Error.hpp>
#include <panda/SubmissionManager.hpp>
#include <panda/UploadBatch.hpp>

namespace panda
{
SubmissionManager::SubmissionManager(const Engine& context) noexcept
    : _engine {&context}
{
}

auto SubmissionManager::createUploadBatch() const -> Result<UploadBatch>
{
    return _engine->createUploadBatch();
}

auto SubmissionManager::poll(CompletionPoint point) const -> Result<bool>
{
    return _engine->poll(point);
}

auto SubmissionManager::wait(CompletionPoint point, std::chrono::nanoseconds timeout) const -> Result<WaitStatus>
{
    return _engine->wait(point, timeout);
}
}
