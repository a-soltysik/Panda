#pragma once

/// @file
/// Shared ownership of vertex and optional index buffers.

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace panda
{
class Context;
class UploadBatch;

namespace detail
{
class MeshResource;
}

/// @brief Integer width used by a mesh's indices.
enum class MeshIndexType : std::uint8_t
{
    UInt16,
    UInt32
};

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

/// @brief Index count and element type for an indexed mesh.
struct MeshIndexDesc
{
    /// Number of indices; must be nonzero when an index description is present.
    std::uint32_t count {0};
    /// Width of each index element.
    MeshIndexType type {MeshIndexType::UInt16};
};

/// @brief Fixed vertex and optional index storage for one mesh.
struct MeshDesc
{
    /// Number of vertices; must be nonzero.
    std::uint32_t vertexCount {0};
    /// Number of bytes between the starts of consecutive vertices; must be nonzero.
    std::uint32_t vertexStride {0};
    /// Index storage; absence creates a non-indexed mesh.
    std::optional<MeshIndexDesc> indices;
    /// Optional attributes in each vertex. An empty list creates upload-only storage.
    /// When present, Position is required and semantic/format pairs must match the standard shader meaning.
    std::vector<VertexAttribute> attributes;
};

/// @brief Copyable owner of a mesh's vertex and optional index storage.
/// Context outlives every copy. Copies retain the same mesh and do not synchronize
/// GPU access; no work is submitted by this handle. Vulkan buffers remain private.
class Mesh final
{
public:
    /// @brief Creates an empty handle.
    Mesh();
    /// @brief Retains the same mesh without copying its data.
    Mesh(const Mesh& other);
    /// @brief Releases the previous mesh and retains other's mesh.
    auto operator=(const Mesh& other) -> Mesh&;
    /// @brief Transfers ownership and leaves other empty.
    Mesh(Mesh&& other) noexcept;
    /// @brief Releases the previous mesh and transfers ownership.
    auto operator=(Mesh&& other) noexcept -> Mesh&;
    /// @brief Releases one owner; the last owner destroys its buffers and allocations.
    ~Mesh();
    /// @brief Reports whether this handle retains a mesh.
    [[nodiscard]] auto isValid() const noexcept -> bool;
    /// @brief Returns the vertex count, or zero for an empty handle.
    [[nodiscard]] auto getVertexCount() const noexcept -> std::uint32_t;
    /// @brief Returns the vertex stride in bytes, or zero for an empty handle.
    [[nodiscard]] auto getVertexStride() const noexcept -> std::uint32_t;
    /// @brief Returns a view of this mesh's declared attributes, or an empty view for an upload-only/invalid mesh.
    /// The view remains valid while any copy of this mesh retains its resource.
    [[nodiscard]] auto getVertexAttributes() const noexcept -> std::span<const VertexAttribute>;
    /// @brief Returns the vertex storage size in bytes, or zero for an empty handle.
    [[nodiscard]] auto getVertexSize() const noexcept -> std::uint64_t;
    /// @brief Returns the index count, or zero for an empty or non-indexed mesh.
    [[nodiscard]] auto getIndexCount() const noexcept -> std::uint32_t;
    /// @brief Returns the index type, or absence for an empty or non-indexed mesh.
    [[nodiscard]] auto getIndexType() const noexcept -> std::optional<MeshIndexType>;
    /// @brief Returns index storage size in bytes, or zero for an empty or non-indexed mesh.
    [[nodiscard]] auto getIndexSize() const noexcept -> std::uint64_t;
    /// @brief Releases this owner and leaves the handle empty; does not wait for GPU work.
    void reset() noexcept;

private:
    friend class Context;
    friend class detail::MeshResource;
    explicit Mesh(std::shared_ptr<detail::MeshResource> resource);
    std::shared_ptr<detail::MeshResource> _resource;
};
}
