#pragma once

/// @file
/// Context-owned upload submission and completion operations.

#include <chrono>
#include <panda/CompletionPoint.hpp>
#include <panda/Error.hpp>
#include <panda/UploadBatch.hpp>

namespace panda
{
class Context;

/// @brief Creates upload batches and observes submitted work for one Context.
/// This facade is borrowed from Context and must not outlive it. Calls are serialized
/// with other operations on the owning Context.
class SubmissionManager final
{
public:
    SubmissionManager(const SubmissionManager&) = delete;
    auto operator=(const SubmissionManager&) -> SubmissionManager& = delete;
    SubmissionManager(SubmissionManager&&) = delete;
    auto operator=(SubmissionManager&&) -> SubmissionManager& = delete;
    ~SubmissionManager() = default;

    /// @brief Creates an empty batch borrowing this Context; may allocate.
    /// @return A batch or an initialization/backend error. Submits no work and performs no completion wait.
    /// Unsubmitted batches must be destroyed before Context; successful submit consumes the batch.
    [[nodiscard]] auto createUploadBatch() const -> Result<UploadBatch>;

    /// @brief Queries GPU completion without waiting; does not indicate display/presentation completion.
    /// @return Completion state or a backend error; empty/foreign tokens are InvalidArgument.
    [[nodiscard]] auto poll(CompletionPoint point) const -> Result<bool>;

    /// @brief Explicitly waits for GPU work; timeout is in nanoseconds and must be nonnegative.
    /// @return Completed, Timeout, or a backend error; rejects empty/foreign tokens.
    [[nodiscard]] auto wait(CompletionPoint point, std::chrono::nanoseconds timeout) const -> Result<WaitStatus>;

private:
    friend class Context;

    explicit SubmissionManager(const Context& context) noexcept;

    const Context* _context;
};
}
