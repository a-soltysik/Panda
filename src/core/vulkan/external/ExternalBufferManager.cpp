// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
// clang-format on

#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <panda/base/Error.hpp>
#include <panda/interop/ExternalBuffer.hpp>
#include <panda/interop/ExternalBufferManager.hpp>
#include <panda/interop/ExternalTimelineValue.hpp>
#include <panda/resources/Buffer.hpp>

#include "external/ExternalBufferInterop.hpp"

namespace panda
{
ExternalBufferManager::ExternalBufferManager(
    std::optional<std::reference_wrapper<detail::ExternalBufferInterop>> implementation) noexcept
    : _implementation {implementation}
{
}

auto ExternalBufferManager::createBuffer(const BufferDesc& description) const -> Result<ExternalBuffer>
{
    if (!_implementation)
    {
        return std::unexpected {makeError(ErrorCode::Unsupported, "Engine has no external-buffer support")};
    }
    return _implementation->get().createBuffer(description);
}

auto ExternalBufferManager::releaseToExternal(const Buffer& buffer) -> Result<ExternalTimelineValue>
{
    if (!_implementation)
    {
        return std::unexpected {makeError(ErrorCode::Unsupported, "Engine has no external-buffer support")};
    }
    return _implementation->get().release(buffer).transform([](std::uint64_t value) {
        return ExternalTimelineValue {value};
    });
}

auto ExternalBufferManager::acquireFromExternal(const Buffer& buffer, ExternalTimelineValue externalSignalValue)
    -> Result<void>
{
    if (!_implementation)
    {
        return std::unexpected {makeError(ErrorCode::Unsupported, "Engine has no external-buffer support")};
    }
    return _implementation->get().acquire(buffer, externalSignalValue.getValue());
}
}
