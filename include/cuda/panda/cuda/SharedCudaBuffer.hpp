#pragma once

#include <cstdint>
#include <memory>

namespace panda
{
class Buffer;
}

namespace panda::cuda
{
class CudaSession;

namespace detail
{
class SharedCudaBufferState;
}

/// @brief Vulkan buffer with a retained CUDA memory import and mapped allocation.
/// Context outlives every copy. Available paired timeline semaphores are also
/// imported and retained. This object does not synchronize GPU use.
class SharedCudaBuffer final
{
public:
    SharedCudaBuffer();
    /// @brief Shares the imported resource retained by another handle.
    /// @param other Handle whose resource state is shared.
    SharedCudaBuffer(const SharedCudaBuffer& other);
    /// @brief Shares the imported resource retained by another handle.
    /// @param other Handle whose resource state is shared.
    /// @return This handle.
    auto operator=(const SharedCudaBuffer& other) -> SharedCudaBuffer&;
    /// @brief Transfers the resource state and leaves the source empty.
    /// @param other Handle to move from.
    SharedCudaBuffer(SharedCudaBuffer&& other) noexcept;
    /// @brief Transfers the resource state and leaves the source empty.
    /// @param other Handle to move from.
    /// @return This handle.
    auto operator=(SharedCudaBuffer&& other) noexcept -> SharedCudaBuffer&;
    ~SharedCudaBuffer();

    /// @brief Returns an owning Vulkan view, or an empty handle for an empty object.
    [[nodiscard]] auto getBuffer() const -> panda::Buffer;
    /// @brief Returns the logical buffer size, or zero for an empty object.
    [[nodiscard]] auto getSize() const noexcept -> std::uint64_t;
    /// @brief Reports whether this handle retains a shared resource.
    [[nodiscard]] auto isValid() const noexcept -> bool;

private:
    friend class CudaSession;
    explicit SharedCudaBuffer(std::shared_ptr<detail::SharedCudaBufferState> state);

    std::shared_ptr<detail::SharedCudaBufferState> _state;
};
}
