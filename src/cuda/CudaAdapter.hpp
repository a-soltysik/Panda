#pragma once

#include <cstdint>

namespace panda::cuda::detail
{

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
    void* session {nullptr};
};

struct NativeExternalBufferResult
{
    NativeInteropStatus status {NativeInteropStatus::Failure};
    std::int32_t nativeCode {0};
    std::int32_t cleanupCode {0};
    void* externalMemory {nullptr};
    void* devicePointer {nullptr};
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
    void* externalSemaphore {nullptr};
    bool imported {false};
};

struct NativeExternalSemaphoreImport
{
    std::intptr_t handle;
    NativeSemaphoreHandleType handleType;
};

[[nodiscard]] auto queryNativeAvailability() noexcept -> NativeAvailabilityResult;
[[nodiscard]] auto createInteropSession(const std::uint8_t* deviceUuid) noexcept -> NativeInteropResult;
[[nodiscard]] auto importExternalBuffer(void* session, const NativeExternalBufferImport& description) noexcept
    -> NativeExternalBufferResult;
[[nodiscard]] auto importExternalSemaphore(void* session, const NativeExternalSemaphoreImport& description) noexcept
    -> NativeExternalSemaphoreResult;
[[nodiscard]] auto destroyInteropSession(void* session) noexcept -> std::int32_t;
[[nodiscard]] auto destroyExternalBuffer(void* session, const NativeExternalBufferResult& imported) noexcept
    -> std::int32_t;
[[nodiscard]] auto destroyExternalSemaphore(void* session, const NativeExternalSemaphoreResult& imported) noexcept
    -> std::int32_t;

}
