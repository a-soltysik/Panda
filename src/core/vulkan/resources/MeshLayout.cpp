#include "MeshLayout.hpp"

#include <cstdint>
#include <expected>
#include <optional>
#include <panda/Error.hpp>
#include <panda/Mesh.hpp>

namespace panda::detail
{
namespace
{
auto indexElementSize(MeshIndexType type) -> std::optional<std::uint64_t>
{
    switch (type)
    {
    case MeshIndexType::UInt16:
        return 2;
    case MeshIndexType::UInt32:
        return 4;
    }
    return std::nullopt;
}

auto calculateIndexSize(const std::optional<MeshIndexDesc>& indices) -> Result<std::uint64_t>
{
    if (!indices)
    {
        return 0;
    }
    if (indices->count == 0)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Mesh index count must be nonzero")};
    }
    const auto elementSize = indexElementSize(indices->type);
    if (!elementSize)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Mesh index type is invalid")};
    }
    return static_cast<std::uint64_t>(indices->count) * *elementSize;
}
}

auto MeshLayout::create(const MeshDesc& description) -> Result<MeshLayout>
{
    if (description.vertexCount == 0 || description.vertexStride == 0)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Mesh vertices require a count and stride")};
    }
    const auto indexSize = calculateIndexSize(description.indices);
    if (!indexSize)
    {
        return std::unexpected {indexSize.error()};
    }
    const auto vertexSize = static_cast<std::uint64_t>(description.vertexCount) * description.vertexStride;
    auto layout = MeshLayout {};
    layout._description = description;
    layout._vertexSize = vertexSize;
    layout._indexSize = *indexSize;
    return layout;
}

auto MeshLayout::getVertexCount() const noexcept -> std::uint32_t
{
    return _description.vertexCount;
}

auto MeshLayout::getVertexStride() const noexcept -> std::uint32_t
{
    return _description.vertexStride;
}

auto MeshLayout::getVertexSize() const noexcept -> std::uint64_t
{
    return _vertexSize;
}

auto MeshLayout::getIndices() const noexcept -> const std::optional<MeshIndexDesc>&
{
    return _description.indices;
}

auto MeshLayout::getIndexSize() const noexcept -> std::uint64_t
{
    return _indexSize;
}
}
