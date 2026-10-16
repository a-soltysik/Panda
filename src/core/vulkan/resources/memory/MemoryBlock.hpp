#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <panda/Error.hpp>
#include <span>

#include "VulkanHpp.hpp"
#include "resources/memory/RangeAllocator.hpp"

namespace panda::core::vulkan::resources
{

// One non-exportable Vulkan allocation. The device outlives this owner.
// Callers serialize operations and finish GPU use/destroy bound resources before
// releasing a range. Borrowed mapped views expire on release or block destruction.
class MemoryBlock final
{
public:
    [[nodiscard]] static auto create(const vk::raii::PhysicalDevice& physicalDevice,
                                     const vk::raii::Device& device,
                                     std::uint64_t size,
                                     std::uint32_t memoryTypeIndex) -> Result<std::unique_ptr<MemoryBlock>>;

    [[nodiscard]] static auto createDedicated(const vk::raii::PhysicalDevice& physicalDevice,
                                              const vk::raii::Device& device,
                                              vk::MemoryAllocateInfo allocation,
                                              const vk::MemoryDedicatedAllocateInfo& resource)
        -> Result<std::unique_ptr<MemoryBlock>>;

    MemoryBlock(const MemoryBlock&) = delete;
    auto operator=(const MemoryBlock&) -> MemoryBlock& = delete;
    MemoryBlock(MemoryBlock&&) = delete;
    auto operator=(MemoryBlock&&) -> MemoryBlock& = delete;
    ~MemoryBlock();

    // Returned size includes atom padding. It must be passed unchanged to release.
    [[nodiscard]] auto allocate(std::uint64_t size, std::uint64_t alignment) -> Result<MemoryRange>;
    [[nodiscard]] auto release(MemoryRange range) -> Result<void>;
    [[nodiscard]] auto getMappedBytes(MemoryRange range) const -> Result<std::span<std::byte>>;
    // Relative byte ranges are rounded to whole atoms inside the live allocation.
    // These operations establish host cache visibility, never GPU completion.
    [[nodiscard]] auto flush(MemoryRange range, std::uint64_t offset, std::uint64_t size) const -> Result<void>;
    [[nodiscard]] auto invalidate(MemoryRange range, std::uint64_t offset, std::uint64_t size) const -> Result<void>;
    [[nodiscard]] auto getMemory() const -> vk::DeviceMemory;
    [[nodiscard]] auto getFreeSize() const -> std::uint64_t;
    [[nodiscard]] auto getCapacity() const -> std::uint64_t;

private:
    struct Settings
    {
        std::uint64_t size;
        std::uint64_t atomSize;
        bool needsCacheManagement;
        bool dedicated;
    };

    [[nodiscard]] static auto createInternal(const vk::raii::PhysicalDevice& physicalDevice,
                                             const vk::raii::Device& device,
                                             const vk::MemoryAllocateInfo& info)
        -> Result<std::unique_ptr<MemoryBlock>>;

    MemoryBlock(const vk::raii::Device& device, vk::raii::DeviceMemory memory, Settings settings);

    [[nodiscard]] auto map() -> Result<void>;
    [[nodiscard]] auto mappedRange(MemoryRange range, std::uint64_t offset, std::uint64_t size) const
        -> Result<vk::MappedMemoryRange>;

    const vk::raii::Device& _device;
    vk::raii::DeviceMemory _memory;
    RangeAllocator _ranges;
    std::uint64_t _atomSize;
    bool _needsCacheManagement;
    bool _dedicated;
    std::span<std::byte> _mapped;
};

}
