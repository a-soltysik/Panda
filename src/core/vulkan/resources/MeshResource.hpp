#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <panda/Error.hpp>
#include <panda/Mesh.hpp>

#include "BufferResource.hpp"
#include "MemoryPool.hpp"
#include "MeshLayout.hpp"

namespace panda::detail
{
class MeshResource final
{
public:
    [[nodiscard]] static auto getResource(const Mesh& mesh) -> const std::shared_ptr<MeshResource>&;
    [[nodiscard]] static auto create(const vk::raii::Device& device, MemoryPool& pool, const MeshDesc& description)
        -> Result<std::shared_ptr<MeshResource>>;
    [[nodiscard]] auto getVertexBuffer() const -> const std::shared_ptr<BufferResource>&;
    [[nodiscard]] auto getIndexBuffer() const -> const std::shared_ptr<BufferResource>&;
    [[nodiscard]] auto getLayout() const -> const MeshLayout&;

private:
    MeshResource(std::shared_ptr<BufferResource> vertexBuffer,
                 std::shared_ptr<BufferResource> indexBuffer,
                 const MeshLayout& layout);

    std::shared_ptr<BufferResource> _vertexBuffer;
    std::shared_ptr<BufferResource> _indexBuffer;
    MeshLayout _layout;
};
}
