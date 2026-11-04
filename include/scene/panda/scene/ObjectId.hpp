#pragma once

/// @file
/// Scene-scoped object identifiers.

#include <compare>
#include <cstdint>

namespace panda::scene
{
namespace detail
{
class SceneState;
}

/// @brief 64-bit identifier for one object, unique within a Scene's lifetime.
/// Serialization is not implemented; a project-wide ID encoding will be defined with persistence.
class ObjectId final
{
public:
    /// @brief Creates an identifier from its 64-bit value.
    explicit ObjectId(std::uint64_t value) noexcept;

    /// @brief Returns the identifier's 64-bit value.
    [[nodiscard]] auto value() const noexcept -> std::uint64_t;

    /// @brief Compares identifier values.
    [[nodiscard]] auto operator==(const ObjectId& other) const noexcept -> bool;

    /// @brief Orders identifier values numerically.
    [[nodiscard]] auto operator<=>(const ObjectId& other) const noexcept -> std::strong_ordering;

private:
    friend class detail::SceneState;

    std::uint64_t _value {0};
};
}
