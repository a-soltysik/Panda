// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <panda/Buffer.hpp>
#include <panda/Error.hpp>
#include <span>
#include <utility>

#include "ScopedMock.hpp"
#include "panda/core/BufferResourceMock.hpp"
#include "resources/memory/MemoryAllocation.hpp"
#include "resources/memory/MemoryPool.hpp"

namespace panda::detail
{
auto BufferResource::getResource(const Buffer& buffer) -> const std::shared_ptr<BufferResource>&
{
    return buffer._resource;
}

auto BufferResource::create(const vk::raii::Device& device,
                            buffer_resources::MemoryPool& pool,
                            const BufferDesc& description) -> Result<std::shared_ptr<BufferResource>>
{
    auto& mock = test::ScopedMock<test::BufferResourceMock>::getActiveMock();
    const auto created = mock.create(device, pool, description);
    if (!created)
    {
        return std::unexpected {created.error()};
    }
    return std::shared_ptr<BufferResource> {
        new BufferResource {pool, vk::raii::Buffer {nullptr}, description}
    };
}

BufferResource::BufferResource(buffer_resources::MemoryPool& pool,
                               vk::raii::Buffer buffer,
                               const BufferDesc& description)
    : _pool {pool},
      _buffer {std::move(buffer)},
      _size {description.size},
      _usage {description.usage},
      _intent {description.memory}
{
}

auto BufferResource::getSize() const -> std::uint64_t
{
    return test::ScopedMock<test::BufferResourceMock>::getActiveMock().getSize(*this);
}

auto BufferResource::getReservedSize() const -> std::uint64_t
{
    return test::ScopedMock<test::BufferResourceMock>::getActiveMock().getReservedSize(*this);
}

auto BufferResource::getNative() const -> vk::Buffer
{
    return test::ScopedMock<test::BufferResourceMock>::getActiveMock().getNative(*this);
}

auto BufferResource::getUsage() const -> vk::BufferUsageFlags
{
    return test::ScopedMock<test::BufferResourceMock>::getActiveMock().getUsage(*this);
}

auto BufferResource::belongsTo(const buffer_resources::MemoryPool& pool) const -> bool
{
    return test::ScopedMock<test::BufferResourceMock>::getActiveMock().belongsTo(*this, pool);
}

auto BufferResource::getMappedBytes() const -> Result<std::span<std::byte>>
{
    return test::ScopedMock<test::BufferResourceMock>::getActiveMock().getMappedBytes(*this);
}

auto BufferResource::writeMapped(std::uint64_t offset, std::span<const std::byte> bytes) const -> Result<void>
{
    return test::ScopedMock<test::BufferResourceMock>::getActiveMock().writeMapped(*this, offset, bytes);
}

auto BufferResource::invalidate(std::uint64_t offset, std::uint64_t size) const -> Result<void>
{
    return test::ScopedMock<test::BufferResourceMock>::getActiveMock().invalidate(*this, offset, size);
}
}
