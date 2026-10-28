// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
// clang-format on

#include "panda/ExternalBuffer.hpp"

#include <cstdint>
#include <panda/Buffer.hpp>
#include <panda/ExternalMemoryHandle.hpp>
#include <panda/ExternalSemaphoreHandle.hpp>

#include "resources/BufferResource.hpp"

namespace panda
{
ExternalBuffer::ExternalBuffer() = default;

ExternalBuffer::ExternalBuffer(ExternalBuffer&& other) noexcept = default;

auto ExternalBuffer::operator=(ExternalBuffer&& other) noexcept -> ExternalBuffer& = default;

ExternalBuffer::~ExternalBuffer() = default;

auto ExternalBuffer::getBuffer() const -> Buffer
{
    return _buffer;
}

auto ExternalBuffer::getHandle() noexcept -> ExternalMemoryHandle&
{
    return _handle;
}

auto ExternalBuffer::getHandle() const noexcept -> const ExternalMemoryHandle&
{
    return _handle;
}

auto ExternalBuffer::getSemaphoreHandle() noexcept -> ExternalSemaphoreHandle&
{
    return _semaphoreHandle;
}

auto ExternalBuffer::getSemaphoreHandle() const noexcept -> const ExternalSemaphoreHandle&
{
    return _semaphoreHandle;
}

auto ExternalBuffer::hasSemaphore() const noexcept -> bool
{
    return _buffer.isValid() && _semaphoreHandle.isValid() &&
           detail::BufferResource::getResource(_buffer)->getExternalSemaphore() != nullptr;
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
