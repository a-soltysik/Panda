#pragma once

#include <cstdint>
#include <optional>
#include <panda/Error.hpp>
#include <panda/Mesh.hpp>
#include <vector>

namespace panda::core::vulkan::resources
{
class MeshLayout final
{
public:
    [[nodiscard]] static auto create(MeshDesc description) -> Result<MeshLayout>;
    [[nodiscard]] auto getVertexCount() const noexcept -> std::uint32_t;
    [[nodiscard]] auto getVertexStride() const noexcept -> std::uint32_t;
    [[nodiscard]] auto getVertexAttributes() const noexcept -> const std::vector<VertexAttribute>&;
    [[nodiscard]] auto getVertexSize() const noexcept -> std::uint64_t;
    [[nodiscard]] auto getIndices() const noexcept -> const std::optional<MeshIndexDesc>&;
    [[nodiscard]] auto getIndexSize() const noexcept -> std::uint64_t;

private:
    MeshLayout() = default;

    MeshDesc _description {};
    std::uint64_t _vertexSize {0};
    std::uint64_t _indexSize {0};
};
}
