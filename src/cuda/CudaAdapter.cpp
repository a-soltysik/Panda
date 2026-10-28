#include "CudaAdapter.hpp"

#include <cuda_runtime_api.h>
#include <driver_types.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <utility>

namespace panda::cuda::detail
{
struct NativeSession
{
    int device {0};
    cudaStream_t stream {nullptr};
};

namespace
{
struct NativeDeviceResult
{
    NativeInteropStatus status {NativeInteropStatus::Failure};
    std::int32_t nativeCode {0};
    int device {-1};
};

auto interopStatus(cudaError_t error) -> NativeInteropStatus
{
    if (error == cudaErrorNoDevice)
    {
        return NativeInteropStatus::NoDevice;
    }
    if (error == cudaErrorInsufficientDriver || error == cudaErrorSystemDriverMismatch)
    {
        return NativeInteropStatus::NoDriver;
    }
    if (error == cudaErrorNotSupported)
    {
        return NativeInteropStatus::Unsupported;
    }
    return NativeInteropStatus::Failure;
}

auto destroyExternalMemory(NativeExternalMemory memory, std::byte* devicePointer) -> cudaError_t
{
    auto firstError = devicePointer != nullptr ? cudaFree(devicePointer) : cudaSuccess;
    if (memory != nullptr)
    {
        const auto status = cudaDestroyExternalMemory(memory);
        if (firstError == cudaSuccess)
        {
            firstError = status;
        }
    }
    return firstError;
}

auto failedSession(NativeSessionOwner session, cudaError_t error) -> NativeInteropResult
{
    auto cleanup = cudaSuccess;
    if (session->stream != nullptr)
    {
        cleanup = cudaStreamDestroy(session->stream);
    }
    return NativeInteropResult {.status = interopStatus(error),
                                .nativeCode = static_cast<std::int32_t>(error),
                                .cleanupCode = static_cast<std::int32_t>(cleanup),
                                .session = {}};
}

auto checkDeviceUuid(int device, std::span<const std::uint8_t, nativeDeviceUuidSize> deviceUuid) -> NativeDeviceResult
{
    auto properties = cudaDeviceProp {};
    const auto status = cudaGetDeviceProperties(&properties, device);
    if (status != cudaSuccess)
    {
        return {.status = interopStatus(status), .nativeCode = static_cast<std::int32_t>(status)};
    }
    const auto uuid = std::span {properties.uuid.bytes};
    return {.status = std::memcmp(uuid.data(), deviceUuid.data(), uuid.size_bytes()) == 0
                          ? NativeInteropStatus::Success
                          : NativeInteropStatus::Unsupported,
            .device = device};
}

auto findMatchingDevice(std::span<const std::uint8_t, nativeDeviceUuidSize> deviceUuid) -> NativeDeviceResult
{
    auto deviceCount = int {};
    const auto countStatus = cudaGetDeviceCount(&deviceCount);
    if (countStatus != cudaSuccess || deviceCount == 0)
    {
        return {.status = countStatus == cudaSuccess ? NativeInteropStatus::NoDevice : interopStatus(countStatus),
                .nativeCode = static_cast<std::int32_t>(countStatus)};
    }
    for (auto device = 0; device < deviceCount; ++device)
    {
        const auto result = checkDeviceUuid(device, deviceUuid);
        if (result.status != NativeInteropStatus::Unsupported || result.nativeCode != 0)
        {
            return result;
        }
    }
    return {.status = NativeInteropStatus::Unsupported};
}

auto createSessionForDevice(int device) -> NativeInteropResult
{
    const auto selected = cudaSetDevice(device);
    if (selected != cudaSuccess)
    {
        return {.status = interopStatus(selected), .nativeCode = static_cast<std::int32_t>(selected), .session = {}};
    }
    // Ownership transfers through NativeInteropResult after stream creation.
    // cppcheck-suppress unreadVariable
    auto session = NativeSessionOwner {new (std::nothrow) NativeSession {.device = device}};
    if (session == nullptr)
    {
        return {.status = NativeInteropStatus::Failure, .session = {}};
    }
    auto* const sessionPointer = session.get();
    const auto streamStatus = cudaStreamCreateWithFlags(&sessionPointer->stream, cudaStreamNonBlocking);
    if (streamStatus != cudaSuccess)
    {
        return failedSession(std::move(session), streamStatus);
    }
    return NativeInteropResult {.status = NativeInteropStatus::Success, .session = std::move(session)};
}

auto importPreparedBuffer(const NativeSession& session,
                          const NativeExternalBufferImport& description,
                          cudaExternalMemoryHandleDesc descriptor) -> NativeExternalBufferResult
{
    const auto selected = cudaSetDevice(session.device);
    if (selected != cudaSuccess)
    {
        return {.status = interopStatus(selected), .nativeCode = static_cast<std::int32_t>(selected)};
    }
    cudaExternalMemory_t externalMemory = nullptr;
    const auto imported = cudaImportExternalMemory(&externalMemory, &descriptor);
    if (imported != cudaSuccess)
    {
        return {.status = interopStatus(imported), .nativeCode = static_cast<std::int32_t>(imported)};
    }
    auto bufferDescriptor = cudaExternalMemoryBufferDesc {};
    bufferDescriptor.size = description.bufferSize;
    void* devicePointer = nullptr;
    const auto mapped = cudaExternalMemoryGetMappedBuffer(&devicePointer, externalMemory, &bufferDescriptor);
    auto* const mappedPointer = static_cast<std::byte*>(devicePointer);
    if (mapped != cudaSuccess)
    {
        return {.status = interopStatus(mapped),
                .nativeCode = static_cast<std::int32_t>(mapped),
                .cleanupCode = static_cast<std::int32_t>(destroyExternalMemory(externalMemory, mappedPointer)),
                .imported = true};
    }
    return NativeExternalBufferResult {.status = NativeInteropStatus::Success,
                                       .externalMemory = externalMemory,
                                       .devicePointer = mappedPointer,
                                       .imported = true};
}

// NOLINTNEXTLINE(readability-function-size): Platform-specific descriptors must match the native CUDA ABI.
auto externalMemoryDescriptor(const NativeExternalBufferImport& description)
    -> std::optional<cudaExternalMemoryHandleDesc>
{
#ifdef __linux__
    if (description.handleType != NativeMemoryHandleType::OpaqueFd)
    {
        return std::nullopt;
    }
    return cudaExternalMemoryHandleDesc {.type = cudaExternalMemoryHandleTypeOpaqueFd,
                                         .handle = {.fd = static_cast<int>(description.handle)},
                                         .size = description.allocationSize,
                                         .flags = cudaExternalMemoryDedicated,
                                         .reserved = {}};
#elif _WIN32
    if (description.handleType != NativeMemoryHandleType::OpaqueWin32)
    {
        return std::nullopt;
    }
    return cudaExternalMemoryHandleDesc {
        .type = cudaExternalMemoryHandleTypeOpaqueWin32,
        .handle =
            {.win32 =
                 {.handle =
                      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast, performance-no-int-to-ptr): CUDA requires HANDLE.
                  reinterpret_cast<decltype(std::declval<cudaExternalMemoryHandleDesc>().handle.win32.handle)>(
                      description.handle),
                  .name = nullptr}},
        .size = description.allocationSize,
        .flags = cudaExternalMemoryDedicated,
        .reserved = {}};
#else
    static_cast<void>(description);
    return std::nullopt;
#endif
}

auto externalSemaphoreDescriptor(const NativeExternalSemaphoreImport& description)
    -> std::optional<cudaExternalSemaphoreHandleDesc>
{
#ifdef __linux__
    if (description.handleType != NativeSemaphoreHandleType::OpaqueFd)
    {
        return std::nullopt;
    }
    return cudaExternalSemaphoreHandleDesc {.type = cudaExternalSemaphoreHandleTypeTimelineSemaphoreFd,
                                            .handle = {.fd = static_cast<int>(description.handle)},
                                            .flags = 0,
                                            .reserved = {}};
#elif _WIN32
    if (description.handleType != NativeSemaphoreHandleType::OpaqueWin32)
    {
        return std::nullopt;
    }
    return cudaExternalSemaphoreHandleDesc {
        .type = cudaExternalSemaphoreHandleTypeTimelineSemaphoreWin32,
        .handle =
            {.win32 =
                 {.handle =
                      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast, performance-no-int-to-ptr): CUDA requires HANDLE.
                  reinterpret_cast<decltype(std::declval<cudaExternalSemaphoreHandleDesc>().handle.win32.handle)>(
                      description.handle),
                  .name = nullptr}},
        .flags = 0,
        .reserved = {}};
#else
    static_cast<void>(description);
    return std::nullopt;
#endif
}
}

auto queryNativeAvailability() noexcept -> NativeAvailabilityResult
{
    auto deviceCount = int {};
    const auto status = cudaGetDeviceCount(&deviceCount);
    if (status == cudaErrorNoDevice)
    {
        return {.availability = NativeAvailability::NoDevice, .nativeCode = static_cast<std::int32_t>(status)};
    }
    if (status == cudaErrorInsufficientDriver || status == cudaErrorSystemDriverMismatch)
    {
        return {.availability = NativeAvailability::NoDriver, .nativeCode = static_cast<std::int32_t>(status)};
    }
    if (status != cudaSuccess)
    {
        return {.availability = NativeAvailability::Failure, .nativeCode = static_cast<std::int32_t>(status)};
    }
    if (deviceCount == 0)
    {
        return {.availability = NativeAvailability::NoDevice, .nativeCode = 0};
    }
    return {.availability = NativeAvailability::Available, .nativeCode = 0};
}

auto NativeSessionDeleter::operator()(NativeSession* session) const noexcept -> void
{
    std::default_delete<NativeSession> {}(session);
}

auto createInteropSession(std::span<const std::uint8_t, nativeDeviceUuidSize> deviceUuid) noexcept
    -> NativeInteropResult
{
    const auto matchingDevice = findMatchingDevice(deviceUuid);
    if (matchingDevice.status != NativeInteropStatus::Success)
    {
        return {.status = matchingDevice.status, .nativeCode = matchingDevice.nativeCode, .session = {}};
    }
    return createSessionForDevice(matchingDevice.device);
}

auto importExternalBuffer(const NativeSession& session, const NativeExternalBufferImport& description) noexcept
    -> NativeExternalBufferResult
{
    const auto descriptor = externalMemoryDescriptor(description);
    return descriptor ? importPreparedBuffer(session, description, *descriptor)
                      : NativeExternalBufferResult {.status = NativeInteropStatus::Unsupported};
}

auto importExternalSemaphore(const NativeSession& session, const NativeExternalSemaphoreImport& description) noexcept
    -> NativeExternalSemaphoreResult
{
    const auto descriptor = externalSemaphoreDescriptor(description);
    if (!descriptor)
    {
        return {.status = NativeInteropStatus::Unsupported};
    }
    const auto selected = cudaSetDevice(session.device);
    if (selected != cudaSuccess)
    {
        return {.status = interopStatus(selected), .nativeCode = static_cast<std::int32_t>(selected)};
    }
    auto* semaphore = cudaExternalSemaphore_t {};
    const auto imported = cudaImportExternalSemaphore(&semaphore, &*descriptor);
    if (imported != cudaSuccess)
    {
        return {.status = interopStatus(imported), .nativeCode = static_cast<std::int32_t>(imported)};
    }
    return NativeExternalSemaphoreResult {.status = NativeInteropStatus::Success,
                                          .externalSemaphore = semaphore,
                                          .imported = true};
}

auto enqueueExternalSemaphoreWait(const NativeSession& session,
                                  const NativeExternalSemaphore& externalSemaphore,
                                  std::uint64_t value) noexcept -> NativeInteropOperationResult
{
    if (externalSemaphore == nullptr || value == 0)
    {
        return {.status = NativeInteropStatus::Failure, .nativeCode = cudaErrorInvalidValue};
    }
    const auto selected = cudaSetDevice(session.device);
    if (selected != cudaSuccess)
    {
        return {.status = interopStatus(selected), .nativeCode = static_cast<std::int32_t>(selected)};
    }
    auto* const semaphore = externalSemaphore;
    auto params = cudaExternalSemaphoreWaitParams {};
    params.params.fence.value = value;
    const auto enqueued = cudaWaitExternalSemaphoresAsync(&semaphore, &params, 1, session.stream);
    return {.status = enqueued == cudaSuccess ? NativeInteropStatus::Success : interopStatus(enqueued),
            .nativeCode = static_cast<std::int32_t>(enqueued)};
}

auto enqueueExternalSemaphoreSignal(const NativeSession& session,
                                    const NativeExternalSemaphore& externalSemaphore,
                                    std::uint64_t value) noexcept -> NativeInteropOperationResult
{
    if (externalSemaphore == nullptr || value == 0)
    {
        return {.status = NativeInteropStatus::Failure, .nativeCode = cudaErrorInvalidValue};
    }
    const auto selected = cudaSetDevice(session.device);
    if (selected != cudaSuccess)
    {
        return {.status = interopStatus(selected), .nativeCode = static_cast<std::int32_t>(selected)};
    }
    auto* const semaphore = externalSemaphore;
    auto params = cudaExternalSemaphoreSignalParams {};
    params.params.fence.value = value;
    const auto enqueued = cudaSignalExternalSemaphoresAsync(&semaphore, &params, 1, session.stream);
    return {.status = enqueued == cudaSuccess ? NativeInteropStatus::Success : interopStatus(enqueued),
            .nativeCode = static_cast<std::int32_t>(enqueued)};
}

auto synchronizeInteropSession(const NativeSession& session) noexcept -> std::int32_t
{
    const auto selected = cudaSetDevice(session.device);
    if (selected != cudaSuccess)
    {
        return static_cast<std::int32_t>(selected);
    }
    return static_cast<std::int32_t>(cudaStreamSynchronize(session.stream));
}

auto destroyInteropSession(NativeSessionOwner nativeSession) noexcept -> std::int32_t
{
    if (!nativeSession)
    {
        return 0;
    }
    const auto selected = cudaSetDevice(nativeSession->device);
    if (selected != cudaSuccess)
    {
        return static_cast<std::int32_t>(selected);
    }
    return static_cast<std::int32_t>(cudaStreamDestroy(nativeSession->stream));
}

auto destroyExternalBuffer(const NativeSession& session, const NativeExternalBufferResult& imported) noexcept
    -> std::int32_t
{
    const auto selected = cudaSetDevice(session.device);
    if (selected != cudaSuccess)
    {
        return static_cast<std::int32_t>(selected);
    }
    return static_cast<std::int32_t>(destroyExternalMemory(imported.externalMemory, imported.devicePointer));
}

auto destroyExternalSemaphore(const NativeSession& session, const NativeExternalSemaphoreResult& imported) noexcept
    -> std::int32_t
{
    const auto selected = cudaSetDevice(session.device);
    if (selected != cudaSuccess)
    {
        return static_cast<std::int32_t>(selected);
    }
    return static_cast<std::int32_t>(cudaDestroyExternalSemaphore(imported.externalSemaphore));
}
}
