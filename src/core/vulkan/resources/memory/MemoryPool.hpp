#pragma once

#include <cstdint>
#include <list>
#include <map>
#include <memory>
#include <panda/Error.hpp>
#include <vector>

#include "VulkanHpp.hpp"
#include "resources/memory/MemoryAllocation.hpp"
#include "resources/memory/MemoryBlock.hpp"
#include "resources/memory/RangeAllocator.hpp"

namespace panda::core::vulkan::resources
{
struct MemoryRequest
{
    vk::MemoryRequirements requirements {};
    vk::MemoryDedicatedRequirements dedicated {};
    // Exactly one handle, owned by the caller. Images must be optimal-tiled,
    // non-sparse and non-disjoint; this allocator does not import/export memory.
    vk::MemoryDedicatedAllocateInfo resource {};
    vk::MemoryPropertyFlags requiredProperties;
    vk::MemoryPropertyFlags preferredProperties;
};

struct MemoryPoolStats
{
    std::uint32_t allocationCount {0};
    std::uint64_t committedBytes {0};
    std::uint64_t liveBytes {0};
};

// One manager for all Panda memory on a device. Device and physical device
// outlive it; allocation owners outlive bound-resource use but not this pool.
// Serialized use. No relocation, GPU waits, submission or retirement scheduler.
class MemoryPool final
{
public:
    [[nodiscard]] static auto create(const vk::raii::PhysicalDevice& physicalDevice, const vk::raii::Device& device)
        -> std::unique_ptr<MemoryPool>;

    MemoryPool(const MemoryPool&) = delete;
    auto operator=(const MemoryPool&) -> MemoryPool& = delete;
    MemoryPool(MemoryPool&&) = delete;
    auto operator=(MemoryPool&&) -> MemoryPool& = delete;
    ~MemoryPool();

    [[nodiscard]] auto allocate(const MemoryRequest& request) -> Result<MemoryAllocation>;
    [[nodiscard]] auto getStats() const -> const MemoryPoolStats&;

private:
    friend class MemoryAllocation;

    struct Block
    {
        std::unique_ptr<MemoryBlock> memory;
        std::uint32_t memoryType {0};
        bool image {false};
        bool dedicated {false};
    };

    struct Allocation
    {
        Block* block {nullptr};
        MemoryRange range;
    };

    MemoryPool(const vk::raii::PhysicalDevice& physicalDevice, const vk::raii::Device& device);

    [[nodiscard]] auto selectMemoryType(const MemoryRequest& request) const -> Result<std::uint32_t>;
    [[nodiscard]] auto getBlockSize(std::uint32_t memoryType) const -> std::uint64_t;
    [[nodiscard]] auto tryExisting(const MemoryRequest& request, std::uint32_t memoryType) -> Result<MemoryAllocation>;
    [[nodiscard]] auto grow(const MemoryRequest& request, std::uint32_t memoryType) -> Result<MemoryAllocation>;
    [[nodiscard]] auto makeBlock(const MemoryRequest& request, std::uint32_t memoryType, bool dedicated)
        -> Result<MemoryAllocation>;
    [[nodiscard]] auto checkBudget(std::uint32_t memoryType, std::uint64_t size) -> Result<void>;
    [[nodiscard]] auto publish(Block& block, MemoryRange range) -> MemoryAllocation;
    [[nodiscard]] auto getAllocation(std::uint64_t id) const -> const Allocation&;
    void release(std::uint64_t id) noexcept;
    void reclaimEmptyBlocks();
    void eraseBlock(const std::list<Block>::iterator& position);
    void releaseEmpty(Block& block);

    const vk::raii::PhysicalDevice& _physicalDevice;
    const vk::raii::Device& _device;
    vk::PhysicalDeviceMemoryProperties _memoryProperties;
    vk::PhysicalDeviceLimits _limits;
    std::uint64_t _maximumAllocationSize;
    std::vector<std::uint64_t> _heapCommitted;
    std::list<Block> _blocks;
    std::map<std::uint64_t, Allocation> _allocations;
    std::uint64_t _nextId {1};
    MemoryPoolStats _stats;
};
}
