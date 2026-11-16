#pragma once

#include <panda/DeviceUuid.hpp>
#include <utility>

#include "CudaAdapter.hpp"

namespace panda::cuda::detail
{
class CudaSessionState final
{
public:
    explicit CudaSessionState(DeviceUuid uuid);
    CudaSessionState(const CudaSessionState&) = delete;
    auto operator=(const CudaSessionState&) -> CudaSessionState& = delete;
    CudaSessionState(CudaSessionState&&) = delete;
    auto operator=(CudaSessionState&&) -> CudaSessionState& = delete;
    ~CudaSessionState();

    [[nodiscard]] auto getDeviceUuid() const noexcept -> const DeviceUuid&;
    [[nodiscard]] auto getNativeSession() const noexcept -> const NativeSession&;
    void setNativeSession(NativeSessionOwner&& session) noexcept;

private:
    DeviceUuid _deviceUuid;
    NativeSessionOwner _nativeSession;
};
}
