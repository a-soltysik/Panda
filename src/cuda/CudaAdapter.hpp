#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

// CUDA Runtime typedefs these native handle types to opaque pointers.
// NOLINTNEXTLINE(readability-identifier-naming): These names match the CUDA SDK declarations.
struct CUexternalMemory_st;
// NOLINTNEXTLINE(readability-identifier-naming): These names match the CUDA SDK declarations.
struct CUexternalSemaphore_st;

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

using NativeExternalMemory = ::CUexternalMemory_st*;

using NativeExternalSemaphore = ::CUexternalSemaphore_st*;

// Plain data shared with the C++23 facade. CUDA and engine headers stay local to
// their respective translation units.
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

enum class NativeMemoryHandleType : std::uint8_t
{
    OpaqueFd,
    OpaqueWin32
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
    NativeExternalMemory externalMemory {nullptr};
    std::byte* devicePointer {nullptr};
    bool imported {false};
};

struct NativeExternalBufferImport
{
    std::intptr_t handle;
    NativeMemoryHandleType handleType;
    std::uint64_t allocationSize;
    std::uint64_t bufferSize;
};

enum class NativeSemaphoreHandleType : std::uint8_t
{
    OpaqueFd,
    OpaqueWin32
};

struct NativeExternalSemaphoreResult
{
    NativeInteropStatus status {NativeInteropStatus::Failure};
    std::int32_t nativeCode {0};
    std::int32_t cleanupCode {0};
    NativeExternalSemaphore externalSemaphore {nullptr};
    bool imported {false};
};

struct NativeExternalSemaphoreImport
{
    std::intptr_t handle;
    NativeSemaphoreHandleType handleType;
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
                                                const NativeExternalSemaphore& semaphore,
                                                std::uint64_t value) noexcept -> NativeInteropOperationResult;
[[nodiscard]] auto enqueueExternalSemaphoreSignal(const NativeSession& session,
                                                  const NativeExternalSemaphore& semaphore,
                                                  std::uint64_t value) noexcept -> NativeInteropOperationResult;
[[nodiscard]] auto synchronizeInteropSession(const NativeSession& session) noexcept -> std::int32_t;
[[nodiscard]] auto destroyInteropSession(NativeSessionOwner session) noexcept -> std::int32_t;
[[nodiscard]] auto destroyExternalBuffer(const NativeSession& session,
                                         const NativeExternalBufferResult& imported) noexcept -> std::int32_t;
[[nodiscard]] auto destroyExternalSemaphore(const NativeSession& session,
                                            const NativeExternalSemaphoreResult& imported) noexcept -> std::int32_t;

}
