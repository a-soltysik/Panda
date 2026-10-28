#pragma once

#include <memory>
#include <panda/Error.hpp>
#include <panda/ExternalBuffer.hpp>
#include <panda/cuda/SharedCudaBuffer.hpp>

namespace panda
{
class Context;
}

namespace panda::cuda
{
namespace detail
{
class CudaSessionState;
}

/// @brief Move-only CUDA session matched to one Context's selected Vulkan device.
/// The Context outlives the session and every shared buffer created from it.
class CudaSession final
{
public:
    /// @brief Matches the selected Vulkan device and creates one non-blocking stream.
    /// @return A session, or an actionable Unsupported/initialization error.
    [[nodiscard]] static auto create(const panda::Context& context) -> Result<CudaSession>;

    CudaSession(const CudaSession&) = delete;
    auto operator=(const CudaSession&) -> CudaSession& = delete;
    /// @brief Transfers ownership of the session facade.
    CudaSession(CudaSession&& other) noexcept;
    /// @brief Closes this facade and takes ownership from other.
    auto operator=(CudaSession&& other) noexcept -> CudaSession&;
    ~CudaSession();

    /// @brief Imports one Panda-owned dedicated external buffer and its optional paired semaphore.
    /// @return A shared owner, or an error. Unsupported usages and memory/semaphore handle
    /// combinations are reported; no CPU copy is used. Importing the semaphore does not
    /// synchronize with Vulkan.
    [[nodiscard]] auto importBuffer(panda::ExternalBuffer buffer) -> Result<SharedCudaBuffer>;

    /// @brief Closes this facade when no SharedCudaBuffer retains the session.
    /// @return `ResourcesInUse` while any shared buffer still needs its mapping or stream.
    [[nodiscard]] auto close() -> Result<void>;

private:
    explicit CudaSession(std::shared_ptr<detail::CudaSessionState> state);

    std::shared_ptr<detail::CudaSessionState> _state;
};
}
