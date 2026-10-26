#pragma once

/// @file
/// Optional CUDA Runtime availability query.

#include <cstdint>
#include <expected>
#include <memory>
#include <panda/Error.hpp>
#include <panda/ExternalBuffer.hpp>

namespace panda
{
class Buffer;
class Context;
}

namespace panda::cuda
{

namespace detail
{
struct CudaSessionState;
struct SharedCudaBufferState;
}

/// @brief CUDA availability on this machine at the time of the query.
enum class Availability : std::uint8_t
{
    Available,
    NoDriver,
    NoDevice
};

/// @brief Unexpected CUDA Runtime initialization failure.
struct Error
{
    /// Native cudaError_t value for diagnostic reporting.
    std::int32_t nativeCode;
};

/// @brief Checks whether CUDA Runtime can see a usable device.
/// This may initialize the CUDA Runtime and block; it does not create a Panda GPU
/// session or choose a device for Vulkan interoperability.
/// @return Availability for ordinary absence, or the native status of another
/// initialization failure. A successful result does not guarantee later launches.
[[nodiscard]] auto queryAvailability() noexcept -> std::expected<Availability, Error>;

/// @brief Vulkan buffer with one retained CUDA memory import and mapped allocation.
/// Context outlives every copy. Available paired timeline semaphores are also
/// imported and retained. This object does not synchronize GPU use.
class SharedCudaBuffer final
{
public:
    SharedCudaBuffer() = default;
    /// @brief Shares the imported mapping and keeps its session alive.
    SharedCudaBuffer(const SharedCudaBuffer&) = default;
    /// @brief Releases this reference and shares other's imported mapping.
    auto operator=(const SharedCudaBuffer&) -> SharedCudaBuffer& = default;
    /// @brief Transfers this reference to the imported mapping.
    SharedCudaBuffer(SharedCudaBuffer&&) noexcept = default;
    /// @brief Releases this reference and transfers ownership from other.
    auto operator=(SharedCudaBuffer&&) noexcept -> SharedCudaBuffer& = default;
    ~SharedCudaBuffer() = default;

    /// @brief Returns an owning Vulkan view, or an empty handle for an empty object.
    [[nodiscard]] auto buffer() const -> panda::Buffer;
    /// @brief Returns the logical buffer size, or zero for an empty object.
    [[nodiscard]] auto getSize() const noexcept -> std::uint64_t;
    /// @brief Reports whether this handle retains a shared resource.
    [[nodiscard]] auto isValid() const noexcept -> bool;

private:
    friend class CudaSession;
    explicit SharedCudaBuffer(std::shared_ptr<detail::SharedCudaBufferState> state);
    std::shared_ptr<detail::SharedCudaBufferState> _state;
};

/// @brief Move-only CUDA session matched to one Context's selected Vulkan device.
/// The Context outlives the session and every shared buffer created from it.
class CudaSession final
{
public:
    /// @brief Matches the selected Vulkan device and creates one non-blocking stream.
    /// @return A session, or an actionable Unsupported/initialization error.
    [[nodiscard]] static auto create(const panda::Context& context) -> Result<CudaSession>;

    CudaSession(const CudaSession&) = delete;
    auto operator=(const CudaSession&) -> CudaSession& = delete;
    /// @brief Transfers ownership of the session facade.
    CudaSession(CudaSession&&) noexcept = default;
    /// @brief Closes this facade and takes ownership from other.
    auto operator=(CudaSession&&) noexcept -> CudaSession& = default;
    ~CudaSession() = default;

    /// @brief Imports one Panda-owned dedicated external buffer and its optional paired semaphore.
    /// @return A shared owner, or an error. Unsupported usages and memory/semaphore handle
    /// combinations are reported; no CPU copy is used. Importing the semaphore does not
    /// synchronize with Vulkan.
    [[nodiscard]] auto importBuffer(panda::ExternalBuffer buffer) -> Result<SharedCudaBuffer>;

    /// @brief Closes this facade when no SharedCudaBuffer retains the session.
    /// @return `ResourcesInUse` while any shared buffer still needs its mapping or stream.
    [[nodiscard]] auto close() -> Result<void>;

private:
    explicit CudaSession(std::shared_ptr<detail::CudaSessionState> state);
    std::shared_ptr<detail::CudaSessionState> _state;
};

}
