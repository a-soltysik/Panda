#pragma once

/// @file
/// Stateless camera construction helpers for applications and prototypes.

#include <glm/ext/vector_float3.hpp>
#include <panda/Error.hpp>
#include <panda/RenderView.hpp>

namespace panda::tools
{
/// @brief Inputs for constructing a right-handed perspective camera.
struct PerspectiveCameraDesc
{
    /// @brief Camera position in world space.
    glm::vec3 position;
    /// @brief World-space point the camera looks at.
    glm::vec3 target;
    /// @brief World-space direction that defines camera up.
    glm::vec3 up;
    /// @brief Vertical field of view in radians.
    float verticalFieldOfViewRadians;
    /// @brief View width divided by height.
    float aspectRatio;
    /// @brief Positive near clipping distance.
    float nearPlane;
    /// @brief Far clipping distance, greater than nearPlane.
    float farPlane;
};

/// @brief Creates a right-handed perspective camera looking from position to target.
/// @param description Camera pose and perspective projection parameters.
/// @return A validated RenderCamera or InvalidArgument for invalid projection or pose data.
[[nodiscard]] auto createPerspectiveCamera(const PerspectiveCameraDesc& description) -> Result<RenderCamera>;
}
