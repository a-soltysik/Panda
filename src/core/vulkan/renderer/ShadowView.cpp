#include "renderer/ShadowView.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <expected>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/matrix.hpp>
#include <numbers>
#include <panda/Error.hpp>
#include <panda/Projection.hpp>
#include <panda/RenderView.hpp>

#include "Math.hpp"

namespace panda::core::vulkan::renderer
{
namespace
{
using panda::detail::isFinite;

constexpr auto localShadowNearPlaneLimit = 0.1F;
constexpr auto localShadowNearRangeRatio = 0.01F;
constexpr auto cameraHomogeneousCoordinateLimit = 1.0e-6F;

struct PerspectiveShadowViewDescription
{
    glm::vec3 position;
    glm::vec3 direction;
    glm::vec3 up;
    float fieldOfViewRadians;
    float range;
};

auto normalizedDirection(glm::vec3 direction) -> std::expected<glm::vec3, Error>
{
    const auto length = glm::length(direction);
    if (!std::isfinite(length) || length <= 0.0F)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Shadow light has no direction")};
    }
    const auto normalized = direction / length;
    if (!isFinite(normalized))
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Shadow light direction is not finite")};
    }
    return normalized;
}

auto buildPerspectiveShadowView(const PerspectiveShadowViewDescription& description) -> std::expected<glm::mat4, Error>
{
    if (!isFinite(description.position) || !isFinite(description.direction) || !isFinite(description.up))
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Shadow view inputs are not finite")};
    }
    const auto nearPlane = std::min(localShadowNearPlaneLimit, description.range * localShadowNearRangeRatio);
    const auto projection =
        createPerspectiveProjection(description.fieldOfViewRadians, 1.0F, nearPlane, description.range);
    if (!projection)
    {
        return std::unexpected {projection.error()};
    }
    const auto viewProjection =
        *projection * glm::lookAtRH(description.position, description.position + description.direction, description.up);
    if (!isFinite(viewProjection))
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Shadow view is not finite")};
    }
    return viewProjection;
}

auto buildPointShadowView(const PointLight& light, glm::vec3 direction, glm::vec3 up) -> std::expected<glm::mat4, Error>
{
    return buildPerspectiveShadowView({.position = light.position,
                                       .direction = direction,
                                       .up = up,
                                       .fieldOfViewRadians = glm::half_pi<float>(),
                                       .range = light.range});
}

auto cameraForward(const RenderCamera& camera) -> std::expected<glm::vec3, Error>
{
    const auto inverseViewProjection = glm::inverse(camera.viewProjection);
    const auto nearCenter = inverseViewProjection * glm::vec4 {0.0F, 0.0F, 0.0F, 1.0F};
    if (!std::isfinite(nearCenter.w) || std::abs(nearCenter.w) <= cameraHomogeneousCoordinateLimit)
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
    static constexpr auto parallelUpThreshold = 0.99F;
    return std::abs(glm::dot(direction, glm::vec3 {0.0F, 1.0F, 0.0F})) > parallelUpThreshold
               ? glm::vec3 {1.0F, 0.0F, 0.0F}
               : glm::vec3 {0.0F, 1.0F, 0.0F};
}

auto buildDirectionalShadowMatrix(glm::vec3 cameraPosition,
                                  glm::vec3 forward,
                                  glm::vec3 direction,
                                  float coverageMeters) -> std::expected<glm::mat4, Error>
{
    static constexpr auto shadowCenterRatio = 0.5F;
    static constexpr auto shadowDepth = 40.0F;
    const auto center = cameraPosition + forward * (coverageMeters * shadowCenterRatio);
    const auto eye = center - direction * shadowDepth;
    const auto projection = createOrthographicProjection(coverageMeters, 1.0F, 0.1F, 80.0F);
    if (!projection)
    {
        return std::unexpected {projection.error()};
    }
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
    const auto direction = normalizedDirection(light.direction);
    if (!direction)
    {
        return std::unexpected {direction.error()};
    }
    const auto forward = cameraForward(camera);
    if (!forward)
    {
        return std::unexpected {forward.error()};
    }

    return buildDirectionalShadowMatrix(camera.position, *forward, *direction, light.shadowCoverageMeters);
}

auto createSpotShadowView(const SpotLight& light) -> std::expected<glm::mat4, Error>
{
    const auto direction = normalizedDirection(light.direction);
    if (!direction)
    {
        return std::unexpected {direction.error()};
    }
    static constexpr auto coneHalfAngleToFieldOfView = 2.0F;
    auto fieldOfViewRadians = light.outerConeAngleRadians * coneHalfAngleToFieldOfView;
    if (fieldOfViewRadians == std::numbers::pi_v<float>)
    {
        fieldOfViewRadians = std::nextafter(fieldOfViewRadians, 0.0F);
    }
    return buildPerspectiveShadowView({.position = light.position,
                                       .direction = *direction,
                                       .up = lightUp(*direction),
                                       .fieldOfViewRadians = fieldOfViewRadians,
                                       .range = light.range});
}

auto createPointShadowViews(const PointLight& light)
    -> std::expected<std::array<glm::mat4, pointShadowFaceCount>, Error>
{
    static constexpr auto directions = std::array {
        glm::vec3 {1.0F,  0.0F,  0.0F },
        glm::vec3 {-1.0F, 0.0F,  0.0F },
        glm::vec3 {0.0F,  1.0F,  0.0F },
        glm::vec3 {0.0F,  -1.0F, 0.0F },
        glm::vec3 {0.0F,  0.0F,  1.0F },
        glm::vec3 {0.0F,  0.0F,  -1.0F}
    };
    static constexpr auto upDirections = std::array {
        glm::vec3 {0.0F, -1.0F, 0.0F },
        glm::vec3 {0.0F, -1.0F, 0.0F },
        glm::vec3 {0.0F, 0.0F,  1.0F },
        glm::vec3 {0.0F, 0.0F,  -1.0F},
        glm::vec3 {0.0F, -1.0F, 0.0F },
        glm::vec3 {0.0F, -1.0F, 0.0F }
    };

    auto views = std::array<glm::mat4, pointShadowFaceCount> {};
    for (auto index = std::size_t {}; index < views.size(); ++index)
    {
        const auto view = buildPointShadowView(light, directions.at(index), upDirections.at(index));
        if (!view)
        {
            return std::unexpected {view.error()};
        }
        views.at(index) = *view;
    }
    return views;
}
}
