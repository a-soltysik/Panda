#include "CudaSessionState.hpp"

#include <panda/DeviceUuid.hpp>
#include <panda/diagnostics/Assert.hpp>
#include <utility>

#include "CudaAdapter.hpp"
#include "CudaInterop.hpp"

namespace panda::cuda::detail
{
CudaSessionState::CudaSessionState(DeviceUuid uuid)
    : _deviceUuid {uuid}
{
}

CudaSessionState::~CudaSessionState()
{
    reportCleanupFailure(destroyInteropSession(std::move(_nativeSession)), "CUDA session destruction failed");
}

auto CudaSessionState::getDeviceUuid() const noexcept -> const DeviceUuid&
{
    return _deviceUuid;
}

auto CudaSessionState::getNativeSession() const noexcept -> const NativeSession&
{
    return panda::expect(_nativeSession, "CUDA session state has no native session");
}

void CudaSessionState::setNativeSession(NativeSessionOwner&& session) noexcept
{
    _nativeSession = std::move(session);
}
}
