#pragma once

#include <driver_types.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <panda/Error.hpp>
#include <panda/ExternalTimelineValue.hpp>

namespace panda
{
class Buffer;
}

namespace panda::cuda
{
class CudaSession;

/// @brief Callable that enqueues CUDA work using a borrowed session stream and device buffer.
using CudaWorkCallback = std::function<panda::Result<void>(cudaStream_t, std::byte*, std::uint64_t)>;

/// @brief Result of queuing CUDA work and returning its buffer to Vulkan.
struct CudaWorkSubmission
{
    /// @brief Timeline value signaled after the callback's stream work.
    panda::ExternalTimelineValue signalValue;
    /// @brief Callback result or converted exception; the signal is queued even on failure.
    panda::Result<void> workResult;
};

namespace detail
{
class SharedCudaBufferState;
}

/// @brief Vulkan buffer with a retained CUDA memory import and mapped allocation.
/// Context outlives every copy. Available paired timeline semaphores are also
/// imported and retained.
class SharedCudaBuffer final
{
public:
    SharedCudaBuffer();
    /// @brief Shares the imported resource retained by another handle.
    /// @param other Handle whose resource state is shared.
    SharedCudaBuffer(const SharedCudaBuffer& other);
    /// @brief Shares the imported resource retained by another handle.
    /// @param other Handle whose resource state is shared.
    /// @return This handle.
    auto operator=(const SharedCudaBuffer& other) -> SharedCudaBuffer&;
    /// @brief Transfers the resource state and leaves the source empty.
    /// @param other Handle to move from.
    SharedCudaBuffer(SharedCudaBuffer&& other) noexcept;
    /// @brief Transfers the resource state and leaves the source empty.
    /// @param other Handle to move from.
    /// @return This handle.
    auto operator=(SharedCudaBuffer&& other) noexcept -> SharedCudaBuffer&;
    ~SharedCudaBuffer();

    /// @brief Returns an owning Vulkan view, or an empty handle for an empty object.
    [[nodiscard]] auto getBuffer() const -> panda::Buffer;
    /// @brief Returns the logical buffer size, or zero for an empty object.
    [[nodiscard]] auto getSize() const noexcept -> std::uint64_t;
    /// @brief Reports whether this handle retains a shared resource.
    [[nodiscard]] auto isValid() const noexcept -> bool;

    /// @brief Enqueues application work between a Vulkan release and a CUDA signal.
    /// @param vulkanSignalValue Value returned by ExternalBufferManager::releaseToExternal.
    /// @param work Callable that enqueues work on the supplied stream and device buffer.
    /// The stream and device pointer are borrowed for the callback; enqueue on that stream
    /// and do not retain the pointer beyond this buffer's lifetime. Serialize submissions
    /// for the same shared buffer and do not re-enter this operation from the callback.
    /// @return A submission with the next timeline value and callback result, or an error
    /// if validation, the CUDA wait or the CUDA signal fails. The signal is queued even
    /// when the callback reports an error, so Vulkan ownership must still be reacquired
    /// with the returned signal value before handling that error. Callback exceptions
    /// are converted to `workResult` errors after queuing the signal.
    [[nodiscard]] auto submitWork(panda::ExternalTimelineValue vulkanSignalValue, const CudaWorkCallback& work)
        -> panda::Result<CudaWorkSubmission>;

private:
    friend class CudaSession;
    explicit SharedCudaBuffer(std::shared_ptr<detail::SharedCudaBufferState> state);

    std::shared_ptr<detail::SharedCudaBufferState> _state;
};
}
