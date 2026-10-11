// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
// clang-format on

#include <cstdint>
#include <memory>
#include <panda/Buffer.hpp>
#include <utility>

#include "vulkan/resources/BufferResource.hpp"

namespace panda
{
Buffer::Buffer() = default;
Buffer::Buffer(const Buffer& other) = default;
auto Buffer::operator=(const Buffer& other) -> Buffer& = default;
Buffer::Buffer(Buffer&& other) noexcept = default;
auto Buffer::operator=(Buffer&& other) noexcept -> Buffer& = default;
Buffer::~Buffer() = default;

Buffer::Buffer(std::shared_ptr<detail::BufferResource> resource)
    : _resource {std::move(resource)}
{
}

auto Buffer::isValid() const noexcept -> bool
{
    return _resource != nullptr;
}

auto Buffer::getSize() const noexcept -> std::uint64_t
{
    return _resource ? _resource->getSize() : 0;
}

void Buffer::reset() noexcept
{
    _resource.reset();
}
}
