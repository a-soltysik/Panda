#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <panda/Error.hpp>
#include <panda/ExternalBuffer.hpp>
#include <panda/cuda/SharedCudaBuffer.hpp>

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
    [[nodiscard]] auto submitWork(std::uint64_t vulkanSignalValue, const CudaWorkCallback& work)
        -> Result<CudaWorkSubmission>;
    [[nodiscard]] auto import() -> Result<void>;

private:
    [[nodiscard]] auto validateSubmission(std::uint64_t vulkanSignalValue, const CudaWorkCallback& work) const
        -> Result<void>;
    [[nodiscard]] auto waitForVulkan(std::uint64_t value) -> Result<void>;
    [[nodiscard]] auto signalToVulkan(std::uint64_t value) -> Result<void>;

    panda::ExternalBuffer _externalBuffer;
    std::shared_ptr<CudaSessionState> _session;
    NativeExternalBufferResult _imported;
    std::optional<NativeExternalSemaphoreResult> _importedSemaphore;
    std::uint64_t _lastSignalValue {0};
    bool _submissionFaulted {false};
};
}
