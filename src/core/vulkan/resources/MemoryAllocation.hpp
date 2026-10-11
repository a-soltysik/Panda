#pragma once

#include <cstddef>
#include <cstdint>
#include <panda/Error.hpp>
#include <span>

#include "RangeAllocator.hpp"
#include "VulkanHpp.hpp"

namespace panda::detail
{
class MemoryPool;

// Unique range owner borrowing its pool. Destroy the bound native resource and
// finish GPU use before reset/destruction. Moving preserves the allocation.
// Mapped views are borrowed and expire on reset/destruction.
class MemoryAllocation final
{
public:
    MemoryAllocation();
    MemoryAllocation(const MemoryAllocation&) = delete;
    auto operator=(const MemoryAllocation&) -> MemoryAllocation& = delete;
    MemoryAllocation(MemoryAllocation&& other) noexcept;
    auto operator=(MemoryAllocation&& other) noexcept -> MemoryAllocation&;
    ~MemoryAllocation();

    [[nodiscard]] auto isValid() const -> bool;
    [[nodiscard]] auto getMemory() const -> vk::DeviceMemory;
    [[nodiscard]] auto getRange() const -> MemoryRange;
    [[nodiscard]] auto getMappedBytes() const -> Result<std::span<std::byte>>;
    [[nodiscard]] auto flush(std::uint64_t offset, std::uint64_t size) const -> Result<void>;
    [[nodiscard]] auto invalidate(std::uint64_t offset, std::uint64_t size) const -> Result<void>;
    void reset() noexcept;

private:
    friend class MemoryPool;

    MemoryAllocation(MemoryPool& pool, std::uint64_t id);

    MemoryPool* _pool {nullptr};
    std::uint64_t _id {0};
};
}
