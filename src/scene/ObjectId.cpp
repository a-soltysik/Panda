#include <compare>
#include <cstdint>
#include <panda/scene/ObjectId.hpp>

namespace panda::scene
{
ObjectId::ObjectId(std::uint64_t value) noexcept
    : _value {value}
{
}

auto ObjectId::value() const noexcept -> std::uint64_t
{
    return _value;
}

auto ObjectId::operator==(const ObjectId& other) const noexcept -> bool
{
    return _value == other._value;
}

auto ObjectId::operator<=>(const ObjectId& other) const noexcept -> std::strong_ordering
{
    return _value <=> other._value;
}
}
