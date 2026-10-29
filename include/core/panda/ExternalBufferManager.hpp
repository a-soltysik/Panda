#pragma once

/// @file
/// Context-owned external-buffer creation and GPU handoff operations.

#include <cstdint>
#include <functional>
#include <optional>
#include <panda/Buffer.hpp>
#include <panda/Error.hpp>
#include <panda/ExternalBuffer.hpp>
#include <panda/ExternalTimelineValue.hpp>

namespace panda
{
class Context;

namespace detail
{
class ExternalContext;
}

/// @brief Creates external buffers and queues ownership handoffs for one Context.
/// This facade is borrowed from Context and must not outlive it. Calls are serialized
/// with other operations on the owning Context.
class ExternalBufferManager final
{
public:
    ExternalBufferManager(const ExternalBufferManager&) = delete;
    auto operator=(const ExternalBufferManager&) -> ExternalBufferManager& = delete;
    ExternalBufferManager(ExternalBufferManager&&) = delete;
    auto operator=(ExternalBufferManager&&) -> ExternalBufferManager& = delete;
    // NOLINTNEXTLINE(performance-trivially-destructible): Function definitions stay in .cpp files by convention.
    ~ExternalBufferManager() noexcept;

    /// @brief Creates a dedicated device-local buffer with exported platform handles.
    /// @return The owning buffer, or an unsupported/creation error.
    [[nodiscard]] auto createBuffer(const BufferDesc& description) const -> Result<ExternalBuffer>;

    /// @brief Releases a Buffer view to an external GPU API and signals its timeline semaphore.
    /// @param buffer An external buffer view returned by its owning adapter.
    /// @return The timeline value that the external API must wait for, or an error before submission.
    /// The returned value leaves room for the external API's next signal value.
    /// The queue operation does not wait on the CPU. No unsubmitted upload batch may be alive.
    [[nodiscard]] auto releaseToExternal(const Buffer& buffer) -> Result<ExternalTimelineValue>;

    /// @brief Queues a wait for the external timeline signal and reacquires Vulkan ownership.
    /// @param buffer The same external Buffer view passed to releaseToExternal.
    /// @param externalSignalValue The next value returned by external GPU work.
    /// @return Success once the wait and ownership barrier are submitted, or an error before submission.
    /// Later Vulkan queue work follows the acquire without a CPU wait.
    [[nodiscard]] auto acquireFromExternal(const Buffer& buffer, ExternalTimelineValue externalSignalValue)
        -> Result<void>;

private:
    friend class Context;
    explicit ExternalBufferManager(
        std::optional<std::reference_wrapper<detail::ExternalContext>> implementation) noexcept;

    std::optional<std::reference_wrapper<detail::ExternalContext>> _implementation;
};
}
