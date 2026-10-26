#include "CudaAdapter.hpp"

#include <cuda_runtime_api.h>
#include <driver_types.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <utility>

namespace panda::cuda::detail
{
namespace
{
struct NativeSession
{
    int device {0};
    cudaStream_t stream {nullptr};
};

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

auto destroyExternalMemory(cudaExternalMemory_t memory, void* devicePointer) -> cudaError_t
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

auto failedSession(std::unique_ptr<NativeSession> session, cudaError_t error) -> NativeInteropResult
{
    auto cleanup = cudaSuccess;
    if (session->stream != nullptr)
    {
        cleanup = cudaStreamDestroy(session->stream);
    }
    return NativeInteropResult {.status = interopStatus(error),
                                .nativeCode = static_cast<std::int32_t>(error),
                                .cleanupCode = static_cast<std::int32_t>(cleanup)};
}

auto checkDeviceUuid(int device, const std::uint8_t* deviceUuid) -> NativeDeviceResult
{
    auto properties = cudaDeviceProp {};
    const auto status = cudaGetDeviceProperties(&properties, device);
    if (status != cudaSuccess)
    {
        return {.status = interopStatus(status), .nativeCode = static_cast<std::int32_t>(status)};
    }
    const auto uuid = std::span {properties.uuid.bytes};
    return {.status = std::memcmp(uuid.data(), deviceUuid, uuid.size_bytes()) == 0 ? NativeInteropStatus::Success
                                                                                   : NativeInteropStatus::Unsupported,
            .device = device};
}

auto findMatchingDevice(const std::uint8_t* deviceUuid) -> NativeDeviceResult
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
        return {.status = interopStatus(selected), .nativeCode = static_cast<std::int32_t>(selected)};
    }
    // Ownership transfers through NativeInteropResult after stream creation.
    // cppcheck-suppress unreadVariable
    auto session = std::unique_ptr<NativeSession> {new (std::nothrow) NativeSession {.device = device}};
    if (session == nullptr)
    {
        return {.status = NativeInteropStatus::Failure};
    }
    auto* const sessionPointer = session.get();
    const auto streamStatus = cudaStreamCreateWithFlags(&sessionPointer->stream, cudaStreamNonBlocking);
    if (streamStatus != cudaSuccess)
    {
        return failedSession(std::move(session), streamStatus);
    }
    return NativeInteropResult {.status = NativeInteropStatus::Success, .session = session.release()};
}

auto importPreparedBuffer(void* nativeSession,
                          const NativeExternalBufferImport& description,
                          cudaExternalMemoryHandleDesc descriptor) -> NativeExternalBufferResult
{
    auto& session = *static_cast<NativeSession*>(nativeSession);
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
    if (mapped != cudaSuccess)
    {
        return {.status = interopStatus(mapped),
                .nativeCode = static_cast<std::int32_t>(mapped),
                .cleanupCode = static_cast<std::int32_t>(destroyExternalMemory(externalMemory, devicePointer)),
                .imported = true};
    }
    return NativeExternalBufferResult {.status = NativeInteropStatus::Success,
                                       .externalMemory = externalMemory,
                                       .devicePointer = devicePointer,
                                       .imported = true};
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
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast, performance-no-int-to-ptr): Core carries HANDLE as intptr_t.
        .handle = {.win32 = {.handle = reinterpret_cast<void*>(description.handle), .name = nullptr}},
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

auto createInteropSession(const std::uint8_t* deviceUuid) noexcept -> NativeInteropResult
{
    const auto matchingDevice = findMatchingDevice(deviceUuid);
    if (matchingDevice.status != NativeInteropStatus::Success)
    {
        return {.status = matchingDevice.status, .nativeCode = matchingDevice.nativeCode};
    }
    return createSessionForDevice(matchingDevice.device);
}

auto importExternalBuffer(void* nativeSession, const NativeExternalBufferImport& description) noexcept
    -> NativeExternalBufferResult
{
#ifdef __linux__
    if (description.handleType != NativeMemoryHandleType::OpaqueFd)
    {
        return {.status = NativeInteropStatus::Unsupported};
    }
    auto descriptor = cudaExternalMemoryHandleDesc {.type = cudaExternalMemoryHandleTypeOpaqueFd,
                                                    .handle = {.fd = static_cast<int>(description.handle)},
                                                    .size = description.allocationSize,
                                                    .flags = cudaExternalMemoryDedicated,
                                                    .reserved = {}};
    return importPreparedBuffer(nativeSession, description, descriptor);
#elif _WIN32
    if (description.handleType != NativeMemoryHandleType::OpaqueWin32)
    {
        return {.status = NativeInteropStatus::Unsupported};
    }
    auto descriptor = cudaExternalMemoryHandleDesc {
        .type = cudaExternalMemoryHandleTypeOpaqueWin32,
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast, performance-no-int-to-ptr): Core carries HANDLE as intptr_t.
        .handle = {.win32 = {.handle = reinterpret_cast<void*>(description.handle), .name = nullptr}},
        .size = description.allocationSize,
        .flags = cudaExternalMemoryDedicated,
        .reserved = {}};
    return importPreparedBuffer(nativeSession, description, descriptor);
#else
    static_cast<void>(nativeSession);
    static_cast<void>(description);
    return {.status = NativeInteropStatus::Unsupported};
#endif
}

auto importExternalSemaphore(void* nativeSession, const NativeExternalSemaphoreImport& description) noexcept
    -> NativeExternalSemaphoreResult
{
    const auto descriptor = externalSemaphoreDescriptor(description);
    if (!descriptor)
    {
        return {.status = NativeInteropStatus::Unsupported};
    }
    auto& session = *static_cast<NativeSession*>(nativeSession);
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

auto destroyInteropSession(void* nativeSession) noexcept -> std::int32_t
{
    auto session = std::unique_ptr<NativeSession> {static_cast<NativeSession*>(nativeSession)};
    if (!session)
    {
        return 0;
    }
    const auto selected = cudaSetDevice(session->device);
    if (selected != cudaSuccess)
    {
        return static_cast<std::int32_t>(selected);
    }
    return static_cast<std::int32_t>(cudaStreamDestroy(session->stream));
}

auto destroyExternalBuffer(void* nativeSession, const NativeExternalBufferResult& imported) noexcept -> std::int32_t
{
    auto& session = *static_cast<NativeSession*>(nativeSession);
    const auto selected = cudaSetDevice(session.device);
    if (selected != cudaSuccess)
    {
        return static_cast<std::int32_t>(selected);
    }
    return static_cast<std::int32_t>(
        destroyExternalMemory(static_cast<cudaExternalMemory_t>(imported.externalMemory), imported.devicePointer));
}

auto destroyExternalSemaphore(void* nativeSession, const NativeExternalSemaphoreResult& imported) noexcept
    -> std::int32_t
{
    auto& session = *static_cast<NativeSession*>(nativeSession);
    const auto selected = cudaSetDevice(session.device);
    if (selected != cudaSuccess)
    {
        return static_cast<std::int32_t>(selected);
    }
    return static_cast<std::int32_t>(
        cudaDestroyExternalSemaphore(static_cast<cudaExternalSemaphore_t>(imported.externalSemaphore)));
}
}
