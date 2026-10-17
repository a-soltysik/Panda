// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <panda/Error.hpp>
#include <span>
#include <utility>

#include "ScopedMock.hpp"
#include "panda/core/MemoryBlockMock.hpp"
#include "resources/memory/MemoryBlock.hpp"
#include "resources/memory/RangeAllocator.hpp"

namespace panda::core::vulkan::resources
{
auto MemoryBlock::create(const vk::raii::PhysicalDevice& physicalDevice,
                         const vk::raii::Device& device,
                         std::uint64_t size,
                         std::uint32_t memoryTypeIndex) -> Result<std::unique_ptr<MemoryBlock>>
{
    auto& mock = test::ScopedMock<test::MemoryBlockMock>::getActiveMock();
    const auto created = mock.create(physicalDevice, device, size, memoryTypeIndex);
    if (!created)
    {
        return std::unexpected {created.error()};
    }
    return std::unique_ptr<MemoryBlock> {
        new MemoryBlock {device,
                         vk::raii::DeviceMemory {nullptr},
                         {.size = size, .atomSize = 1, .needsCacheManagement = false, .dedicated = false}}
    };
}

auto MemoryBlock::createDedicated(const vk::raii::PhysicalDevice& physicalDevice,
                                  const vk::raii::Device& device,
                                  vk::MemoryAllocateInfo allocation,
                                  const vk::MemoryDedicatedAllocateInfo& resource)
    -> Result<std::unique_ptr<MemoryBlock>>
{
    auto& mock = test::ScopedMock<test::MemoryBlockMock>::getActiveMock();
    const auto created = mock.createDedicated(physicalDevice, device, allocation, resource);
    if (!created)
    {
        return std::unexpected {created.error()};
    }
    return std::unique_ptr<MemoryBlock> {
        new MemoryBlock {
                         device, vk::raii::DeviceMemory {nullptr},
                         {.size = allocation.allocationSize, .atomSize = 1, .needsCacheManagement = false, .dedicated = true}}
    };
}

MemoryBlock::MemoryBlock(const vk::raii::Device& device, vk::raii::DeviceMemory memory, Settings settings)
    : _device {device},
      _memory {std::move(memory)},
      _ranges {settings.size},
      _atomSize {settings.atomSize},
      _needsCacheManagement {settings.needsCacheManagement},
      _dedicated {settings.dedicated}
{
}

MemoryBlock::~MemoryBlock() = default;

// This replacement must preserve the production member signature, which mutates
// the allocator; the mock itself records the call externally.
// NOLINTNEXTLINE(readability-make-member-function-const)
auto MemoryBlock::allocate(std::uint64_t size, std::uint64_t alignment) -> Result<MemoryRange>
{
    return test::ScopedMock<test::MemoryBlockMock>::getActiveMock().allocate(*this, size, alignment);
}

// NOLINTNEXTLINE(readability-make-member-function-const)
auto MemoryBlock::release(MemoryRange range) -> Result<void>
{
    return test::ScopedMock<test::MemoryBlockMock>::getActiveMock().release(*this, range);
}

auto MemoryBlock::getMappedBytes(MemoryRange range) const -> Result<std::span<std::byte>>
{
    return test::ScopedMock<test::MemoryBlockMock>::getActiveMock().getMappedBytes(*this, range);
}

auto MemoryBlock::flush(MemoryRange range, std::uint64_t offset, std::uint64_t size) const -> Result<void>
{
    return test::ScopedMock<test::MemoryBlockMock>::getActiveMock().flush(*this, range, offset, size);
}

auto MemoryBlock::invalidate(MemoryRange range, std::uint64_t offset, std::uint64_t size) const -> Result<void>
{
    return test::ScopedMock<test::MemoryBlockMock>::getActiveMock().invalidate(*this, range, offset, size);
}

auto MemoryBlock::getMemory() const -> vk::DeviceMemory
{
    return test::ScopedMock<test::MemoryBlockMock>::getActiveMock().getMemory(*this);
}

auto MemoryBlock::getFreeSize() const -> std::uint64_t
{
    return test::ScopedMock<test::MemoryBlockMock>::getActiveMock().getFreeSize(*this);
}

auto MemoryBlock::getCapacity() const -> std::uint64_t
{
    return test::ScopedMock<test::MemoryBlockMock>::getActiveMock().getCapacity(*this);
}
}
