// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
// clang-format on

#include <expected>
#include <memory>
#include <panda/Error.hpp>
#include <panda/Mesh.hpp>
#include <utility>

#include "BufferResource.hpp"
#include "MeshLayout.hpp"
#include "ScopedMock.hpp"
#include "panda/core/MeshResourceMock.hpp"
#include "resources/memory/MemoryPool.hpp"

namespace panda::detail
{
auto MeshResource::getResource(const Mesh& mesh) -> const std::shared_ptr<MeshResource>&
{
    return mesh._resource;
}

auto MeshResource::create(const vk::raii::Device& device, mesh_resources::MemoryPool& pool, const MeshDesc& description)
    -> Result<std::shared_ptr<MeshResource>>
{
    auto& mock = test::ScopedMock<test::MeshResourceMock>::getActiveMock();
    auto plan = mock.create(device, pool, description);
    if (!plan)
    {
        return std::unexpected {plan.error()};
    }
    if (plan->layout == nullptr)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Mesh resource mock needs a layout")};
    }
    return std::shared_ptr<MeshResource> {
        new MeshResource {std::move(plan->vertexBuffer), std::move(plan->indexBuffer), *plan->layout}
    };
}

MeshResource::MeshResource(std::shared_ptr<BufferResource> vertexBuffer,
                           std::shared_ptr<BufferResource> indexBuffer,
                           const mesh_resources::MeshLayout& layout)
    : _vertexBuffer {std::move(vertexBuffer)},
      _indexBuffer {std::move(indexBuffer)},
      _layout {layout}
{
}

auto MeshResource::getVertexBuffer() const -> const std::shared_ptr<BufferResource>&
{
    return test::ScopedMock<test::MeshResourceMock>::getActiveMock().getVertexBuffer(*this);
}

auto MeshResource::getIndexBuffer() const -> const std::shared_ptr<BufferResource>&
{
    return test::ScopedMock<test::MeshResourceMock>::getActiveMock().getIndexBuffer(*this);
}

auto MeshResource::getLayout() const -> const mesh_resources::MeshLayout&
{
    return test::ScopedMock<test::MeshResourceMock>::getActiveMock().getLayout(*this);
}
}
