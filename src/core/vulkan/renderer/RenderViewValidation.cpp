// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
// clang-format on

#include "RenderViewValidation.hpp"

#include <algorithm>
#include <cmath>
#include <expected>
#include <glm/ext/matrix_float3x3.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>
#include <glm/geometric.hpp>
#include <glm/matrix.hpp>
#include <panda/Error.hpp>
#include <panda/Geometry.hpp>
#include <panda/Projection.hpp>
#include <panda/RenderView.hpp>

#include "resources/MeshResource.hpp"
#include "resources/memory/MemoryPool.hpp"

namespace panda::core::vulkan::renderer
{
namespace
{
auto isFinite(const glm::vec3& vector) -> bool
{
    return std::isfinite(vector.x) && std::isfinite(vector.y) && std::isfinite(vector.z);
}

auto isFinite(const glm::vec4& vector) -> bool
{
    return std::isfinite(vector.x) && std::isfinite(vector.y) && std::isfinite(vector.z) && std::isfinite(vector.w);
}

auto isFinite(const Mat4& matrix) -> bool
{
    for (auto column = 0; column < 4; ++column)
    {
        if (!isFinite(matrix[column]))
        {
            return false;
        }
    }
    return true;
}

auto isFinite(const glm::mat3& matrix) -> bool
{
    for (auto column = 0; column < 3; ++column)
    {
        if (!isFinite(matrix[column]))
        {
            return false;
        }
    }
    return true;
}

auto isInvertible(const Mat4& matrix) -> bool
{
    const auto determinant = glm::determinant(matrix);
    return determinant != 0.0F && isFinite(glm::inverse(matrix));
}

auto isAffine(const Mat4& model) -> bool
{
    static constexpr auto tolerance = 1.0e-6F;
    return std::abs(model[0][3]) <= tolerance && std::abs(model[1][3]) <= tolerance &&
           std::abs(model[2][3]) <= tolerance && std::abs(model[3][3] - 1.0F) <= tolerance;
}

auto isRenderableModel(const Mat4& model) -> bool
{
    if (!isFinite(model) || !isAffine(model))
    {
        return false;
    }
    const auto linear = glm::mat3 {model};
    const auto normalMatrix = glm::transpose(glm::inverse(linear));
    return isFinite(normalMatrix);
}
}

auto validateRenderViewData(const RenderView& view) -> Result<void>
{
    if (!isFinite(view.camera.viewProjection) || !isInvertible(view.camera.viewProjection) ||
        !isFinite(view.camera.position) || !isFinite(view.directionalLight.direction) ||
        !isFinite(view.directionalLight.color) || !std::isfinite(view.directionalLight.intensity))
    {
        return std::unexpected {
            makeError(ErrorCode::InvalidArgument, "Render view contains non-finite or singular data")};
    }
    const auto directionLengthSquared = glm::dot(view.directionalLight.direction, view.directionalLight.direction);
    if (!std::isfinite(directionLengthSquared) || directionLengthSquared <= 1.0e-12F ||
        view.directionalLight.intensity < 0.0F || view.directionalLight.color.r < 0.0F ||
        view.directionalLight.color.g < 0.0F || view.directionalLight.color.b < 0.0F)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Directional light is invalid")};
    }
    if (std::ranges::any_of(view.meshes, [](const auto& draw) {
            return !isRenderableModel(draw.modelMatrix) || !isFinite(draw.tint) || draw.tint.r < 0.0F ||
                   draw.tint.g < 0.0F || draw.tint.b < 0.0F || draw.tint.a < 0.0F;
        }))
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Mesh draw contains invalid transform or tint")};
    }
    return {};
}

auto validateRenderViewMeshes(const RenderView& view, const resources::MemoryPool& pool) -> Result<void>
{
    if (std::ranges::any_of(view.meshes, [&pool](const auto& draw) {
            return !draw.mesh.isValid() || draw.mesh.getVertexStride() != standardMeshVertexStrideBytes ||
                   !::panda::detail::MeshResource::getResource(draw.mesh)->belongsTo(pool);
        }))
    {
        return std::unexpected {
            makeError(ErrorCode::InvalidArgument, "Mesh draw requires a standard mesh created by this Context")};
    }
    return {};
}
}
