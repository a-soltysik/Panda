// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include "RenderViewValidation.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <expected>
#include <glm/ext/matrix_float3x3.hpp>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/geometric.hpp>
#include <glm/matrix.hpp>
#include <panda/Error.hpp>
#include <panda/Mesh.hpp>
#include <panda/RenderView.hpp>
#include <panda/Texture.hpp>

#include "Math.hpp"
#include "resources/MeshResource.hpp"
#include "resources/TextureResource.hpp"
#include "resources/memory/MemoryPool.hpp"

namespace panda::core::vulkan::renderer
{
namespace
{
using panda::detail::isFinite;

auto isInvertible(const glm::mat4& matrix) -> bool
{
    const auto determinant = glm::determinant(matrix);
    return determinant != 0.0F && isFinite(glm::inverse(matrix));
}

auto isAffine(const glm::mat4& model) -> bool
{
    static constexpr auto tolerance = 1.0e-6F;
    return std::abs(model[0][3]) <= tolerance && std::abs(model[1][3]) <= tolerance &&
           std::abs(model[2][3]) <= tolerance && std::abs(model[3][3] - 1.0F) <= tolerance;
}

auto isRenderableModel(const glm::mat4& model) -> bool
{
    if (!isFinite(model) || !isAffine(model))
    {
        return false;
    }
    const auto linear = glm::mat3 {model};
    const auto normalMatrix = glm::transpose(glm::inverse(linear));
    return isFinite(normalMatrix);
}

auto isValidMaterial(const Material& material) -> bool
{
    return (material.model == MaterialModel::Lit || material.model == MaterialModel::Unlit) &&
           (material.renderFace == RenderFace::Front || material.renderFace == RenderFace::Back ||
            material.renderFace == RenderFace::Both) &&
           (material.alphaMode == AlphaMode::Opaque || material.alphaMode == AlphaMode::Mask ||
            material.alphaMode == AlphaMode::Blend) &&
           isFinite(material.baseColor) && material.baseColor.r >= 0.0F && material.baseColor.g >= 0.0F &&
           material.baseColor.b >= 0.0F && material.baseColor.a >= 0.0F && isFinite(material.emissive) &&
           material.emissive.r >= 0.0F && material.emissive.g >= 0.0F && material.emissive.b >= 0.0F &&
           std::isfinite(material.metallic) && std::isfinite(material.roughness) &&
           std::isfinite(material.normalScale) && std::isfinite(material.alphaCutoff) && material.alphaCutoff >= 0.0F &&
           material.alphaCutoff <= 1.0F;
}

auto hasAttribute(const Mesh& mesh, VertexAttributeSemantic semantic) -> bool
{
    return ::panda::detail::MeshResource::getResource(mesh)->getLayout().hasVertexAttribute(semantic);
}

auto isCompatibleMeshDraw(const MeshDraw& draw, const resources::MemoryPool& pool) -> bool
{
    if (!draw.mesh.isValid() || !::panda::detail::MeshResource::getResource(draw.mesh)->belongsTo(pool))
    {
        return false;
    }
    return hasAttribute(draw.mesh, VertexAttributeSemantic::Position) &&
           (draw.material.model != MaterialModel::Lit || hasAttribute(draw.mesh, VertexAttributeSemantic::Normal));
}

auto isCompatibleTexture(const Texture& handle,
                         const resources::MemoryPool& pool,
                         const std::array<vk::Format, 2>& formats) -> bool
{
    if (!handle.isValid())
    {
        return true;
    }
    const auto& texture = *::panda::detail::TextureResource::getResource(handle);
    return texture.belongsTo(pool) && (texture.getUsage() & vk::ImageUsageFlagBits::eSampled) &&
           std::ranges::find(formats, texture.getFormat()) != formats.end();
}

auto areCompatibleSrgbMaps(const MeshDraw& draw, const resources::MemoryPool& pool) -> bool
{
    static constexpr auto formats = std::array {vk::Format::eR8G8B8A8Srgb, vk::Format::eB8G8R8A8Srgb};
    const auto hasUv = hasAttribute(draw.mesh, VertexAttributeSemantic::TexCoord0);
    return (!draw.material.baseColorTexture.isValid() || hasUv) &&
           isCompatibleTexture(draw.material.baseColorTexture, pool, formats) &&
           (!draw.material.emissiveTexture.isValid() || hasUv) &&
           isCompatibleTexture(draw.material.emissiveTexture, pool, formats);
}

auto isCompatibleMetallicRoughnessTexture(const MeshDraw& draw, const resources::MemoryPool& pool) -> bool
{
    static constexpr auto formats = std::array {vk::Format::eR8G8B8A8Unorm, vk::Format::eB8G8R8A8Unorm};
    return !draw.material.metallicRoughnessTexture.isValid() ||
           (draw.material.model == MaterialModel::Lit && hasAttribute(draw.mesh, VertexAttributeSemantic::TexCoord0) &&
            isCompatibleTexture(draw.material.metallicRoughnessTexture, pool, formats));
}

auto isCompatibleNormalTexture(const MeshDraw& draw, const resources::MemoryPool& pool) -> bool
{
    static constexpr auto formats = std::array {vk::Format::eR8G8B8A8Unorm, vk::Format::eB8G8R8A8Unorm};
    return !draw.material.normalTexture.isValid() || (draw.material.model == MaterialModel::Lit &&
                                                      isCompatibleTexture(draw.material.normalTexture, pool, formats));
}

template <typename CompatibilityCheck>
auto hasIncompatibleDraws(const RenderView& view,
                          const resources::MemoryPool& pool,
                          CompatibilityCheck compatibilityCheck) -> bool
{
    return std::ranges::any_of(view.meshes, [&pool, compatibilityCheck](const auto& draw) {
        return !compatibilityCheck(draw, pool);
    });
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
                   draw.tint.g < 0.0F || draw.tint.b < 0.0F || draw.tint.a < 0.0F || !isValidMaterial(draw.material);
        }))
    {
        return std::unexpected {
            makeError(ErrorCode::InvalidArgument, "Mesh draw contains invalid transform or material")};
    }
    return {};
}

auto validateRenderViewMeshes(const RenderView& view, const resources::MemoryPool& pool) -> Result<void>
{
    if (hasIncompatibleDraws(view, pool, isCompatibleMeshDraw))
    {
        return std::unexpected {
            makeError(ErrorCode::InvalidArgument, "Mesh draw requires a compatible vertex layout from this Context")};
    }
    if (hasIncompatibleDraws(view, pool, areCompatibleSrgbMaps))
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument,
                                          "Base-color and emissive maps require UVs and a sampled RGBA8 sRGB texture "
                                          "from this Context")};
    }
    if (hasIncompatibleDraws(view, pool, isCompatibleMetallicRoughnessTexture))
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument,
                                          "Metallic-roughness map requires UVs and a sampled RGBA8 UNORM texture for "
                                          "a Lit material from this Context")};
    }
    if (hasIncompatibleDraws(view, pool, isCompatibleNormalTexture))
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument,
                                          "Normal map requires a sampled RGBA8 UNORM texture for a Lit material "
                                          "from this Context")};
    }
    return {};
}

auto validateRenderViewTextures(const RenderView& view) -> Result<void>
{
    if (std::ranges::any_of(view.meshes, [](const auto& draw) {
            return (draw.material.baseColorTexture.isValid() &&
                    !::panda::detail::TextureResource::getResource(draw.material.baseColorTexture)->isUploaded()) ||
                   (draw.material.metallicRoughnessTexture.isValid() &&
                    !::panda::detail::TextureResource::getResource(draw.material.metallicRoughnessTexture)
                         ->isUploaded()) ||
                   (draw.material.emissiveTexture.isValid() &&
                    !::panda::detail::TextureResource::getResource(draw.material.emissiveTexture)->isUploaded()) ||
                   (draw.material.normalTexture.isValid() &&
                    !::panda::detail::TextureResource::getResource(draw.material.normalTexture)->isUploaded());
        }))
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Material texture has not been uploaded")};
    }
    return {};
}
}
