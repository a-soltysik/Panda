// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
// clang-format on

#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <panda/Buffer.hpp>
#include <panda/Error.hpp>
#include <panda/ExternalBuffer.hpp>
#include <panda/ExternalBufferManager.hpp>
#include <panda/ExternalTimelineValue.hpp>

#include "external/ExternalContext.hpp"

namespace panda
{
ExternalBufferManager::ExternalBufferManager(
    std::optional<std::reference_wrapper<detail::ExternalContext>> implementation) noexcept
    : _implementation {implementation}
{
}

ExternalBufferManager::~ExternalBufferManager() noexcept = default;

auto ExternalBufferManager::createBuffer(const BufferDesc& description) const -> Result<ExternalBuffer>
{
    if (!_implementation)
    {
        return std::unexpected {makeError(ErrorCode::Unsupported, "Context has no external-buffer support")};
    }
    return _implementation->get().createBuffer(description);
}

auto ExternalBufferManager::releaseToExternal(const Buffer& buffer) -> Result<ExternalTimelineValue>
{
    if (!_implementation)
    {
        return std::unexpected {makeError(ErrorCode::Unsupported, "Context has no external-buffer support")};
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
        return std::unexpected {makeError(ErrorCode::Unsupported, "Context has no external-buffer support")};
    }
    return _implementation->get().acquire(buffer, externalSignalValue.getValue());
}
}
