#include <expected>
#include <memory>
#include <panda/Context.hpp>
#include <panda/Error.hpp>
#include <panda/ExternalBuffer.hpp>
#include <panda/cuda/CudaSession.hpp>
#include <panda/cuda/SharedCudaBuffer.hpp>
#include <utility>

#include "CudaAdapter.hpp"
#include "CudaInterop.hpp"
#include "CudaSessionState.hpp"
#include "SharedCudaBufferState.hpp"

namespace panda::cuda
{
auto CudaSession::create(const panda::Context& context) -> Result<CudaSession>
{
    const auto& deviceUuid = context.getDeviceInfo().deviceUuid;
    auto state = std::make_shared<detail::CudaSessionState>(deviceUuid);
    auto native = detail::createInteropSession(deviceUuid);
    if (native.status != detail::NativeInteropStatus::Success)
    {
        return std::unexpected {detail::interopError(native.status,
                                                     native.nativeCode,
                                                     "Could not create a CUDA session",
                                                     native.cleanupCode)};
    }
    if (!native.session)
    {
        return std::unexpected {panda::makeError(ErrorCode::BackendFailure, "CUDA returned an empty session")};
    }
    state->setNativeSession(std::move(native.session));
    return CudaSession {std::move(state)};
}

CudaSession::CudaSession(std::shared_ptr<detail::CudaSessionState> state)
    : _state {std::move(state)}
{
}

CudaSession::CudaSession(CudaSession&&) noexcept = default;

auto CudaSession::operator=(CudaSession&&) noexcept -> CudaSession& = default;

CudaSession::~CudaSession() = default;

auto CudaSession::importBuffer(panda::ExternalBuffer buffer) -> Result<SharedCudaBuffer>
{
    if (_state == nullptr)
    {
        return std::unexpected {panda::makeError(ErrorCode::InvalidArgument, "CUDA session is closed")};
    }
    if (const auto valid = detail::validateExternalBuffer(buffer, _state->getDeviceUuid()); !valid)
    {
        return std::unexpected {valid.error()};
    }
    auto resource = std::make_shared<detail::SharedCudaBufferState>(std::move(buffer), _state);
    return resource->import().transform([&resource] {
        return SharedCudaBuffer {std::move(resource)};
    });
}

auto CudaSession::close() -> Result<void>
{
    if (_state != nullptr && _state.use_count() != 1)
    {
        return std::unexpected {
            panda::makeError(ErrorCode::ResourcesInUse, "Shared CUDA buffers still retain this session")};
    }
    _state.reset();
    return {};
}
}
