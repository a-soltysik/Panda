// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
// clang-format on

#include "MeshLayout.hpp"

#include <algorithm>
#include <cstdint>
#include <expected>
#include <optional>
#include <panda/Error.hpp>
#include <panda/Mesh.hpp>
#include <utility>
#include <vector>

namespace panda::core::vulkan::resources
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

auto attributeSize(VertexAttributeFormat format) -> std::optional<std::uint32_t>
{
    switch (format)
    {
    case VertexAttributeFormat::Float2:
        return 8;
    case VertexAttributeFormat::Float3:
        return 12;
    case VertexAttributeFormat::Float4:
        return 16;
    }
    return std::nullopt;
}

auto expectedFormat(VertexAttributeSemantic semantic) -> std::optional<VertexAttributeFormat>
{
    switch (semantic)
    {
    case VertexAttributeSemantic::Position:
    case VertexAttributeSemantic::Normal:
        return VertexAttributeFormat::Float3;
    case VertexAttributeSemantic::TexCoord0:
        return VertexAttributeFormat::Float2;
    case VertexAttributeSemantic::Tangent:
    case VertexAttributeSemantic::Color:
        return VertexAttributeFormat::Float4;
    }
    return std::nullopt;
}

struct ValidatedAttribute
{
    VertexAttributeSemantic semantic;
    std::uint64_t start;
    std::uint64_t end;
};

auto validateAttribute(const VertexAttribute& attribute, std::uint32_t vertexStride) -> Result<ValidatedAttribute>
{
    const auto size = attributeSize(attribute.format);
    const auto format = expectedFormat(attribute.semantic);
    if (!size || !format || attribute.format != *format ||
        static_cast<std::uint64_t>(attribute.offset) + *size > vertexStride)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Mesh vertex attribute is invalid")};
    }
    const auto start = static_cast<std::uint64_t>(attribute.offset);
    return ValidatedAttribute {.semantic = attribute.semantic, .start = start, .end = start + *size};
}

auto hasPosition(const std::vector<ValidatedAttribute>& attributes) -> bool
{
    return std::ranges::any_of(attributes, [](const auto& attribute) {
        return attribute.semantic == VertexAttributeSemantic::Position;
    });
}

auto validateNonOverlappingAttributes(std::vector<ValidatedAttribute>& attributes) -> Result<void>
{
    std::ranges::sort(attributes, {}, &ValidatedAttribute::start);
    const auto overlap = std::ranges::adjacent_find(attributes, [](const auto& left, const auto& right) {
        return left.end > right.start;
    });
    if (overlap != attributes.end())
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Mesh vertex attributes overlap")};
    }
    return {};
}

auto appendValidatedAttribute(const VertexAttribute& attribute,
                              std::uint32_t vertexStride,
                              std::vector<ValidatedAttribute>& attributes) -> Result<void>
{
    const auto validated = validateAttribute(attribute, vertexStride);
    if (!validated)
    {
        return std::unexpected {validated.error()};
    }
    if (std::ranges::any_of(attributes, [&attribute](const auto& existing) {
            return existing.semantic == attribute.semantic;
        }))
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Mesh vertex attribute is invalid")};
    }
    attributes.push_back(*validated);
    return {};
}

auto validateAttributes(const MeshDesc& description) -> Result<void>
{
    if (description.attributes.empty())
    {
        return {};
    }

    auto attributes = std::vector<ValidatedAttribute> {};
    attributes.reserve(description.attributes.size());
    for (const auto& attribute : description.attributes)
    {
        if (const auto appended = appendValidatedAttribute(attribute, description.vertexStride, attributes); !appended)
        {
            return appended;
        }
    }
    if (!hasPosition(attributes))
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Renderable mesh layout requires a position")};
    }
    return validateNonOverlappingAttributes(attributes);
}
}

auto MeshLayout::create(MeshDesc description) -> Result<MeshLayout>
{
    if (description.vertexCount == 0 || description.vertexStride == 0)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Mesh vertices require a count and stride")};
    }
    if (const auto attributes = validateAttributes(description); !attributes)
    {
        return std::unexpected {attributes.error()};
    }
    const auto indexSize = calculateIndexSize(description.indices);
    if (!indexSize)
    {
        return std::unexpected {indexSize.error()};
    }
    const auto vertexSize = static_cast<std::uint64_t>(description.vertexCount) * description.vertexStride;
    auto layout = MeshLayout {};
    layout._description = std::move(description);
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

auto MeshLayout::getVertexAttributes() const noexcept -> const std::vector<VertexAttribute>&
{
    return _description.attributes;
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
