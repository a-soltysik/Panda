#include "CudaInterop.hpp"

#include <cstdint>
#include <expected>
#include <optional>
#include <panda/Assert.hpp>
#include <panda/Error.hpp>
#include <panda/ExternalBuffer.hpp>
#include <panda/ExternalMemoryHandle.hpp>
#include <panda/ExternalSemaphoreHandle.hpp>
#include <panda/Logger.hpp>
#include <string>
#include <string_view>
#include <utility>

#include "CudaAdapter.hpp"

namespace panda::cuda::detail
{
namespace
{
auto interopErrorDetail(NativeInteropStatus status) -> std::pair<ErrorCode, std::string_view>
{
    switch (status)
    {
    case NativeInteropStatus::Success:
        return {ErrorCode::BackendFailure, " unexpectedly failed with success status"};
    case NativeInteropStatus::NoDriver:
        return {ErrorCode::Unsupported, ": no compatible CUDA driver is available"};
    case NativeInteropStatus::NoDevice:
        return {ErrorCode::Unsupported, ": no compatible CUDA device is available"};
    case NativeInteropStatus::Unsupported:
        return {ErrorCode::Unsupported, ": the selected device or external handle type is unsupported"};
    case NativeInteropStatus::Failure:
        return {ErrorCode::BackendFailure, ": CUDA Runtime operation failed"};
    }
    return {ErrorCode::BackendFailure, ": CUDA Runtime operation failed"};
}

auto nativeHandleType(ExternalMemoryHandleType type) -> NativeMemoryHandleType
{
    switch (type)
    {
    case ExternalMemoryHandleType::OpaqueFd:
        return NativeMemoryHandleType::OpaqueFd;
    case ExternalMemoryHandleType::OpaqueWin32:
        return NativeMemoryHandleType::OpaqueWin32;
    }
    panda::panic("Invalid external memory handle type");
}

auto nativeSemaphoreHandleType(ExternalSemaphoreHandleType type) -> NativeSemaphoreHandleType
{
    switch (type)
    {
    case ExternalSemaphoreHandleType::OpaqueFd:
        return NativeSemaphoreHandleType::OpaqueFd;
    case ExternalSemaphoreHandleType::OpaqueWin32:
        return NativeSemaphoreHandleType::OpaqueWin32;
    }
    panda::panic("Invalid external semaphore handle type");
}

auto checkNativeBufferImport(const NativeSession& nativeSession, const NativeExternalBufferResult& imported)
    -> Result<NativeExternalBufferResult>
{
    if (imported.status != NativeInteropStatus::Success)
    {
        return std::unexpected {interopError(imported.status,
                                             imported.nativeCode,
                                             "Could not import the Vulkan buffer into CUDA",
                                             imported.cleanupCode)};
    }
    if (imported.externalMemory == nullptr || imported.devicePointer == nullptr)
    {
        reportCleanupFailure(destroyExternalBuffer(nativeSession, imported), "CUDA external buffer cleanup failed");
        return std::unexpected {
            panda::makeError(ErrorCode::BackendFailure, "CUDA returned an empty external buffer mapping")};
    }
    return imported;
}
}

auto interopError(NativeInteropStatus status, std::int32_t nativeCode, std::string operation, std::int32_t cleanupCode)
    -> panda::Error
{
    const auto [code, detail] = interopErrorDetail(status);
    operation += detail;
    if (cleanupCode != 0)
    {
        operation += "; CUDA cleanup also failed with status " + std::to_string(cleanupCode);
    }
    const auto native = nativeCode != 0 ? std::optional {panda::NativeError {.api = "CUDA", .code = nativeCode}}
                                        : std::nullopt;
    return panda::makeError(code, std::move(operation), native);
}

void reportCleanupFailure(std::int32_t status, std::string_view message) noexcept
{
    if (status == 0)
    {
        return;
    }
    try
    {
        panda::log::error("{}: CUDA Runtime cleanup status {}", message, status);
    }
    catch (...)
    {
        panda::panic("Could not report a CUDA Runtime cleanup failure");
    }
}

auto validateExternalBuffer(const ExternalBuffer& buffer, const VulkanDeviceUuid& deviceUuid) -> Result<void>
{
    if (!buffer.isValid() || buffer.getAllocationSize() < buffer.getSize() || buffer.getSize() == 0)
    {
        return std::unexpected {panda::makeError(ErrorCode::InvalidArgument, "Invalid external buffer")};
    }
    if (buffer.getDeviceUuid() != deviceUuid)
    {
        return std::unexpected {
            panda::makeError(ErrorCode::InvalidArgument, "External buffer belongs to a different Vulkan device")};
    }
    return {};
}

auto importNativeBuffer(const NativeSession& nativeSession, ExternalBuffer& buffer)
    -> Result<NativeExternalBufferResult>
{
#ifdef __linux__
    auto& handle = buffer.getHandle();
#else
    const auto& handle = buffer.getHandle();
#endif
    const auto imported = detail::importExternalBuffer(nativeSession,
                                                       {.handle = handle.get(),
                                                        .handleType = nativeHandleType(handle.getType()),
                                                        .allocationSize = buffer.getAllocationSize(),
                                                        .bufferSize = buffer.getSize()});
#ifdef __linux__
    if (imported.imported)
    {
        static_cast<void>(handle.release());
    }
#endif
    return checkNativeBufferImport(nativeSession, imported);
}

auto importNativeSemaphore(const NativeSession& nativeSession, const ExternalSemaphoreHandle& handle)
    -> Result<std::optional<NativeExternalSemaphoreResult>>
{
    if (!handle.isValid())
    {
        return std::nullopt;
    }
    const auto imported = detail::importExternalSemaphore(
        nativeSession,
        {.handle = handle.get(), .handleType = nativeSemaphoreHandleType(handle.getType())});
    if (imported.status != NativeInteropStatus::Success)
    {
        return std::unexpected {interopError(imported.status,
                                             imported.nativeCode,
                                             "Could not import the Vulkan semaphore into CUDA",
                                             imported.cleanupCode)};
    }
    if (!imported.imported || imported.externalSemaphore == nullptr)
    {
        return std::unexpected {panda::makeError(ErrorCode::BackendFailure, "CUDA returned an empty semaphore")};
    }
    return std::optional {imported};
}
}
