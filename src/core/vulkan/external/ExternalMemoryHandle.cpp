#include "panda/ExternalMemoryHandle.hpp"

#include <cstdint>
#include <utility>

#include "external/ScopedExternalHandle.hpp"

namespace panda
{
ExternalMemoryHandle::ExternalMemoryHandle(std::intptr_t value, ExternalMemoryHandleType type) noexcept
    : _value {value},
      _type {type},
      _valid {true}
{
}

ExternalMemoryHandle::ExternalMemoryHandle(ExternalMemoryHandle&& other) noexcept
    : _value {other._value},
      _type {other._type},
      _valid {std::exchange(other._valid, false)}
{
}

auto ExternalMemoryHandle::operator=(ExternalMemoryHandle&& other) noexcept -> ExternalMemoryHandle&
{
    if (this != &other)
    {
        reset();
        _value = other._value;
        _type = other._type;
        _valid = std::exchange(other._valid, false);
    }
    return *this;
}

ExternalMemoryHandle::~ExternalMemoryHandle()
{
    reset();
}

auto ExternalMemoryHandle::isValid() const noexcept -> bool
{
    return _valid;
}

auto ExternalMemoryHandle::get() const noexcept -> std::intptr_t
{
    return _value;
}

auto ExternalMemoryHandle::getType() const noexcept -> ExternalMemoryHandleType
{
    return _type;
}

auto ExternalMemoryHandle::release() noexcept -> std::intptr_t
{
    _valid = false;
    return _value;
}

void ExternalMemoryHandle::reset() noexcept
{
    if (_valid)
    {
        _valid = false;
        [[maybe_unused]] const auto scopedHandle = core::vulkan::external::ScopedExternalHandle {_value};
    }
}
}
