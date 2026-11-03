#include <cmath>
#include <expected>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/quaternion_float.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/gtc/quaternion.hpp>
#include <panda/Error.hpp>
#include <panda/Transform.hpp>

#include "Math.hpp"

namespace panda
{
namespace
{
constexpr auto minimumRotationLength = 1.0e-8;
constexpr auto minimumScaleMagnitude = 1.0e-6F;

auto hasValidScale(glm::vec3 scale) -> bool
{
    return std::abs(scale.x) >= minimumScaleMagnitude && std::abs(scale.y) >= minimumScaleMagnitude &&
           std::abs(scale.z) >= minimumScaleMagnitude;
}

auto normalize(glm::quat rotation, double length) -> glm::quat
{
    return {static_cast<float>(static_cast<double>(rotation.w) / length),
            static_cast<float>(static_cast<double>(rotation.x) / length),
            static_cast<float>(static_cast<double>(rotation.y) / length),
            static_cast<float>(static_cast<double>(rotation.z) / length)};
}
}

auto createModelMatrix(const Transform& transform) -> Result<glm::mat4>
{
    if (!detail::isFinite(transform.position) || !detail::isFinite(transform.rotation) ||
        !detail::isFinite(transform.scale))
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Transform values must be finite")};
    }
    const auto rotationLength =
        std::hypot(std::hypot(static_cast<double>(transform.rotation.x), static_cast<double>(transform.rotation.y)),
                   std::hypot(static_cast<double>(transform.rotation.z), static_cast<double>(transform.rotation.w)));
    if (rotationLength < minimumRotationLength || !hasValidScale(transform.scale))
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Transform rotation or scale is singular")};
    }
    const auto translation = glm::translate(glm::mat4 {1.0F}, transform.position);
    const auto rotationMatrix = glm::mat4_cast(normalize(transform.rotation, rotationLength));
    const auto scaleMatrix = glm::scale(glm::mat4 {1.0F}, transform.scale);
    const auto modelMatrix = translation * rotationMatrix * scaleMatrix;
    if (!detail::isFinite(modelMatrix))
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Transform matrix is not representable")};
    }
    return modelMatrix;
}
}
