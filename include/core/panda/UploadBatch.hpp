#pragma once

/// @file
/// Owned staging for asynchronous buffer uploads.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <panda/Buffer.hpp>
#include <panda/CompletionPoint.hpp>
#include <panda/Error.hpp>
#include <span>

namespace panda
{
namespace detail
{
class UploadBatchImpl;
}

/// @brief Move-only batch borrowing its Context until submitted or destroyed.
/// Calls and destruction are serialized with Context. Moving Context preserves
/// the batch. Destruction before submit cancels its writes without GPU work.
class UploadBatch final
{
public:
    /// @brief Creates an empty handle; write/submit reject empty handles.
    UploadBatch();
    UploadBatch(const UploadBatch&) = delete;
    auto operator=(const UploadBatch&) -> UploadBatch& = delete;
    /// @brief Transfers the batch and leaves other empty.
    UploadBatch(UploadBatch&& other) noexcept;
    /// @brief Cancels this unsubmitted batch and transfers other's batch.
    auto operator=(UploadBatch&& other) noexcept -> UploadBatch&;
    /// @brief Releases unsubmitted staging and retained destinations; does not wait.
    ~UploadBatch();
    /// @brief Reports whether write/submit can be attempted.
    [[nodiscard]] auto isValid() const noexcept -> bool;
    /// @brief Copies bytes into owned staging before returning; may allocate.
    /// @param destination Buffer from the same Context with transfer-destination usage.
    /// @param offset Destination offset in bytes.
    /// @param bytes Nonempty caller data; its lifetime may end after this call.
    /// @return Success or a validation/allocation/cache error. Failed writes leave earlier writes intact.
    /// Writes are applied in insertion order, including overlapping destination ranges.
    [[nodiscard]] auto write(const Buffer& destination, std::uint64_t offset, std::span<const std::byte> bytes)
        -> Result<void>;
    /// @brief Submits all writes on Context's graphics/compute queue without waiting for completion.
    /// @return A committed point or a preparation/submission error; an empty batch is InvalidArgument.
    /// Success consumes this handle. Context retains command resources, staging and
    /// destinations until completion. Recoverable failure leaves the batch available to retry.
    [[nodiscard]] auto submit() -> Result<CompletionPoint>;

private:
    friend class Context;
    explicit UploadBatch(std::unique_ptr<detail::UploadBatchImpl> impl);
    std::unique_ptr<detail::UploadBatchImpl> _impl;
};
}
