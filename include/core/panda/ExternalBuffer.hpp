#pragma once

/// @file
/// Vulkan buffer and platform handle exported for another GPU API.

#include <array>
#include <cstdint>
#include <panda/Buffer.hpp>
#include <vulkan/vulkan.hpp>

namespace panda
{
namespace detail
{
class ExternalContext;
}

/// @brief UUID bytes identifying one selected Vulkan device.
using VulkanDeviceUuid = std::array<std::uint8_t, VK_UUID_SIZE>;

/// @brief Native handle kind used to share an external buffer allocation.
enum class ExternalMemoryHandleType : std::uint8_t
{
    OpaqueFd,
    OpaqueWin32
};

/// @brief Move-only owner of one exported OS handle.
class ExternalMemoryHandle final
{
public:
    ExternalMemoryHandle() = default;
    ExternalMemoryHandle(const ExternalMemoryHandle&) = delete;
    auto operator=(const ExternalMemoryHandle&) -> ExternalMemoryHandle& = delete;
    /// @brief Transfers ownership of the exported OS handle.
    ExternalMemoryHandle(ExternalMemoryHandle&& other) noexcept;
    /// @brief Closes the current handle and takes ownership from other.
    auto operator=(ExternalMemoryHandle&& other) noexcept -> ExternalMemoryHandle&;
    ~ExternalMemoryHandle();

    /// @brief Reports whether the handle is valid.
    [[nodiscard]] auto isValid() const noexcept -> bool;
    /// @brief Returns a non-owning platform value; interpret it using getType().
    /// The value is usable only while this object owns the corresponding OS handle.
    [[nodiscard]] auto get() const noexcept -> std::intptr_t;
    /// @brief Returns the platform handle kind.
    [[nodiscard]] auto getType() const noexcept -> ExternalMemoryHandleType;
    /// @brief Releases ownership without closing the handle; caller assumes responsibility for the OS handle.
    [[nodiscard]] auto release() noexcept -> std::intptr_t;
    /// @brief Closes the owned handle, if any.
    void reset() noexcept;

private:
    friend class detail::ExternalContext;
    ExternalMemoryHandle(std::intptr_t value, ExternalMemoryHandleType type) noexcept;

    std::intptr_t _value {0};
    ExternalMemoryHandleType _type {ExternalMemoryHandleType::OpaqueFd};
    bool _valid {false};
};

/// @brief Move-only Vulkan buffer plus its exported allocation handle.
/// The handle identifies the dedicated allocation, not a synchronization primitive.
class ExternalBuffer final
{
public:
    ExternalBuffer() = default;
    ExternalBuffer(const ExternalBuffer&) = delete;
    auto operator=(const ExternalBuffer&) -> ExternalBuffer& = delete;
    /// @brief Transfers ownership of the buffer and its exported handle.
    ExternalBuffer(ExternalBuffer&& other) noexcept = default;
    /// @brief Releases the current buffer and handle, then takes ownership from other.
    auto operator=(ExternalBuffer&& other) noexcept -> ExternalBuffer& = default;
    ~ExternalBuffer() = default;

    /// @brief Returns an owning Vulkan buffer handle.
    [[nodiscard]] auto buffer() const -> Buffer;
    /// @brief Returns the exported platform allocation handle.
    [[nodiscard]] auto handle() noexcept -> ExternalMemoryHandle&;
    /// @brief Returns the exported platform allocation handle without mutable access.
    [[nodiscard]] auto handle() const noexcept -> const ExternalMemoryHandle&;
    /// @brief Returns the logical buffer size.
    [[nodiscard]] auto getSize() const noexcept -> std::uint64_t;
    /// @brief Returns the dedicated allocation size required by external import.
    [[nodiscard]] auto getAllocationSize() const noexcept -> std::uint64_t;
    /// @brief Returns the identity of the Vulkan device that owns this allocation.
    [[nodiscard]] auto getDeviceUuid() const noexcept -> const VulkanDeviceUuid&;
    /// @brief Reports whether this object owns a buffer and valid exported handle.
    [[nodiscard]] auto isValid() const noexcept -> bool;

private:
    friend class Context;
    friend class detail::ExternalContext;
    ExternalBuffer(Buffer buffer,
                   ExternalMemoryHandle handle,
                   std::uint64_t allocationSize,
                   VulkanDeviceUuid deviceUuid) noexcept;

    Buffer _buffer;
    ExternalMemoryHandle _handle;
    std::uint64_t _allocationSize {0};
    VulkanDeviceUuid _deviceUuid {};
};
}
