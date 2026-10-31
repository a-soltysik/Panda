#pragma once

/// @file
/// Checked camera projection matrices for Vulkan rendering.

#if defined(GLM_FORCE_QUAT_DATA_WXYZ) || defined(GLM_FORCE_QUAT_DATA_XYZW)
#    error "Panda requires the default GLM quaternion configuration"
#endif

#include <glm/ext/matrix_float4x4.hpp>
#include <panda/Error.hpp>

namespace panda
{
/// @brief Packed single-precision column-major matrix used by Panda's public math API.
using Mat4 = glm::mat<4, 4, float, glm::packed_highp>;
static_assert(sizeof(Mat4) == 64 && alignof(Mat4) == 4);

/// @brief Builds a right-handed Vulkan perspective projection with one Y correction.
/// @param verticalFieldOfViewRadians Finite vertical field of view in radians, between zero and pi.
/// @param aspectRatio Finite positive viewport width divided by height.
/// @param nearPlane Finite positive near distance in meters.
/// @param farPlane Finite far distance in meters, greater than nearPlane.
/// @return Matrix mapping near/far depth to 0/1, or InvalidArgument for invalid or unrepresentable values.
[[nodiscard]] auto createPerspectiveProjection(float verticalFieldOfViewRadians,
                                               float aspectRatio,
                                               float nearPlane,
                                               float farPlane) -> Result<Mat4>;

/// @brief Builds a centered right-handed Vulkan orthographic projection with one Y correction.
/// @param height Finite positive vertical span in meters.
/// @param aspectRatio Finite positive viewport width divided by height.
/// @param nearPlane Finite positive near distance in meters.
/// @param farPlane Finite far distance in meters, greater than nearPlane.
/// @return Matrix mapping near/far depth to 0/1, or InvalidArgument for invalid or unrepresentable values.
[[nodiscard]] auto createOrthographicProjection(float height, float aspectRatio, float nearPlane, float farPlane)
    -> Result<Mat4>;
}
