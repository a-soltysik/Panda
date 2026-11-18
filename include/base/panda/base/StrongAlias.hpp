#pragma once

/// @file
/// Template for distinct, explicitly constructed value types.

#include <type_traits>
#include <utility>

namespace panda
{
/// @brief Wraps a value in a distinct type selected by its tag.
/// @tparam Value The underlying value type.
/// @tparam Tag A distinct type that separates aliases with the same underlying value type.
template <typename Value, typename Tag>
class StrongAlias final
{
public:
    /// @brief Creates the alias from an underlying value.
    explicit StrongAlias(Value value) noexcept(std::is_nothrow_move_constructible_v<Value>)
        : _value {std::move(value)}
    {
    }

    /// @brief Returns the underlying value.
    [[nodiscard]] auto getValue() const noexcept -> const Value&
    {
        return _value;
    }

    /// @brief Compares aliases with the same value type and tag.
    auto operator==(const StrongAlias&) const -> bool = default;

private:
    Value _value;
};
}
