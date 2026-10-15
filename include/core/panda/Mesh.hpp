#pragma once

/// @file
/// Shared ownership of vertex and optional index buffers.

#include <cstdint>
#include <memory>
#include <optional>

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
    friend class UploadBatch;
    friend class detail::MeshResource;
    explicit Mesh(std::shared_ptr<detail::MeshResource> resource);
    std::shared_ptr<detail::MeshResource> _resource;
};
}
