#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <panda/Buffer.hpp>
#include <panda/Error.hpp>
#include <span>

#include "VulkanHpp.hpp"
#include "resources/memory/MemoryAllocation.hpp"
#include "resources/memory/MemoryPool.hpp"

namespace panda::detail
{
namespace buffer_resources = ::panda::core::vulkan::resources;

// Declaration order destroys the bound buffer before releasing its allocation.
// The context pool and device outlive this state, including every shared owner.
class BufferResource final
{
public:
    [[nodiscard]] static auto getResource(const Buffer& buffer) -> const std::shared_ptr<BufferResource>&;
    [[nodiscard]] static auto create(const vk::raii::Device& device,
                                     buffer_resources::MemoryPool& pool,
                                     const BufferDesc& description) -> Result<std::shared_ptr<BufferResource>>;
    [[nodiscard]] auto getSize() const -> std::uint64_t;
    [[nodiscard]] auto getReservedSize() const -> std::uint64_t;
    [[nodiscard]] auto getNative() const -> vk::Buffer;
    [[nodiscard]] auto getUsage() const -> vk::BufferUsageFlags;
    [[nodiscard]] auto belongsTo(const buffer_resources::MemoryPool& pool) const -> bool;
    [[nodiscard]] auto getMappedBytes() const -> Result<std::span<std::byte>>;
    [[nodiscard]] auto writeMapped(std::uint64_t offset, std::span<const std::byte> bytes) const -> Result<void>;
    [[nodiscard]] auto invalidate(std::uint64_t offset, std::uint64_t size) const -> Result<void>;

private:
    BufferResource(buffer_resources::MemoryPool& pool, vk::raii::Buffer buffer, const BufferDesc& description);
    [[nodiscard]] auto bind(const vk::raii::Device& device, const BufferDesc& description) -> Result<void>;

    buffer_resources::MemoryPool& _pool;
    buffer_resources::MemoryAllocation _allocation;
    vk::raii::Buffer _buffer;
    std::uint64_t _size;
    vk::BufferUsageFlags _usage;
    BufferMemory _intent;
};
}
