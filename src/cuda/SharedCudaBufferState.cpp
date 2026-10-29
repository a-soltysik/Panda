#include "SharedCudaBufferState.hpp"

#include <driver_types.h>

#include <cstddef>
#include <cstdint>
#include <exception>
#include <expected>
#include <limits>
#include <memory>
#include <panda/Buffer.hpp>
#include <panda/Error.hpp>
#include <panda/ExternalBuffer.hpp>
#include <panda/cuda/SharedCudaBuffer.hpp>
#include <string>
#include <utility>

#include "CudaAdapter.hpp"
#include "CudaInterop.hpp"
#include "CudaSessionState.hpp"

namespace panda::cuda::detail
{
namespace
{
auto enqueueWork(const CudaWorkCallback& work, cudaStream_t stream, std::byte* deviceBuffer, std::uint64_t size)
    -> Result<void>
{
    try
    {
        return work(stream, deviceBuffer, size);
    }
    catch (const std::exception& exception)
    {
        return std::unexpected {
            panda::makeError(ErrorCode::BackendFailure, std::string {"CUDA work callback threw: "} + exception.what())};
    }
    catch (...)
    {
        return std::unexpected {
            panda::makeError(ErrorCode::BackendFailure, "CUDA work callback threw an unknown exception")};
    }
}
}

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

auto SharedCudaBufferState::validateSubmission(std::uint64_t vulkanSignalValue, const CudaWorkCallback& work) const
    -> Result<void>
{
    if (!work || vulkanSignalValue == 0 || vulkanSignalValue == std::numeric_limits<std::uint64_t>::max())
    {
        return std::unexpected {panda::makeError(ErrorCode::InvalidArgument, "Invalid CUDA work submission")};
    }
    if (_submissionFaulted)
    {
        return std::unexpected {panda::makeError(ErrorCode::BackendFailure, "CUDA buffer handoff is faulted")};
    }
    if (_lastSignalValue == std::numeric_limits<std::uint64_t>::max())
    {
        return std::unexpected {panda::makeError(ErrorCode::ResourceExhausted, "CUDA timeline values are exhausted")};
    }
    if (vulkanSignalValue != _lastSignalValue + 1)
    {
        return std::unexpected {panda::makeError(ErrorCode::InvalidArgument, "Unexpected Vulkan release value")};
    }
    return {};
}

auto SharedCudaBufferState::submitWork(std::uint64_t vulkanSignalValue, const CudaWorkCallback& work)
    -> Result<CudaWorkSubmission>
{
    if (const auto valid = validateSubmission(vulkanSignalValue, work); !valid)
    {
        return std::unexpected {valid.error()};
    }
    if (const auto waited = waitForVulkan(vulkanSignalValue); !waited)
    {
        return std::unexpected {waited.error()};
    }

    auto workResult =
        enqueueWork(work, getInteropStream(_session->getNativeSession()), _imported.devicePointer, getSize());
    const auto signalValue = vulkanSignalValue + 1;
    if (const auto signaled = signalToVulkan(signalValue); !signaled)
    {
        _submissionFaulted = true;
        return std::unexpected {signaled.error()};
    }
    _lastSignalValue = signalValue;
    return CudaWorkSubmission {.signalValue = ExternalTimelineValue {signalValue}, .workResult = std::move(workResult)};
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
