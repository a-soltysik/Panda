#pragma once

/// @file
/// Application-owned camera, lighting and mesh draw data.

#include <cstdint>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <panda/Mesh.hpp>
#include <vector>

namespace panda
{
/// @brief Shading model provided by Panda's built-in renderer.
enum class MaterialModel : std::uint8_t
{
    /// Metallic-roughness direct lighting; the mesh must provide normals.
    Lit,
    /// Base color without lighting; normals are not required.
    Unlit
};

/// @brief Mesh faces selected for rendering by a material.
enum class RenderFace : std::uint8_t
{
    /// Render front-facing triangles and cull back-facing triangles.
    Front,
    /// Render back-facing triangles and cull front-facing triangles.
    Back,
    /// Render both sides and orient back-facing shading normals inward.
    Both
};

/// @brief Alpha handling provided by Panda's built-in renderer.
enum class AlphaMode : std::uint8_t
{
    /// Render the entire surface as opaque, without using alpha for coverage.
    Opaque,
    /// Use alpha as a cutout: below alphaCutoff is invisible, otherwise fully opaque.
    Mask
};

/// @brief Built-in material values shared by mesh draws.
struct Material
{
    /// Direct GGX lighting or tone-mapped base color without lighting.
    MaterialModel model {MaterialModel::Lit};
    /// Linear RGBA material color multiplied by draw tint and vertex color.
    glm::vec4 baseColor {1.0F, 1.0F, 1.0F, 1.0F};
    /// Metallic response, clamped to [0,1] when uploaded.
    float metallic {0.0F};
    /// Perceptual roughness, clamped to [0,1] when uploaded.
    float roughness {1.0F};
    /// Mesh side(s) rendered by the built-in pipelines.
    RenderFace renderFace {RenderFace::Front};
    /// Whether to render the full surface or cut out pixels below alphaCutoff.
    AlphaMode alphaMode {AlphaMode::Opaque};
    /// Linear alpha threshold used by Mask materials; must be in [0,1].
    float alphaCutoff {0.5F};
};

/// @brief Camera data for one rendered view.
struct RenderCamera
{
    /// Combined projection and view transform; matrices multiply column vectors.
    glm::mat4 viewProjection {1.0F};
    /// World-space camera position in meters.
    glm::vec3 position {};
};

/// @brief One directional light; direction points from the light into the scene.
struct DirectionalLight
{
    /// World-space direction from the light into the scene; normalized by the renderer.
    glm::vec3 direction {-0.35777086F, -0.89442718F, -0.26832816F};
    /// Linear RGB light color.
    glm::vec3 color {1.0F, 1.0F, 1.0F};
    /// Nonnegative linear intensity multiplier.
    float intensity {3.0F};
};

/// @brief One uploaded mesh with an object-to-world transform, linear tint and material.
struct MeshDraw
{
    /// Retained GPU mesh with attributes compatible with its material.
    Mesh mesh;
    /// Full object-to-world transform in column-major order.
    glm::mat4 modelMatrix {1.0F};
    /// Linear RGBA per-draw modulation multiplied by material and vertex color.
    glm::vec4 tint {1.0F, 1.0F, 1.0F, 1.0F};
    /// Built-in material parameters for this mesh.
    Material material {};
};

/// @brief Transient application-owned inputs for one renderer frame.
/// The renderer copies draw values and retains mesh handles through GPU completion.
struct RenderView
{
    /// Camera used for mesh projection and view-dependent lighting.
    RenderCamera camera;
    /// Directional light used by the built-in metallic-roughness material.
    DirectionalLight directionalLight;
    /// Draws to render; an empty list presents only the configured background.
    std::vector<MeshDraw> meshes;
};
}
