#include "panda/ExternalSemaphoreHandle.hpp"

#include <cstdint>
#include <utility>

#include "external/ScopedExternalHandle.hpp"

namespace panda
{
ExternalSemaphoreHandle::ExternalSemaphoreHandle(std::intptr_t value, ExternalSemaphoreHandleType type) noexcept
    : _value {value},
      _type {type},
      _valid {true}
{
}

ExternalSemaphoreHandle::ExternalSemaphoreHandle(ExternalSemaphoreHandle&& other) noexcept
    : _value {other._value},
      _type {other._type},
      _valid {std::exchange(other._valid, false)}
{
}

auto ExternalSemaphoreHandle::operator=(ExternalSemaphoreHandle&& other) noexcept -> ExternalSemaphoreHandle&
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

ExternalSemaphoreHandle::~ExternalSemaphoreHandle()
{
    reset();
}

auto ExternalSemaphoreHandle::isValid() const noexcept -> bool
{
    return _valid;
}

auto ExternalSemaphoreHandle::get() const noexcept -> std::intptr_t
{
    return _value;
}

auto ExternalSemaphoreHandle::getType() const noexcept -> ExternalSemaphoreHandleType
{
    return _type;
}

auto ExternalSemaphoreHandle::release() noexcept -> std::intptr_t
{
    _valid = false;
    return _value;
}

void ExternalSemaphoreHandle::reset() noexcept
{
    if (_valid)
    {
        _valid = false;
        [[maybe_unused]] const auto scopedHandle = core::vulkan::external::ScopedExternalHandle {_value};
    }
}
}
