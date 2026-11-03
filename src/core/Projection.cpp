#include <cmath>
#include <expected>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_float4x4.hpp>
#include <numbers>
#include <panda/Error.hpp>
#include <panda/Projection.hpp>

#include "Math.hpp"

namespace
{
using panda::detail::isRepresentableAsNonzeroFloat;

auto validRange(float size, float aspectRatio, float nearPlane, float farPlane) -> bool
{
    return std::isfinite(size) && size > 0.0F && std::isfinite(aspectRatio) && aspectRatio > 0.0F &&
           std::isfinite(nearPlane) && nearPlane > 0.0F && std::isfinite(farPlane) && farPlane > nearPlane;
}
}

namespace panda
{
auto createPerspectiveProjection(float verticalFieldOfViewRadians, float aspectRatio, float nearPlane, float farPlane)
    -> Result<glm::mat4>
{
    if (!validRange(verticalFieldOfViewRadians, aspectRatio, nearPlane, farPlane) ||
        verticalFieldOfViewRadians >= std::numbers::pi_v<float>)
    {
        return std::unexpected(makeError(ErrorCode::InvalidArgument, "Invalid perspective projection parameters"));
    }
    auto matrix = glm::perspectiveRH_ZO(static_cast<double>(verticalFieldOfViewRadians),
                                        static_cast<double>(aspectRatio),
                                        static_cast<double>(nearPlane),
                                        static_cast<double>(farPlane));
    matrix[1][1] = -matrix[1][1];
    if (!isRepresentableAsNonzeroFloat(matrix[0][0]) || !isRepresentableAsNonzeroFloat(matrix[1][1]) ||
        !isRepresentableAsNonzeroFloat(matrix[2][2]) || !isRepresentableAsNonzeroFloat(matrix[3][2]))
    {
        return std::unexpected(makeError(ErrorCode::InvalidArgument, "Perspective projection is not representable"));
    }
    return glm::mat4 {matrix};
}

auto createOrthographicProjection(float height, float aspectRatio, float nearPlane, float farPlane) -> Result<glm::mat4>
{
    if (!validRange(height, aspectRatio, nearPlane, farPlane))
    {
        return std::unexpected(makeError(ErrorCode::InvalidArgument, "Invalid orthographic projection parameters"));
    }
    const auto halfHeight = static_cast<double>(height) / 2.0;
    const auto halfWidth = halfHeight * static_cast<double>(aspectRatio);
    auto matrix = glm::orthoRH_ZO(-halfWidth,
                                  halfWidth,
                                  halfHeight,
                                  -halfHeight,
                                  static_cast<double>(nearPlane),
                                  static_cast<double>(farPlane));
    if (!isRepresentableAsNonzeroFloat(matrix[0][0]) || !isRepresentableAsNonzeroFloat(matrix[1][1]) ||
        !isRepresentableAsNonzeroFloat(matrix[2][2]) || !isRepresentableAsNonzeroFloat(matrix[3][2]))
    {
        return std::unexpected(makeError(ErrorCode::InvalidArgument, "Orthographic projection is not representable"));
    }
    return glm::mat4 {matrix};
}
}
