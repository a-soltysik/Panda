#include <cmath>
#include <expected>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/geometric.hpp>
#include <panda/Error.hpp>
#include <panda/Projection.hpp>
#include <panda/RenderView.hpp>
#include <panda/tools/Camera.hpp>

#include "Math.hpp"

namespace panda::tools
{
namespace
{
using panda::detail::isFinite;

auto isValidLookAt(glm::vec3 position, glm::vec3 target, glm::vec3 up) -> bool
{
    if (!isFinite(position) || !isFinite(target) || !isFinite(up))
    {
        return false;
    }
    const auto direction = target - position;
    const auto right = glm::cross(direction, up);
    const auto directionLengthSquared = glm::dot(direction, direction);
    const auto rightLengthSquared = glm::dot(right, right);
    return isFinite(direction) && isFinite(right) && std::isfinite(directionLengthSquared) &&
           directionLengthSquared > 0.0F && std::isfinite(rightLengthSquared) && rightLengthSquared > 0.0F;
}

}

auto createPerspectiveCamera(const PerspectiveCameraDesc& description) -> Result<RenderCamera>
{
    if (!isValidLookAt(description.position, description.target, description.up))
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Invalid perspective camera pose")};
    }
    const auto projection = createPerspectiveProjection(description.verticalFieldOfViewRadians,
                                                        description.aspectRatio,
                                                        description.nearPlane,
                                                        description.farPlane);
    if (!projection)
    {
        return std::unexpected {projection.error()};
    }
    const auto viewProjection = *projection * glm::lookAtRH(description.position, description.target, description.up);
    if (!isFinite(viewProjection))
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Perspective camera matrix is not finite")};
    }
    return RenderCamera {.viewProjection = viewProjection, .position = description.position};
}
}
