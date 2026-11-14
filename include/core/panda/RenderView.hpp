#pragma once

/// @file
/// Application-owned camera, lighting and mesh draw data.

#include <cstdint>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <numbers>
#include <optional>
#include <panda/Mesh.hpp>
#include <panda/Texture.hpp>
#include <variant>
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
    Mask,
    /// Blend combined alpha over prior color draws; blended meshes do not cast shadows.
    Blend
};

/// @brief Built-in material values shared by mesh draws.
struct Material
{
    /// Direct GGX lighting or tone-mapped base color without lighting.
    MaterialModel model {MaterialModel::Lit};
    /// Linear RGBA material color multiplied by base-color texture, draw tint and vertex color.
    glm::vec4 baseColor {1.0F, 1.0F, 1.0F, 1.0F};
    /// Optional base-color map. If set, it must be uploaded RGBA8 sRGB, sampled, and from the rendered Engine;
    /// the mesh must declare TexCoord0. An empty handle contributes white.
    Texture baseColorTexture;
    /// Optional Lit-only linear metallic-roughness map. If set, it must be uploaded RGBA8 UNORM, sampled, and from
    /// the rendered Engine; the mesh must declare TexCoord0. Blue scales metallic and green scales roughness.
    /// An empty handle contributes white.
    Texture metallicRoughnessTexture;
    /// Optional emissive map. If set, it must be uploaded RGBA8 sRGB, sampled, and from the rendered Engine; the
    /// mesh must declare TexCoord0. Its RGB multiplies emissive; alpha is ignored. An empty handle contributes white.
    Texture emissiveTexture;
    /// Optional Lit-only tangent-space normal map. If set, it must be uploaded RGBA8 UNORM, sampled, and from the
    /// rendered Engine. Without TexCoord0 or Tangent, the map is ignored with a warning.
    Texture normalTexture;
    /// Finite multiplier for tangent-space normal-map X and Y components.
    float normalScale {1.0F};
    /// Linear RGB emissive multiplier, multiplied by emissiveTexture and added before tone mapping; defaults to black.
    glm::vec3 emissive {};
    /// Metallic response, clamped to [0,1] when uploaded.
    float metallic {0.0F};
    /// Perceptual roughness, clamped to [0,1] when uploaded.
    float roughness {1.0F};
    /// Mesh side(s) rendered by the built-in pipelines.
    RenderFace renderFace {RenderFace::Front};
    /// Whether to render opaque, cut out by alphaCutoff, or blend by alpha.
    AlphaMode alphaMode {AlphaMode::Opaque};
    /// Linear alpha threshold used by Mask materials; must be in [0,1].
    float alphaCutoff {0.5F};
    /// Optional Lit-only linear occlusion map. If set, it must be uploaded RGBA8 UNORM, sampled, and from the rendered
    /// Engine; the mesh must declare TexCoord0. Its red channel scales ambient lighting. An empty handle contributes
    /// white.
    Texture occlusionTexture;
    /// Finite multiplier for the occlusion map's effect, clamped to [0,1] when uploaded; defaults to full strength.
    float occlusionStrength {1.0F};
};

/// @brief Camera data for one rendered view.
struct RenderCamera
{
    /// Combined projection and view transform; matrices multiply column vectors.
    glm::mat4 viewProjection {1.0F};
    /// World-space camera position in meters.
    glm::vec3 position {};
};

/// @brief Shadow quality settings for one light.
struct ShadowSettings
{
    /// Optional shadow-map face width and height in texels (256, 512, 1024 or 2048).
    /// Empty selects 2048 for directional, 1024 for spot, or 512 per point-light face.
    std::optional<std::uint32_t> resolution;
    /// Nonnegative bias subtracted from projected [0,1] shadow depth before comparison; default 0.0005.
    /// Larger values reduce self-shadowing but can detach shadows from their casters.
    float depthBias {0.0005F};
    /// Nonnegative offset of the receiver along its world-space normal before projection, in meters; default 0.002.
    /// Larger values reduce self-shadowing but can detach shadows from their casters.
    float normalOffset {0.002F};
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
    /// Optional dynamic shadow map; empty means this light has no shadow pass.
    std::optional<ShadowSettings> shadow;
    /// Orthographic shadow width in meters; used only when `shadow` is set.
    float shadowCoverageMeters {30.0F};
};

/// @brief Point light with finite range and inverse-square distance attenuation.
struct PointLight
{
    /// World-space position in meters.
    glm::vec3 position {};
    /// Linear RGB light color.
    glm::vec3 color {1.0F};
    /// Nonnegative linear intensity multiplier.
    float intensity {1.0F};
    /// Positive finite distance in meters where the contribution reaches zero.
    float range {};
    /// Optional dynamic shadow map; empty means this light has no shadow pass.
    std::optional<ShadowSettings> shadow;
};

/// @brief Spot light shining along its direction inside a smooth cone.
struct SpotLight
{
    /// World-space position in meters.
    glm::vec3 position {};
    /// World-space direction from the light into the scene; normalized by the renderer.
    glm::vec3 direction {0.0F, 0.0F, -1.0F};
    /// Linear RGB light color.
    glm::vec3 color {1.0F};
    /// Nonnegative linear intensity multiplier.
    float intensity {1.0F};
    /// Positive finite distance in meters where the contribution reaches zero.
    float range {};
    /// Radians from the light axis where angular falloff begins.
    float innerConeAngleRadians {};
    /// Radians from the light axis where angular falloff reaches zero.
    float outerConeAngleRadians {std::numbers::pi_v<float> / 4.0F};
    /// Optional dynamic shadow map; empty means this light has no shadow pass.
    std::optional<ShadowSettings> shadow;
};

/// @brief One built-in light type accepted by RenderView and Scene.
using Light = std::variant<DirectionalLight, PointLight, SpotLight>;

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
/// The renderer copies draw values and retains mesh and texture handles through GPU completion.
struct RenderView
{
    /// Camera used for mesh projection and view-dependent lighting.
    RenderCamera camera;
    /// Ordered lights used by Lit materials. Each light casts a shadow only when its `shadow` settings are present.
    std::vector<Light> lights;
    /// Draws to render; an empty list presents only the configured background.
    std::vector<MeshDraw> meshes;
};
}
