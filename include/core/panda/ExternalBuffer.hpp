#pragma once

/// @file
/// Vulkan buffer and platform handle exported for another GPU API.

#include <array>
#include <cstdint>
#include <panda/Buffer.hpp>
#include <panda/Error.hpp>
#include <panda/ExternalMemoryHandle.hpp>
#include <panda/ExternalSemaphoreHandle.hpp>
#include <vulkan/vulkan.hpp>

namespace panda
{
namespace detail
{
class ExternalContext;
}

/// @brief UUID bytes identifying one selected Vulkan device.
using VulkanDeviceUuid = std::array<std::uint8_t, VK_UUID_SIZE>;

/// @brief Move-only Vulkan buffer plus exported allocation and optional semaphore handles.
/// The memory handle identifies the dedicated allocation. A semaphore handle, when
/// present, identifies a timeline semaphore but does not itself order buffer access.
class ExternalBuffer final
{
public:
    ExternalBuffer();
    ExternalBuffer(const ExternalBuffer&) = delete;
    auto operator=(const ExternalBuffer&) -> ExternalBuffer& = delete;
    /// @brief Transfers ownership of the buffer and its exported handle.
    ExternalBuffer(ExternalBuffer&& other) noexcept;
    /// @brief Releases the current buffer and handle, then takes ownership from other.
    auto operator=(ExternalBuffer&& other) noexcept -> ExternalBuffer&;
    ~ExternalBuffer();

    /// @brief Returns an owning Vulkan buffer handle.
    [[nodiscard]] auto getBuffer() const -> Buffer;
    /// @brief Returns the exported platform allocation handle.
    [[nodiscard]] auto getHandle() noexcept -> ExternalMemoryHandle&;
    /// @brief Returns the exported platform allocation handle without mutable access.
    [[nodiscard]] auto getHandle() const noexcept -> const ExternalMemoryHandle&;
    /// @brief Returns the paired timeline semaphore handle, or an invalid handle when unsupported.
    [[nodiscard]] auto getSemaphoreHandle() noexcept -> ExternalSemaphoreHandle&;
    /// @brief Returns the paired timeline semaphore handle without mutable access.
    [[nodiscard]] auto getSemaphoreHandle() const noexcept -> const ExternalSemaphoreHandle&;
    /// @brief Reports whether this buffer has an exportable timeline semaphore.
    [[nodiscard]] auto hasSemaphore() const noexcept -> bool;
    /// @brief Returns the logical buffer size.
    [[nodiscard]] auto getSize() const noexcept -> std::uint64_t;
    /// @brief Returns the dedicated allocation size required by external import.
    [[nodiscard]] auto getAllocationSize() const noexcept -> std::uint64_t;
    /// @brief Returns the identity of the Vulkan device that owns this allocation.
    [[nodiscard]] auto getDeviceUuid() const noexcept -> const VulkanDeviceUuid&;
    /// @brief Reports whether this object owns a buffer and valid exported handle.
    [[nodiscard]] auto isValid() const noexcept -> bool;

private:
    friend class detail::ExternalContext;
    friend class Context;

    Buffer _buffer;
    ExternalMemoryHandle _handle;
    ExternalSemaphoreHandle _semaphoreHandle;
    std::uint64_t _allocationSize {0};
    VulkanDeviceUuid _deviceUuid {};
};
}
