#pragma once

/// @file
/// Owned staging for asynchronous buffer uploads.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <panda/Buffer.hpp>
#include <panda/CompletionPoint.hpp>
#include <panda/Error.hpp>
#include <panda/Texture.hpp>
#include <span>

namespace panda
{
namespace detail
{
class UploadBatchImpl;
}

/// @brief Destination rectangle and source row layout for a texture upload.
struct TextureUploadRegion
{
    /// Destination offset in texels; must be nonnegative and block-aligned.
    vk::Offset2D offset {};
    /// Nonempty copied extent in texels; compressed extents may end at an image edge.
    vk::Extent2D extent {};
    /// Byte stride between source block rows; zero means tightly packed rows.
    std::uint64_t rowPitchBytes {0};
};

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
    /// @brief Snapshots a full-texture upload into owned staging.
    /// @param destination Texture from the same Context with transfer-destination and sampled usage.
    /// @param bytes Source texels or compressed blocks, with rows separated by rowPitchBytes.
    /// @param rowPitchBytes Source byte stride between block rows; zero means tightly packed.
    /// @return Success or a validation/allocation/cache error. Failed writes leave earlier writes intact.
    /// A texture can be uploaded only once; multiple region writes to that texture are allowed in this batch.
    [[nodiscard]] auto write(const Texture& destination,
                             std::span<const std::byte> bytes,
                             std::uint64_t rowPitchBytes = 0) -> Result<void>;
    /// @brief Snapshots one texture region into owned staging.
    /// @param destination Texture from the same Context with transfer-destination and sampled usage.
    /// @param bytes Source begins at the first copied texel or block; each block row uses rowPitchBytes.
    /// @param region Destination rectangle and source row stride in bytes.
    /// @return Success or a validation/allocation/cache error. Failed writes leave earlier writes intact.
    /// Multiple calls for one texture must use the same batch; regions are copied in insertion order.
    [[nodiscard]] auto write(const Texture& destination,
                             std::span<const std::byte> bytes,
                             const TextureUploadRegion& region) -> Result<void>;
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
