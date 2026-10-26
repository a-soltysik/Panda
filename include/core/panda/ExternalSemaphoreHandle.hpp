#pragma once

/// @file
/// Move-only owner of an exported external-semaphore OS handle.

#include <cstdint>

namespace panda
{
namespace detail
{
class ExternalContext;
}

/// @brief Native handle kind used to share an external timeline semaphore.
enum class ExternalSemaphoreHandleType : std::uint8_t
{
    OpaqueFd,
    OpaqueWin32
};

/// @brief Move-only owner of an exported external semaphore OS handle.
class ExternalSemaphoreHandle final
{
public:
    ExternalSemaphoreHandle() = default;
    ExternalSemaphoreHandle(const ExternalSemaphoreHandle&) = delete;
    auto operator=(const ExternalSemaphoreHandle&) -> ExternalSemaphoreHandle& = delete;
    /// @brief Transfers ownership of the exported OS handle.
    ExternalSemaphoreHandle(ExternalSemaphoreHandle&& other) noexcept;
    /// @brief Closes the current handle and takes ownership from other.
    auto operator=(ExternalSemaphoreHandle&& other) noexcept -> ExternalSemaphoreHandle&;
    ~ExternalSemaphoreHandle();

    /// @brief Reports whether the handle is valid.
    [[nodiscard]] auto isValid() const noexcept -> bool;
    /// @brief Returns a non-owning platform value; interpret it using getType().
    /// The value is usable only while this object owns the corresponding OS handle.
    [[nodiscard]] auto get() const noexcept -> std::intptr_t;
    /// @brief Returns the platform handle kind.
    [[nodiscard]] auto getType() const noexcept -> ExternalSemaphoreHandleType;
    /// @brief Releases ownership without closing the handle; caller assumes responsibility for the OS handle.
    [[nodiscard]] auto release() noexcept -> std::intptr_t;
    /// @brief Closes the owned handle, if any.
    void reset() noexcept;

private:
    friend class detail::ExternalContext;
    ExternalSemaphoreHandle(std::intptr_t value, ExternalSemaphoreHandleType type) noexcept;

    std::intptr_t _value {0};
    ExternalSemaphoreHandleType _type {ExternalSemaphoreHandleType::OpaqueFd};
    bool _valid {false};
};
}
