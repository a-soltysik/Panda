#pragma once

#include <glm/ext/matrix_float3x3.hpp>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/quaternion_float.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>

namespace panda::detail
{
[[nodiscard]] auto isFinite(const glm::vec3& value) -> bool;
[[nodiscard]] auto isFinite(const glm::vec4& value) -> bool;
[[nodiscard]] auto isFinite(const glm::quat& value) -> bool;
[[nodiscard]] auto isFinite(const glm::mat3& value) -> bool;
[[nodiscard]] auto isFinite(const glm::mat4& value) -> bool;
[[nodiscard]] auto isRepresentableAsNonzeroFloat(double value) -> bool;
}
