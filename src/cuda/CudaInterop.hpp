#pragma once

#include <cstdint>
#include <optional>
#include <panda/Error.hpp>
#include <panda/ExternalBuffer.hpp>
#include <panda/ExternalMemoryHandle.hpp>
#include <panda/ExternalSemaphoreHandle.hpp>
#include <string>
#include <string_view>

#include "CudaAdapter.hpp"

namespace panda::cuda::detail
{
[[nodiscard]] auto interopError(NativeInteropStatus status,
                                std::int32_t nativeCode,
                                std::string operation,
                                std::int32_t cleanupCode = 0) -> panda::Error;

void reportCleanupFailure(std::int32_t status, std::string_view message) noexcept;

[[nodiscard]] auto validateExternalBuffer(const ExternalBuffer& buffer, const VulkanDeviceUuid& deviceUuid)
    -> Result<void>;

[[nodiscard]] auto importNativeBuffer(const NativeSession& nativeSession, ExternalBuffer& buffer)
    -> Result<NativeExternalBufferResult>;

[[nodiscard]] auto importNativeSemaphore(const NativeSession& nativeSession, const ExternalSemaphoreHandle& handle)
    -> Result<std::optional<NativeExternalSemaphoreResult>>;
}
