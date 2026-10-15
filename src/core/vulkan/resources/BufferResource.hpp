#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <panda/Buffer.hpp>
#include <panda/Error.hpp>
#include <span>

#include "MemoryAllocation.hpp"
#include "MemoryPool.hpp"
#include "VulkanHpp.hpp"

namespace panda::detail
{
// Declaration order destroys the bound buffer before releasing its allocation.
// The context pool and device outlive this state, including every shared owner.
class BufferResource final
{
public:
    [[nodiscard]] static auto getResource(const Buffer& buffer) -> const std::shared_ptr<BufferResource>&;
    [[nodiscard]] static auto create(const vk::raii::Device& device, MemoryPool& pool, const BufferDesc& description)
        -> Result<std::shared_ptr<BufferResource>>;
    [[nodiscard]] auto getSize() const -> std::uint64_t;
    [[nodiscard]] auto getReservedSize() const -> std::uint64_t;
    [[nodiscard]] auto getNative() const -> vk::Buffer;
    [[nodiscard]] auto getUsage() const -> vk::BufferUsageFlags;
    [[nodiscard]] auto belongsTo(const MemoryPool& pool) const -> bool;
    [[nodiscard]] auto getMappedBytes() const -> Result<std::span<std::byte>>;
    [[nodiscard]] auto writeMapped(std::span<const std::byte> bytes) const -> Result<void>;
    [[nodiscard]] auto invalidate(std::uint64_t offset, std::uint64_t size) const -> Result<void>;

private:
    BufferResource(MemoryPool& pool, vk::raii::Buffer buffer, const BufferDesc& description);
    [[nodiscard]] auto bind(const vk::raii::Device& device, const BufferDesc& description) -> Result<void>;

    MemoryPool& _pool;
    MemoryAllocation _allocation;
    vk::raii::Buffer _buffer;
    std::uint64_t _size;
    vk::BufferUsageFlags _usage;
    BufferMemory _intent;
};
}
