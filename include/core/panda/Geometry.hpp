#pragma once

/// @file
/// Standard CPU mesh geometry.

#include <array>
#include <cstdint>
#include <variant>
#include <vector>

namespace panda
{
/// @brief Standard mesh vertex, with linear color and tangent handedness in tangent[3].
struct MeshVertex
{
    /// Position in object-space meters.
    std::array<float, 3> position {};
    /// Outward object-space normal.
    std::array<float, 3> normal {};
    /// Texture coordinates.
    std::array<float, 2> uv {};
    /// Tangent vector and bitangent handedness.
    std::array<float, 4> tangent {};
    /// Linear RGBA vertex color.
    std::array<float, 4> color {1.0F, 1.0F, 1.0F, 1.0F};
};

/// @brief Byte stride of the standard mesh vertex format.
inline constexpr auto standardMeshVertexStrideBytes = std::uint32_t {64};
static_assert(sizeof(MeshVertex) == standardMeshVertexStrideBytes);

/// @brief Owned CPU geometry in the standard mesh format.
struct Geometry
{
    /// Vertices in standard mesh layout.
    std::vector<MeshVertex> vertices;
    /// 16- or 32-bit vertex indices; an empty vector describes non-indexed geometry.
    std::variant<std::vector<std::uint16_t>, std::vector<std::uint32_t>> indices;
};
}
