#pragma once

/// @file
/// Move-only owner of an exported external-memory OS handle.

#include <cstdint>

namespace panda
{
namespace detail
{
class ExternalContext;
}

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
}
