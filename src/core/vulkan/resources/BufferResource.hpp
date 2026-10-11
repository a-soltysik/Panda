#pragma once

#include <cstdint>
#include <memory>
#include <panda/Buffer.hpp>
#include <panda/Error.hpp>

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
    [[nodiscard]] static auto create(const vk::raii::Device& device, MemoryPool& pool, const BufferDesc& description)
        -> Result<std::shared_ptr<BufferResource>>;
    [[nodiscard]] auto getSize() const -> std::uint64_t;

private:
    BufferResource(MemoryPool& pool, vk::raii::Buffer buffer, const BufferDesc& description);
    [[nodiscard]] auto bind(const vk::raii::Device& device, const BufferDesc& description) -> Result<void>;

    MemoryPool* _pool;
    MemoryAllocation _allocation;
    vk::raii::Buffer _buffer;
    std::uint64_t _size;
};
}
