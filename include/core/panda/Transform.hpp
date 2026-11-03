#pragma once

/// @file
/// Position, orientation and scale values for mesh transforms.

#if defined(GLM_FORCE_QUAT_DATA_WXYZ) || defined(GLM_FORCE_QUAT_DATA_XYZW)
#    error "Panda requires the default GLM quaternion configuration"
#endif

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/quaternion_float.hpp>
#include <glm/ext/vector_float3.hpp>
#include <panda/Error.hpp>

namespace panda
{
/// @brief Translation, rotation and scale for one object.
struct Transform
{
    /// Translation in meters.
    glm::vec3 position {};
    /// Rotation; defaults to identity. Finite non-unit values are normalized during conversion.
    glm::quat rotation {1.0F, 0.0F, 0.0F, 0.0F};
    /// Per-axis scale; defaults to one. Negative values reflect the object.
    glm::vec3 scale {1.0F};
};

/// @brief Builds a model matrix from translation, rotation and scale using T * R * S.
/// @param transform Position, rotation and scale.
/// @return Matrix, or InvalidArgument for non-finite values, near-zero rotation or near-zero scale.
[[nodiscard]] auto createModelMatrix(const Transform& transform) -> Result<glm::mat4>;
}
