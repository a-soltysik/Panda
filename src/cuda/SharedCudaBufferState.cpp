#include "SharedCudaBufferState.hpp"

#include <cstdint>
#include <expected>
#include <memory>
#include <panda/Buffer.hpp>
#include <panda/Error.hpp>
#include <panda/ExternalBuffer.hpp>
#include <utility>

#include "CudaAdapter.hpp"
#include "CudaInterop.hpp"
#include "CudaSessionState.hpp"

namespace panda::cuda::detail
{
SharedCudaBufferState::SharedCudaBufferState(panda::ExternalBuffer buffer, std::shared_ptr<CudaSessionState> owner)
    : _externalBuffer {std::move(buffer)},
      _session {std::move(owner)}
{
}

SharedCudaBufferState::~SharedCudaBufferState()
{
    if (_imported.imported)
    {
        reportCleanupFailure(synchronizeInteropSession(_session->getNativeSession()),
                             "CUDA interop stream synchronization failed");
    }
    if (_importedSemaphore)
    {
        reportCleanupFailure(destroyExternalSemaphore(_session->getNativeSession(), *_importedSemaphore),
                             "CUDA external semaphore destruction failed");
    }
    if (_imported.imported)
    {
        reportCleanupFailure(destroyExternalBuffer(_session->getNativeSession(), _imported),
                             "CUDA external buffer destruction failed");
    }
}

auto SharedCudaBufferState::getBuffer() const -> panda::Buffer
{
    return _externalBuffer.getBuffer();
}

auto SharedCudaBufferState::getSize() const noexcept -> std::uint64_t
{
    return _externalBuffer.getSize();
}

auto SharedCudaBufferState::waitForVulkan(std::uint64_t value) -> Result<void>
{
    if (!_importedSemaphore || _importedSemaphore->externalSemaphore == nullptr)
    {
        return std::unexpected {panda::makeError(ErrorCode::Unsupported, "No imported CUDA semaphore")};
    }
    const auto result =
        enqueueExternalSemaphoreWait(_session->getNativeSession(), _importedSemaphore->externalSemaphore, value);
    if (result.status != NativeInteropStatus::Success)
    {
        return std::unexpected {
            interopError(result.status, result.nativeCode, "Could not enqueue a CUDA wait for Vulkan")};
    }
    return {};
}

auto SharedCudaBufferState::signalToVulkan(std::uint64_t value) -> Result<void>
{
    if (!_importedSemaphore || _importedSemaphore->externalSemaphore == nullptr)
    {
        return std::unexpected {panda::makeError(ErrorCode::Unsupported, "No imported CUDA semaphore")};
    }
    const auto result =
        enqueueExternalSemaphoreSignal(_session->getNativeSession(), _importedSemaphore->externalSemaphore, value);
    if (result.status != NativeInteropStatus::Success)
    {
        return std::unexpected {
            interopError(result.status, result.nativeCode, "Could not enqueue a CUDA signal for Vulkan")};
    }
    return {};
}

auto SharedCudaBufferState::import() -> Result<void>
{
    const auto imported = importNativeBuffer(_session->getNativeSession(), _externalBuffer);
    if (!imported)
    {
        return std::unexpected {imported.error()};
    }
    _imported = *imported;

    auto importedSemaphore = importNativeSemaphore(_session->getNativeSession(), _externalBuffer.getSemaphoreHandle());
    if (!importedSemaphore)
    {
        return std::unexpected {importedSemaphore.error()};
    }
#ifdef __linux__
    if (importedSemaphore->has_value())
    {
        static_cast<void>(_externalBuffer.getSemaphoreHandle().release());
    }
#endif
    _importedSemaphore = *importedSemaphore;
    return {};
}
}
