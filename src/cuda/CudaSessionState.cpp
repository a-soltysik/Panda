#include "CudaSessionState.hpp"

#include <panda/Assert.hpp>
#include <panda/ExternalBuffer.hpp>
#include <utility>

#include "CudaAdapter.hpp"
#include "CudaInterop.hpp"

namespace panda::cuda::detail
{
CudaSessionState::CudaSessionState(VulkanDeviceUuid uuid)
    : _deviceUuid {uuid}
{
}

CudaSessionState::~CudaSessionState()
{
    reportCleanupFailure(destroyInteropSession(std::move(_nativeSession)), "CUDA session destruction failed");
}

auto CudaSessionState::getDeviceUuid() const noexcept -> const VulkanDeviceUuid&
{
    return _deviceUuid;
}

auto CudaSessionState::getNativeSession() const noexcept -> const NativeSession&
{
    if (!_nativeSession)
    {
        panda::panic("CUDA session state has no native session");
    }
    return *_nativeSession;
}

void CudaSessionState::setNativeSession(NativeSessionOwner&& session) noexcept
{
    _nativeSession = std::move(session);
}
}
