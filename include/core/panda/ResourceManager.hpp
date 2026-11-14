#pragma once

/// @file
/// Engine-owned creation of ordinary GPU resources.

#include <panda/Buffer.hpp>
#include <panda/Error.hpp>
#include <panda/Mesh.hpp>
#include <panda/OffscreenTarget.hpp>
#include <panda/Texture.hpp>

namespace panda
{
class Engine;
struct Geometry;

/// @brief Creates resources that belong to one Engine.
/// This facade is borrowed from Engine and must not outlive it. Calls are serialized
/// with other operations on the owning Engine.
class ResourceManager final
{
public:
    ResourceManager(const ResourceManager&) = delete;
    auto operator=(const ResourceManager&) -> ResourceManager& = delete;
    ResourceManager(ResourceManager&&) = delete;
    auto operator=(ResourceManager&&) -> ResourceManager& = delete;
    ~ResourceManager() = default;

    /// @brief Creates an ordinary fixed-size buffer with undefined initial bytes.
    /// @return A shared resource owner or a validation/allocation/backend error.
    /// May allocate; submits no work and does not wait.
    [[nodiscard]] auto createBuffer(const BufferDesc& description) const -> Result<Buffer>;

    /// @brief Creates an optimal-tiled, single-mip, single-layer 2D image with undefined initial contents.
    /// @return A shared image owner or a validation/allocation/backend error.
    /// May allocate; submits no work and does not wait.
    [[nodiscard]] auto createTexture(const TextureDesc& description) const -> Result<Texture>;

    /// @brief Creates a single-sample RGBA8 sRGB color target for offscreen rendering.
    /// @return A target or an InvalidArgument/Unsupported/allocation error.
    [[nodiscard]] auto createOffscreenTarget(const OffscreenTargetDesc& description) const -> Result<OffscreenTarget>;

    /// @brief Creates fixed vertex storage and optional index storage for one mesh.
    /// @return A shared mesh owner or a validation/allocation/backend error.
    /// May allocate; submits no work and does not wait.
    [[nodiscard]] auto createMesh(const MeshDesc& description) const -> Result<Mesh>;

    /// @brief Creates storage matching standard CPU geometry without uploading it.
    /// @return A shared mesh owner or a validation/allocation/backend error.
    /// Empty indices create a non-indexed mesh.
    [[nodiscard]] auto createMeshFromGeometry(const Geometry& geometry) const -> Result<Mesh>;

private:
    friend class Engine;

    explicit ResourceManager(const Engine& context) noexcept;

    const Engine* _engine;
};
}
