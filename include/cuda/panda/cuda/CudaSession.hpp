#pragma once

#include <memory>
#include <panda/DeviceUuid.hpp>
#include <panda/base/Error.hpp>
#include <panda/cuda/SharedCudaBuffer.hpp>
#include <panda/interop/ExternalBuffer.hpp>

namespace panda::cuda
{
namespace detail
{
class CudaSessionState;
}

/// @brief Move-only CUDA session matched to a generic device identity.
/// The session owns a copied UUID and CUDA stream, independently of Engine. The
/// originating Engine must outlive every imported SharedCudaBuffer and its copies.
class CudaSession final
{
public:
    /// @brief Copies a device UUID, matches the CUDA device and creates a non-blocking stream.
    /// @param deviceUuid UUID from Engine::getDeviceInfo().deviceUuid.
    /// @return A session, or an actionable Unsupported/initialization error.
    [[nodiscard]] static auto create(const DeviceUuid& deviceUuid) -> Result<CudaSession>;

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
