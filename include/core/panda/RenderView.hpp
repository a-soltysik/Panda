#pragma once

/// @file
/// Application-owned camera, lighting and mesh draw data.

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <panda/Mesh.hpp>
#include <vector>

namespace panda
{
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

/// @brief One uploaded standard-format mesh with an object-to-world transform and linear tint.
struct MeshDraw
{
    /// Retained GPU mesh; its vertex stride must be standardMeshVertexStrideBytes.
    Mesh mesh;
    /// Full object-to-world transform in column-major order.
    glm::mat4 modelMatrix {1.0F};
    /// Linear RGBA tint multiplied by vertex color and the built-in material.
    glm::vec4 tint {1.0F, 1.0F, 1.0F, 1.0F};
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
