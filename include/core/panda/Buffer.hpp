#pragma once

/// @file
/// Shared ownership of fixed-size Vulkan buffers.

#include <cstdint>
#include <memory>
#include <vulkan/vulkan.hpp>

namespace panda
{
class Context;

namespace detail
{
class BufferResource;
class ExternalContext;
}

/// @brief Allocation intent; does not grant CPU access or synchronize GPU use.
enum class BufferMemory : std::uint8_t
{
    /// Require device-local memory.
    Device,
    /// Require host-visible memory and prefer host coherence.
    Upload,
    /// Require host-visible memory and prefer host caching.
    Readback
};

/// @brief Ordinary non-exportable buffer, initially containing undefined bytes.
struct BufferDesc
{
    /// Fixed payload size in bytes; must be nonzero.
    std::uint64_t size {0};
    /// Nonzero Vulkan transfer, vertex, index, indirect, uniform/storage or texel usage.
    /// Device-address and extension usages are currently unsupported.
    vk::BufferUsageFlags usage;
    /// Required device-local or host-visible memory, with intent-specific preferences.
    BufferMemory memory {BufferMemory::Device};
};

/// @brief Copyable resource owner. Context outlives every copy.
/// Creation, native access and final release are serialized with Context operations.
/// Copies retain the buffer; they do not synchronize access. No GPU work is submitted
/// by this handle. The last owner releases the resource without a device-wide wait.
class Buffer final
{
public:
    /// @brief Creates an empty handle.
    Buffer();
    /// @brief Retains the same resource without copying its bytes.
    Buffer(const Buffer& other);
    /// @brief Releases the previous resource and retains other's resource.
    auto operator=(const Buffer& other) -> Buffer&;
    /// @brief Transfers ownership and leaves other empty.
    Buffer(Buffer&& other) noexcept;
    /// @brief Releases the previous resource and transfers ownership.
    auto operator=(Buffer&& other) noexcept -> Buffer&;
    /// @brief Releases one owner; the last owner destroys the native buffer then its allocation.
    ~Buffer();
    /// @brief Reports whether this handle retains a resource.
    [[nodiscard]] auto isValid() const noexcept -> bool;
    /// @brief Returns payload bytes, or zero for an empty handle.
    [[nodiscard]] auto getSize() const noexcept -> std::uint64_t;
    /// @brief Releases this owner and leaves the handle empty; does not wait for GPU work.
    void reset() noexcept;

private:
    friend class Context;
    friend class detail::BufferResource;
    friend class detail::ExternalContext;
    explicit Buffer(std::shared_ptr<detail::BufferResource> resource);
    std::shared_ptr<detail::BufferResource> _resource;
};
}
