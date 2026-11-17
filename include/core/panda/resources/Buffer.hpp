#pragma once

/// @file
/// Shared ownership of fixed-size buffers.

#include <bit>
#include <cstdint>
#include <memory>

namespace panda
{
class ResourceManager;

namespace detail
{
class BufferResource;
class ExternalBufferInterop;
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

/// @brief Supported operations for a public buffer.
enum class BufferUsage : std::uint8_t
{
    /// No operation; buffer creation rejects this value.
    None = 0,
    /// Buffer may be read by a transfer operation.
    TransferSource = 1U << 0U,
    /// Buffer may be a transfer destination.
    TransferDestination = 1U << 1U,
    /// Buffer may be used as storage by an external compute API.
    Storage = 1U << 2U,
};

/// @brief Ordinary non-exportable buffer, initially containing undefined bytes.
struct BufferDesc
{
    /// Fixed payload size in bytes; must be nonzero.
    std::uint64_t size {0};
    /// Nonzero combination of supported buffer operations.
    BufferUsage usage {BufferUsage::None};
    /// Required device-local or host-visible memory, with intent-specific preferences.
    BufferMemory memory {BufferMemory::Device};
};

/// @brief Copyable resource owner. Engine outlives every copy.
/// Creation, native access and final release are serialized with Engine operations.
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
    friend class ResourceManager;
    friend class detail::BufferResource;
    friend class detail::ExternalBufferInterop;
    explicit Buffer(std::shared_ptr<detail::BufferResource> resource);
    std::shared_ptr<detail::BufferResource> _resource;
};

/// @brief Combines two buffer operation flags.
[[nodiscard]] constexpr auto operator|(BufferUsage left, BufferUsage right) noexcept -> BufferUsage
{
    return std::bit_cast<BufferUsage>(
        static_cast<std::uint8_t>(static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right)));
}

/// @brief Returns the buffer operation flags present in both values.
[[nodiscard]] constexpr auto operator&(BufferUsage left, BufferUsage right) noexcept -> BufferUsage
{
    return std::bit_cast<BufferUsage>(
        static_cast<std::uint8_t>(static_cast<std::uint32_t>(left) & static_cast<std::uint32_t>(right)));
}

/// @brief Adds buffer operation flags to the left-hand value.
constexpr auto operator|=(BufferUsage& left, BufferUsage right) noexcept -> BufferUsage&
{
    left = left | right;
    return left;
}
}
