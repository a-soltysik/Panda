#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <panda/Error.hpp>
#include <panda/Mesh.hpp>

#include "BufferResource.hpp"
#include "MeshLayout.hpp"
#include "resources/memory/MemoryPool.hpp"

namespace panda::detail
{
namespace mesh_resources = ::panda::core::vulkan::resources;

class MeshResource final
{
public:
    [[nodiscard]] static auto getResource(const Mesh& mesh) -> const std::shared_ptr<MeshResource>&;
    [[nodiscard]] static auto create(const vk::raii::Device& device,
                                     mesh_resources::MemoryPool& pool,
                                     const MeshDesc& description) -> Result<std::shared_ptr<MeshResource>>;
    [[nodiscard]] auto getVertexBuffer() const -> const std::shared_ptr<BufferResource>&;
    [[nodiscard]] auto getIndexBuffer() const -> const std::shared_ptr<BufferResource>&;
    [[nodiscard]] auto getLayout() const -> const mesh_resources::MeshLayout&;
    [[nodiscard]] auto belongsTo(const mesh_resources::MemoryPool& pool) const -> bool;

private:
    MeshResource(std::shared_ptr<BufferResource> vertexBuffer,
                 std::shared_ptr<BufferResource> indexBuffer,
                 const mesh_resources::MeshLayout& layout);

    std::shared_ptr<BufferResource> _vertexBuffer;
    std::shared_ptr<BufferResource> _indexBuffer;
    mesh_resources::MeshLayout _layout;
};
}
