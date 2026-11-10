#pragma once

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/quaternion_float.hpp>
#include <panda/Error.hpp>
#include <panda/Transform.hpp>
#include <utility>

namespace panda::scene::detail
{
[[nodiscard]] auto validateTransform(const Transform& transform) -> Result<std::pair<Transform, glm::mat4>>;
[[nodiscard]] auto decomposeTransform(const glm::mat4& matrix) -> Result<std::pair<Transform, glm::mat4>>;
[[nodiscard]] auto checkedInverse(const glm::mat4& matrix) -> Result<glm::mat4>;
[[nodiscard]] auto extractPoseRotation(const glm::mat4& world) -> Result<glm::quat>;
}
