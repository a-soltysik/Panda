#include "panda/ExternalBuffer.hpp"

#include <cstdint>
#include <panda/Buffer.hpp>
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

ExternalBuffer::ExternalBuffer(Buffer buffer,
                               ExternalMemoryHandle handle,
                               std::uint64_t allocationSize,
                               VulkanDeviceUuid deviceUuid) noexcept
    : _buffer {std::move(buffer)},
      _handle {std::move(handle)},
      _allocationSize {allocationSize},
      _deviceUuid {deviceUuid}
{
}

auto ExternalBuffer::buffer() const -> Buffer
{
    return _buffer;
}

auto ExternalBuffer::handle() noexcept -> ExternalMemoryHandle&
{
    return _handle;
}

auto ExternalBuffer::handle() const noexcept -> const ExternalMemoryHandle&
{
    return _handle;
}

auto ExternalBuffer::getSize() const noexcept -> std::uint64_t
{
    return _buffer.getSize();
}

auto ExternalBuffer::getAllocationSize() const noexcept -> std::uint64_t
{
    return _allocationSize;
}

auto ExternalBuffer::getDeviceUuid() const noexcept -> const VulkanDeviceUuid&
{
    return _deviceUuid;
}

auto ExternalBuffer::isValid() const noexcept -> bool
{
    return _buffer.isValid() && _handle.isValid();
}
}
