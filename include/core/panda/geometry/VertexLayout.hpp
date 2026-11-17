#pragma once

/// @file
/// CPU vertex attribute layout shared by geometry and mesh descriptions.

#include <cstdint>

namespace panda
{

/// @brief Float format used by a mesh vertex attribute.
enum class VertexAttributeFormat : std::uint8_t
{
    /// Two 32-bit floating-point components.
    Float2,
    /// Three 32-bit floating-point components.
    Float3,
    /// Four 32-bit floating-point components.
    Float4
};

/// @brief Standard shader meaning of one vertex attribute.
enum class VertexAttributeSemantic : std::uint8_t
{
    /// Object-space position; required by every renderable mesh.
    Position,
    /// Object-space normal; required by lit materials.
    Normal,
    /// First texture-coordinate set.
    TexCoord0,
    /// Object-space tangent with handedness in its fourth component.
    Tangent,
    /// Linear RGBA vertex tint; absent color contributes white.
    Color
};

/// @brief One attribute's format and byte offset within a vertex.
struct VertexAttribute
{
    /// Shader meaning, which must be unique within the layout.
    VertexAttributeSemantic semantic {VertexAttributeSemantic::Position};
    /// Stored floating-point component count.
    VertexAttributeFormat format {VertexAttributeFormat::Float3};
    /// Byte offset from the beginning of a vertex.
    std::uint32_t offset {0};
    /// @brief Compares all attribute fields.
    friend auto operator==(const VertexAttribute&, const VertexAttribute&) -> bool = default;
};

}
