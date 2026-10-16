// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include <cstdint>
#include <exception>
#include <expected>
#include <memory>
#include <panda/Error.hpp>

#include "ScopedMock.hpp"
#include "panda/core/MemoryPoolMock.hpp"
#include "resources/memory/MemoryAllocation.hpp"
#include "resources/memory/MemoryPool.hpp"

namespace panda::core::vulkan::resources
{
auto MemoryPool::create(const vk::raii::PhysicalDevice& physicalDevice, const vk::raii::Device& device)
    -> std::unique_ptr<MemoryPool>
{
    return std::unique_ptr<MemoryPool> {
        new MemoryPool {physicalDevice, device}
    };
}

MemoryPool::MemoryPool(const vk::raii::PhysicalDevice& physicalDevice, const vk::raii::Device& device)
    : _physicalDevice {physicalDevice},
      _device {device},
      _memoryProperties {physicalDevice.getMemoryProperties()}
{
    const auto properties =
        physicalDevice.getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceMaintenance3Properties>();
    _limits = properties.get<vk::PhysicalDeviceProperties2>().properties.limits;
    _maximumAllocationSize = properties.get<vk::PhysicalDeviceMaintenance3Properties>().maxMemoryAllocationSize;
    _heapCommitted.resize(_memoryProperties.memoryHeapCount);
}

MemoryPool::~MemoryPool()
{
    try
    {
        test::ScopedMock<test::MemoryPoolMock>::getActiveMock().destroy(*this);
    }
    catch (...)
    {
        std::terminate();
    }
}

auto MemoryPool::allocate(const MemoryRequest& request) -> Result<MemoryAllocation>
{
    auto& mock = test::ScopedMock<test::MemoryPoolMock>::getActiveMock();
    auto id = mock.allocate(*this, request);
    if (!id)
    {
        return std::unexpected {id.error()};
    }
    return MemoryAllocation {*this, *id};
}

auto MemoryPool::getStats() const -> const MemoryPoolStats&
{
    return test::ScopedMock<test::MemoryPoolMock>::getActiveMock().getStats(*this);
}

void MemoryPool::release(std::uint64_t id) noexcept
{
    try
    {
        test::ScopedMock<test::MemoryPoolMock>::getActiveMock().release(*this, id);
    }
    catch (...)
    {
        std::terminate();
    }
}
}
