#pragma once

/// @file
/// Standard CPU mesh geometry.

#include <cstdint>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <variant>
#include <vector>

namespace panda
{
/// @brief Standard mesh vertex, with linear color and tangent handedness in tangent.w.
struct MeshVertex
{
    /// Position in object-space meters.
    glm::vec3 position {};
    /// Outward object-space normal.
    glm::vec3 normal {};
    /// Texture coordinates.
    glm::vec2 uv {};
    /// Tangent vector and bitangent handedness.
    glm::vec4 tangent {};
    /// Linear RGBA vertex color.
    glm::vec4 color {1.0F, 1.0F, 1.0F, 1.0F};
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
