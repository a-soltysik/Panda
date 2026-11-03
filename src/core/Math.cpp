#include "Math.hpp"

#include <cmath>
#include <glm/ext/matrix_float3x3.hpp>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/quaternion_float.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>
#include <limits>

namespace panda::detail
{
auto isFinite(const glm::vec3& value) -> bool
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

auto isFinite(const glm::vec4& value) -> bool
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) && std::isfinite(value.w);
}

auto isFinite(const glm::quat& value) -> bool
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) && std::isfinite(value.w);
}

auto isFinite(const glm::mat3& value) -> bool
{
    return isFinite(value[0]) && isFinite(value[1]) && isFinite(value[2]);
}

auto isFinite(const glm::mat4& value) -> bool
{
    return isFinite(value[0]) && isFinite(value[1]) && isFinite(value[2]) && isFinite(value[3]);
}

auto isRepresentableAsNonzeroFloat(double value) -> bool
{
    const auto magnitude = std::abs(value);
    return std::isfinite(magnitude) && magnitude >= static_cast<double>(std::numeric_limits<float>::denorm_min()) &&
           magnitude <= static_cast<double>(std::numeric_limits<float>::max());
}
}
