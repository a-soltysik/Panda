#pragma once

#include <driver_types.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace panda::cuda::detail
{
struct NativeSession;
// Keep CUDA headers local while retaining the native types of opaque handles.
inline constexpr std::size_t nativeDeviceUuidSize {16};

struct NativeSessionDeleter
{
    void operator()(NativeSession* session) const noexcept;
};

using NativeSessionOwner = std::unique_ptr<NativeSession, NativeSessionDeleter>;

// C++20 adapter contract consumed by the C++23 facade; keep engine types out of this boundary.
enum class NativeAvailability : std::uint8_t
{
    Available,
    NoDriver,
    NoDevice,
    Failure
};

struct NativeAvailabilityResult
{
    NativeAvailability availability;
    std::int32_t nativeCode;
};

enum class NativeInteropStatus : std::uint8_t
{
    Success,
    NoDriver,
    NoDevice,
    Unsupported,
    Failure
};

struct NativeInteropResult
{
    NativeInteropStatus status {NativeInteropStatus::Failure};
    std::int32_t nativeCode {0};
    std::int32_t cleanupCode {0};
    NativeSessionOwner session;
};

struct NativeInteropOperationResult
{
    NativeInteropStatus status {NativeInteropStatus::Failure};
    std::int32_t nativeCode {0};
};

struct NativeExternalBufferResult
{
    NativeInteropStatus status {NativeInteropStatus::Failure};
    std::int32_t nativeCode {0};
    std::int32_t cleanupCode {0};
    cudaExternalMemory_t externalMemory {nullptr};
    std::byte* devicePointer {nullptr};
    bool imported {false};
};

struct NativeExternalBufferImport
{
    std::intptr_t handle;
    std::uint64_t allocationSize;
    std::uint64_t bufferSize;
};

struct NativeExternalSemaphoreResult
{
    NativeInteropStatus status {NativeInteropStatus::Failure};
    std::int32_t nativeCode {0};
    std::int32_t cleanupCode {0};
    cudaExternalSemaphore_t externalSemaphore {nullptr};
    bool imported {false};
};

struct NativeExternalSemaphoreImport
{
    std::intptr_t handle;
};

[[nodiscard]] auto queryNativeAvailability() noexcept -> NativeAvailabilityResult;
[[nodiscard]] auto createInteropSession(std::span<const std::uint8_t, nativeDeviceUuidSize> deviceUuid) noexcept
    -> NativeInteropResult;
[[nodiscard]] auto importExternalBuffer(const NativeSession& session,
                                        const NativeExternalBufferImport& description) noexcept
    -> NativeExternalBufferResult;
[[nodiscard]] auto importExternalSemaphore(const NativeSession& session,
                                           const NativeExternalSemaphoreImport& description) noexcept
    -> NativeExternalSemaphoreResult;
[[nodiscard]] auto enqueueExternalSemaphoreWait(const NativeSession& session,
                                                const cudaExternalSemaphore_t& semaphore,
                                                std::uint64_t value) noexcept -> NativeInteropOperationResult;
[[nodiscard]] auto enqueueExternalSemaphoreSignal(const NativeSession& session,
                                                  const cudaExternalSemaphore_t& semaphore,
                                                  std::uint64_t value) noexcept -> NativeInteropOperationResult;
[[nodiscard]] auto getInteropStream(const NativeSession& session) noexcept -> cudaStream_t;
[[nodiscard]] auto synchronizeInteropSession(const NativeSession& session) noexcept -> std::int32_t;
[[nodiscard]] auto destroyInteropSession(NativeSessionOwner session) noexcept -> std::int32_t;
[[nodiscard]] auto destroyExternalBuffer(const NativeSession& session,
                                         const NativeExternalBufferResult& imported) noexcept -> std::int32_t;
[[nodiscard]] auto destroyExternalSemaphore(const NativeSession& session,
                                            const NativeExternalSemaphoreResult& imported) noexcept -> std::int32_t;

}
