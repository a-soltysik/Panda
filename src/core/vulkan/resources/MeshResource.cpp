// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include "MeshResource.hpp"

#include <cstdint>
#include <expected>
#include <memory>
#include <panda/Buffer.hpp>
#include <panda/Error.hpp>
#include <panda/Mesh.hpp>
#include <utility>

#include "MeshLayout.hpp"
#include "resources/memory/MemoryPool.hpp"

namespace panda::detail
{
using core::vulkan::resources::MemoryPool;
using core::vulkan::resources::MeshLayout;

namespace
{
auto createMeshBuffer(const vk::raii::Device& device, MemoryPool& pool, std::uint64_t size, vk::BufferUsageFlags usage)
    -> Result<std::shared_ptr<BufferResource>>
{
    return BufferResource::create(device, pool, {.size = size, .usage = usage, .memory = BufferMemory::Device});
}

auto createIndexBuffer(const vk::raii::Device& device, MemoryPool& pool, const MeshLayout& layout)
    -> Result<std::shared_ptr<BufferResource>>
{
    if (layout.getIndexSize() == 0)
    {
        return std::shared_ptr<BufferResource> {};
    }
    return createMeshBuffer(device,
                            pool,
                            layout.getIndexSize(),
                            vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eIndexBuffer);
}
}

auto MeshResource::getResource(const Mesh& mesh) -> const std::shared_ptr<MeshResource>&
{
    return mesh._resource;
}

auto MeshResource::create(const vk::raii::Device& device, MemoryPool& pool, const MeshDesc& description)
    -> Result<std::shared_ptr<MeshResource>>
{
    auto layout = MeshLayout::create(description);
    if (!layout)
    {
        return std::unexpected {layout.error()};
    }

    auto vertexBuffer =
        createMeshBuffer(device,
                         pool,
                         layout->getVertexSize(),
                         vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer);
    if (!vertexBuffer)
    {
        return std::unexpected {vertexBuffer.error()};
    }

    auto indexBuffer = createIndexBuffer(device, pool, *layout);
    if (!indexBuffer)
    {
        return std::unexpected {indexBuffer.error()};
    }

    return std::shared_ptr<MeshResource> {
        new MeshResource {std::move(*vertexBuffer), std::move(*indexBuffer), *layout}
    };
}

MeshResource::MeshResource(std::shared_ptr<BufferResource> vertexBuffer,
                           std::shared_ptr<BufferResource> indexBuffer,
                           const MeshLayout& layout)
    : _vertexBuffer {std::move(vertexBuffer)},
      _indexBuffer {std::move(indexBuffer)},
      _layout {layout}
{
}

auto MeshResource::getVertexBuffer() const -> const std::shared_ptr<BufferResource>&
{
    return _vertexBuffer;
}

auto MeshResource::getIndexBuffer() const -> const std::shared_ptr<BufferResource>&
{
    return _indexBuffer;
}

auto MeshResource::getLayout() const -> const MeshLayout&
{
    return _layout;
}

auto MeshResource::belongsTo(const MemoryPool& pool) const -> bool
{
    return _vertexBuffer->belongsTo(pool);
}
}
