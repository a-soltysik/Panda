#include "renderer/ShadowView.hpp"

#include <cmath>
#include <expected>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>
#include <glm/geometric.hpp>
#include <glm/matrix.hpp>
#include <panda/Error.hpp>
#include <panda/Projection.hpp>
#include <panda/RenderView.hpp>

#include "Math.hpp"

namespace panda::core::vulkan::renderer
{
namespace
{
using panda::detail::isFinite;

auto normalizedLightDirection(glm::vec3 direction) -> std::expected<glm::vec3, Error>
{
    const auto length = glm::length(direction);
    if (!std::isfinite(length) || length <= 0.0F)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Directional shadow light has no direction")};
    }
    return direction / length;
}

auto cameraForward(const RenderCamera& camera) -> std::expected<glm::vec3, Error>
{
    const auto inverseViewProjection = glm::inverse(camera.viewProjection);
    const auto nearCenter = inverseViewProjection * glm::vec4 {0.0F, 0.0F, 0.0F, 1.0F};
    if (!std::isfinite(nearCenter.w) || std::abs(nearCenter.w) <= 1.0e-6F)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Camera cannot define a shadow view")};
    }
    const auto direction = glm::vec3 {nearCenter} / nearCenter.w - camera.position;
    const auto length = glm::length(direction);
    if (!std::isfinite(length) || length <= 0.0F)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Camera cannot define a shadow view")};
    }
    const auto forward = direction / length;
    if (!isFinite(forward))
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Camera cannot define a shadow view")};
    }
    return forward;
}

auto lightUp(glm::vec3 direction) -> glm::vec3
{
    constexpr auto parallelUpThreshold = 0.99F;
    return std::abs(glm::dot(direction, glm::vec3 {0.0F, 1.0F, 0.0F})) > parallelUpThreshold
               ? glm::vec3 {1.0F, 0.0F, 0.0F}
               : glm::vec3 {0.0F, 1.0F, 0.0F};
}

auto buildDirectionalShadowMatrix(glm::vec3 cameraPosition, glm::vec3 forward, glm::vec3 direction)
    -> std::expected<glm::mat4, Error>
{
    constexpr auto shadowDistance = 15.0F;
    constexpr auto shadowDepth = 40.0F;
    constexpr auto shadowWidth = 30.0F;
    const auto center = cameraPosition + forward * shadowDistance;
    const auto eye = center - direction * shadowDepth;
    const auto projection = createOrthographicProjection(shadowWidth, 1.0F, 0.1F, 80.0F);
    if (!projection)
    {
        return std::unexpected {projection.error()};
    }
    // shortcut: one camera-centered 30 m view, add cascades or app bounds for wider scenes.
    const auto lightViewProjection = *projection * glm::lookAtRH(eye, center, lightUp(direction));
    if (!isFinite(lightViewProjection))
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Shadow view is not finite")};
    }
    return lightViewProjection;
}
}

auto createDirectionalShadowView(const RenderCamera& camera, const DirectionalLight& light)
    -> std::expected<glm::mat4, Error>
{
    const auto direction = normalizedLightDirection(light.direction);
    if (!direction)
    {
        return std::unexpected {direction.error()};
    }
    const auto forward = cameraForward(camera);
    if (!forward)
    {
        return std::unexpected {forward.error()};
    }

    return buildDirectionalShadowMatrix(camera.position, *forward, *direction);
}
}
