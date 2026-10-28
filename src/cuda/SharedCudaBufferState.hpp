#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <panda/Error.hpp>
#include <panda/ExternalBuffer.hpp>

#include "CudaAdapter.hpp"
#include "CudaSessionState.hpp"

namespace panda::cuda::detail
{
class SharedCudaBufferState final
{
public:
    SharedCudaBufferState(panda::ExternalBuffer buffer, std::shared_ptr<CudaSessionState> owner);
    SharedCudaBufferState(const SharedCudaBufferState&) = delete;
    auto operator=(const SharedCudaBufferState&) -> SharedCudaBufferState& = delete;
    SharedCudaBufferState(SharedCudaBufferState&&) = delete;
    auto operator=(SharedCudaBufferState&&) -> SharedCudaBufferState& = delete;
    ~SharedCudaBufferState();

    [[nodiscard]] auto getBuffer() const -> panda::Buffer;
    [[nodiscard]] auto getSize() const noexcept -> std::uint64_t;
    [[nodiscard]] auto waitForVulkan(std::uint64_t value) -> Result<void>;
    [[nodiscard]] auto signalToVulkan(std::uint64_t value) -> Result<void>;
    [[nodiscard]] auto import() -> Result<void>;

private:
    panda::ExternalBuffer _externalBuffer;
    std::shared_ptr<CudaSessionState> _session;
    NativeExternalBufferResult _imported;
    std::optional<NativeExternalSemaphoreResult> _importedSemaphore;
};
}
