#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <panda/Assert.hpp>
#include <panda/Buffer.hpp>
#include <panda/Context.hpp>
#include <panda/Error.hpp>
#include <panda/ExternalBuffer.hpp>
#include <panda/Logger.hpp>
#include <panda/cuda/Cuda.hpp>
#include <string>
#include <string_view>
#include <utility>

#include "CudaAdapter.hpp"

namespace panda::cuda
{
namespace
{
auto interopErrorDetail(detail::NativeInteropStatus status) -> std::pair<ErrorCode, std::string_view>
{
    switch (status)
    {
    case detail::NativeInteropStatus::Success:
        return {ErrorCode::BackendFailure, " unexpectedly failed with success status"};
    case detail::NativeInteropStatus::NoDriver:
        return {ErrorCode::Unsupported, ": no compatible CUDA driver is available"};
    case detail::NativeInteropStatus::NoDevice:
        return {ErrorCode::Unsupported, ": no compatible CUDA device is available"};
    case detail::NativeInteropStatus::Unsupported:
        return {ErrorCode::Unsupported, ": the selected device or external handle type is unsupported"};
    case detail::NativeInteropStatus::Failure:
        return {ErrorCode::BackendFailure, ": CUDA Runtime operation failed"};
    }
    return {ErrorCode::BackendFailure, ": CUDA Runtime operation failed"};
}

template <typename NativeResult>
auto interopError(const NativeResult& result, std::string operation) -> panda::Error
{
    const auto [code, detail] = interopErrorDetail(result.status);
    operation += detail;
    if (result.cleanupCode != 0)
    {
        operation += "; CUDA cleanup also failed with status " + std::to_string(result.cleanupCode);
    }
    const auto native = result.nativeCode != 0
                            ? std::optional {panda::NativeError {.api = "CUDA", .code = result.nativeCode}}
                            : std::nullopt;
    return panda::makeError(code, std::move(operation), native);
}

auto nativeHandleType(ExternalMemoryHandleType type) -> detail::NativeMemoryHandleType
{
    switch (type)
    {
    case ExternalMemoryHandleType::OpaqueFd:
        return detail::NativeMemoryHandleType::OpaqueFd;
    case ExternalMemoryHandleType::OpaqueWin32:
        return detail::NativeMemoryHandleType::OpaqueWin32;
    }
    panda::panic("Invalid external memory handle type");
}

void reportCleanupFailure(std::int32_t status, const char* message) noexcept
{
    if (status == 0)
    {
        return;
    }
    try
    {
        panda::log::error("{}: CUDA Runtime cleanup status {}", message, status);
    }
    catch (...)
    {
        panda::panic("Could not report a CUDA Runtime cleanup failure");
    }
}

auto validateExternalBuffer(const ExternalBuffer& buffer, const VulkanDeviceUuid& deviceUuid) -> Result<void>
{
    if (!buffer.isValid() || buffer.getAllocationSize() < buffer.getSize() || buffer.getSize() == 0)
    {
        return std::unexpected {panda::makeError(ErrorCode::InvalidArgument, "Invalid external buffer")};
    }
    if (buffer.getDeviceUuid() != deviceUuid)
    {
        return std::unexpected {
            panda::makeError(ErrorCode::InvalidArgument, "External buffer belongs to a different Vulkan device")};
    }
    return {};
}

auto importNativeBuffer(void* nativeSession, ExternalBuffer& buffer) -> Result<detail::NativeExternalBufferResult>
{
#ifdef __linux__
    auto& handle = buffer.handle();
#else
    const auto& handle = buffer.handle();
#endif
    const auto imported = detail::importExternalBuffer(nativeSession,
                                                       {.handle = handle.get(),
                                                        .handleType = nativeHandleType(handle.getType()),
                                                        .allocationSize = buffer.getAllocationSize(),
                                                        .bufferSize = buffer.getSize()});
#ifdef __linux__
    if (imported.imported)
    {
        static_cast<void>(handle.release());
    }
#endif
    if (imported.status != detail::NativeInteropStatus::Success)
    {
        return std::unexpected {interopError(imported, "Could not import the Vulkan buffer into CUDA")};
    }
    if (imported.externalMemory == nullptr || imported.devicePointer == nullptr)
    {
        reportCleanupFailure(detail::destroyExternalBuffer(nativeSession, imported),
                             "CUDA external buffer cleanup failed");
        return std::unexpected {
            panda::makeError(ErrorCode::BackendFailure, "CUDA returned an empty external buffer mapping")};
    }
    return imported;
}
}

struct detail::CudaSessionState final
{
public:
    explicit CudaSessionState(VulkanDeviceUuid uuid)
        : _deviceUuid {uuid}
    {
    }

    ~CudaSessionState()
    {
        reportCleanupFailure(detail::destroyInteropSession(_nativeSession), "CUDA session destruction failed");
    }

    CudaSessionState(const CudaSessionState&) = delete;
    auto operator=(const CudaSessionState&) -> CudaSessionState& = delete;
    CudaSessionState(CudaSessionState&&) = delete;
    auto operator=(CudaSessionState&&) -> CudaSessionState& = delete;

    [[nodiscard]] auto getDeviceUuid() const noexcept -> const VulkanDeviceUuid&
    {
        return _deviceUuid;
    }

    [[nodiscard]] auto getNativeSession() const noexcept -> void*
    {
        return _nativeSession;
    }

    void setNativeSession(void* session) noexcept
    {
        _nativeSession = session;
    }

private:
    VulkanDeviceUuid _deviceUuid;
    void* _nativeSession {nullptr};
};

struct detail::SharedCudaBufferState final
{
public:
    SharedCudaBufferState(panda::ExternalBuffer buffer,
                          std::shared_ptr<CudaSessionState> owner,
                          const detail::NativeExternalBufferResult& importedResult)
        : _externalBuffer {std::move(buffer)},
          _session {std::move(owner)},
          _imported {importedResult}
    {
    }

    ~SharedCudaBufferState()
    {
        reportCleanupFailure(detail::destroyExternalBuffer(_session->getNativeSession(), _imported),
                             "CUDA external buffer destruction failed");
    }

    SharedCudaBufferState(const SharedCudaBufferState&) = delete;
    auto operator=(const SharedCudaBufferState&) -> SharedCudaBufferState& = delete;
    SharedCudaBufferState(SharedCudaBufferState&&) = delete;
    auto operator=(SharedCudaBufferState&&) -> SharedCudaBufferState& = delete;

    [[nodiscard]] auto buffer() const -> panda::Buffer
    {
        return _externalBuffer.buffer();
    }

    [[nodiscard]] auto getSize() const noexcept -> std::uint64_t
    {
        return _externalBuffer.getSize();
    }

private:
    panda::ExternalBuffer _externalBuffer;
    std::shared_ptr<CudaSessionState> _session;
    detail::NativeExternalBufferResult _imported;
};

auto queryAvailability() noexcept -> std::expected<Availability, Error>
{
    const auto result = detail::queryNativeAvailability();
    switch (result.availability)
    {
    case detail::NativeAvailability::Available:
        return Availability::Available;
    case detail::NativeAvailability::NoDriver:
        return Availability::NoDriver;
    case detail::NativeAvailability::NoDevice:
        return Availability::NoDevice;
    case detail::NativeAvailability::Failure:
        return std::unexpected {Error {.nativeCode = result.nativeCode}};
    }
    return std::unexpected {Error {.nativeCode = result.nativeCode}};
}

auto SharedCudaBuffer::buffer() const -> panda::Buffer
{
    return _state != nullptr ? _state->buffer() : panda::Buffer {};
}

auto SharedCudaBuffer::getSize() const noexcept -> std::uint64_t
{
    return _state != nullptr ? _state->getSize() : 0;
}

auto SharedCudaBuffer::isValid() const noexcept -> bool
{
    return _state != nullptr;
}

SharedCudaBuffer::SharedCudaBuffer(std::shared_ptr<detail::SharedCudaBufferState> state)
    : _state {std::move(state)}
{
}

auto CudaSession::create(const panda::Context& context) -> Result<CudaSession>
{
    const auto deviceUuid = context.getDeviceInfo().deviceUuid;
    auto state = std::make_shared<detail::CudaSessionState>(deviceUuid);
    const auto native = detail::createInteropSession(deviceUuid.data());
    if (native.status != detail::NativeInteropStatus::Success)
    {
        return std::unexpected {interopError(native, "Could not create a CUDA session")};
    }
    state->setNativeSession(native.session);
    return CudaSession {std::move(state)};
}

CudaSession::CudaSession(std::shared_ptr<detail::CudaSessionState> state)
    : _state {std::move(state)}
{
}

auto CudaSession::importBuffer(panda::ExternalBuffer buffer) -> Result<SharedCudaBuffer>
{
    if (_state == nullptr)
    {
        return std::unexpected {panda::makeError(ErrorCode::InvalidArgument, "CUDA session is closed")};
    }
    if (const auto valid = validateExternalBuffer(buffer, _state->getDeviceUuid()); !valid)
    {
        return std::unexpected {valid.error()};
    }
    auto imported = importNativeBuffer(_state->getNativeSession(), buffer);
    if (!imported)
    {
        return std::unexpected {imported.error()};
    }
    auto resource = std::make_shared<detail::SharedCudaBufferState>(std::move(buffer), _state, *imported);
    return SharedCudaBuffer {std::move(resource)};
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
