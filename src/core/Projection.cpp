#include <cmath>
#include <expected>
#include <glm/ext/matrix_float4x4.hpp>
#include <limits>
#include <numbers>
#include <panda/Error.hpp>
#include <panda/Projection.hpp>

namespace
{
auto validRange(float size, float aspectRatio, float nearPlane, float farPlane) -> bool
{
    return std::isfinite(size) && size > 0.0F && std::isfinite(aspectRatio) && aspectRatio > 0.0F &&
           std::isfinite(nearPlane) && nearPlane > 0.0F && std::isfinite(farPlane) && farPlane > nearPlane;
}

auto representable(double value) -> bool
{
    return value >= static_cast<double>(std::numeric_limits<float>::denorm_min()) &&
           value <= static_cast<double>(std::numeric_limits<float>::max());
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
    const auto tangent = std::tan(static_cast<double>(verticalFieldOfViewRadians) * 0.5);
    const auto depth = static_cast<double>(farPlane) - static_cast<double>(nearPlane);
    const auto xScale = 1.0 / (static_cast<double>(aspectRatio) * tangent);
    const auto yScale = 1.0 / tangent;
    const auto depthScale = static_cast<double>(farPlane) / depth;
    const auto depthOffset = static_cast<double>(farPlane) * static_cast<double>(nearPlane) / depth;
    if (!representable(xScale) || !representable(yScale) || !representable(depthScale) || !representable(depthOffset))
    {
        return std::unexpected(makeError(ErrorCode::InvalidArgument, "Perspective projection is not representable"));
    }
    auto matrix = glm::mat4 {0.0F};
    matrix[0][0] = static_cast<float>(xScale);
    matrix[1][1] = static_cast<float>(-yScale);
    matrix[2][2] = static_cast<float>(-depthScale);
    matrix[2][3] = -1.0F;
    matrix[3][2] = static_cast<float>(-depthOffset);
    return matrix;
}

auto createOrthographicProjection(float height, float aspectRatio, float nearPlane, float farPlane) -> Result<glm::mat4>
{
    if (!validRange(height, aspectRatio, nearPlane, farPlane))
    {
        return std::unexpected(makeError(ErrorCode::InvalidArgument, "Invalid orthographic projection parameters"));
    }
    const auto depth = static_cast<double>(farPlane) - static_cast<double>(nearPlane);
    const auto xScale = 2.0 / (static_cast<double>(height) * static_cast<double>(aspectRatio));
    const auto yScale = 2.0 / static_cast<double>(height);
    const auto depthScale = 1.0 / depth;
    const auto depthOffset = static_cast<double>(nearPlane) / depth;
    if (!representable(xScale) || !representable(yScale) || !representable(depthScale) || !representable(depthOffset))
    {
        return std::unexpected(makeError(ErrorCode::InvalidArgument, "Orthographic projection is not representable"));
    }
    auto matrix = glm::mat4 {1.0F};
    matrix[0][0] = static_cast<float>(xScale);
    matrix[1][1] = static_cast<float>(-yScale);
    matrix[2][2] = static_cast<float>(-depthScale);
    matrix[3][2] = static_cast<float>(-depthOffset);
    return matrix;
}
}
