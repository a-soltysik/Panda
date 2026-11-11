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
           std::isfinite(material.normalScale) && std::isfinite(material.occlusionStrength) &&
           std::isfinite(material.alphaCutoff) && material.alphaCutoff >= 0.0F && material.alphaCutoff <= 1.0F;
}

auto hasAttribute(const Mesh& mesh, VertexAttributeSemantic semantic) -> bool
{
    return ::panda::detail::MeshResource::getResource(mesh)->getLayout().hasVertexAttribute(semantic);
}

auto validateMeshDraw(const MeshDraw& draw, const resources::MemoryPool& pool) -> Result<void>
{
    if (!draw.mesh.isValid() || !::panda::detail::MeshResource::getResource(draw.mesh)->belongsTo(pool))
    {
        return std::unexpected {
            makeError(ErrorCode::InvalidArgument, "Mesh draw requires a compatible vertex layout from this Context")};
    }
    if (!hasAttribute(draw.mesh, VertexAttributeSemantic::Position) ||
        (draw.material.model == MaterialModel::Lit && !hasAttribute(draw.mesh, VertexAttributeSemantic::Normal)))
    {
        return std::unexpected {
            makeError(ErrorCode::InvalidArgument, "Mesh draw requires a compatible vertex layout from this Context")};
    }
    return {};
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

auto validateSrgbMaps(const MeshDraw& draw, const resources::MemoryPool& pool) -> Result<void>
{
    static constexpr auto formats = std::array {vk::Format::eR8G8B8A8Srgb, vk::Format::eB8G8R8A8Srgb};
    const auto hasUv = hasAttribute(draw.mesh, VertexAttributeSemantic::TexCoord0);
    if ((!draw.material.baseColorTexture.isValid() || hasUv) &&
        isCompatibleTexture(draw.material.baseColorTexture, pool, formats) &&
        (!draw.material.emissiveTexture.isValid() || hasUv) &&
        isCompatibleTexture(draw.material.emissiveTexture, pool, formats))
    {
        return {};
    }
    return std::unexpected {makeError(ErrorCode::InvalidArgument,
                                      "Base-color and emissive maps require UVs and a sampled RGBA8 sRGB texture "
                                      "from this Context")};
}

auto validateMetallicRoughnessTexture(const MeshDraw& draw, const resources::MemoryPool& pool) -> Result<void>
{
    static constexpr auto formats = std::array {vk::Format::eR8G8B8A8Unorm, vk::Format::eB8G8R8A8Unorm};
    if (!draw.material.metallicRoughnessTexture.isValid() ||
        (draw.material.model == MaterialModel::Lit && hasAttribute(draw.mesh, VertexAttributeSemantic::TexCoord0) &&
         isCompatibleTexture(draw.material.metallicRoughnessTexture, pool, formats)))
    {
        return {};
    }
    return std::unexpected {
        makeError(ErrorCode::InvalidArgument,
                  "Metallic-roughness map requires UVs and a sampled RGBA8 UNORM texture for a Lit material from "
                  "this Context")};
}

auto validateNormalTexture(const MeshDraw& draw, const resources::MemoryPool& pool) -> Result<void>
{
    static constexpr auto formats = std::array {vk::Format::eR8G8B8A8Unorm, vk::Format::eB8G8R8A8Unorm};
    if (!draw.material.normalTexture.isValid() ||
        (draw.material.model == MaterialModel::Lit && isCompatibleTexture(draw.material.normalTexture, pool, formats)))
    {
        return {};
    }
    return std::unexpected {
        makeError(ErrorCode::InvalidArgument,
                  "Normal map requires a sampled RGBA8 UNORM texture for a Lit material from this Context")};
}

auto validateOcclusionTexture(const MeshDraw& draw, const resources::MemoryPool& pool) -> Result<void>
{
    static constexpr auto formats = std::array {vk::Format::eR8G8B8A8Unorm, vk::Format::eB8G8R8A8Unorm};
    if (!draw.material.occlusionTexture.isValid() ||
        (draw.material.model == MaterialModel::Lit && hasAttribute(draw.mesh, VertexAttributeSemantic::TexCoord0) &&
         isCompatibleTexture(draw.material.occlusionTexture, pool, formats)))
    {
        return {};
    }
    return std::unexpected {
        makeError(ErrorCode::InvalidArgument,
                  "Occlusion map requires UVs and a sampled RGBA8 UNORM texture for a Lit material from this "
                  "Context")};
}

auto isUploadedTexture(const Texture& texture) -> bool
{
    return !texture.isValid() || ::panda::detail::TextureResource::getResource(texture)->isUploaded();
}

auto validateMaterialTexturesUploaded(const Material& material) -> Result<void>
{
    const auto textures = std::array {&material.baseColorTexture,
                                      &material.metallicRoughnessTexture,
                                      &material.emissiveTexture,
                                      &material.normalTexture,
                                      &material.occlusionTexture};
    if (std::ranges::all_of(textures, [](const Texture* texture) {
            return isUploadedTexture(*texture);
        }))
    {
        return {};
    }
    return std::unexpected {makeError(ErrorCode::InvalidArgument, "Material texture has not been uploaded")};
}

using DrawValidation = Result<void> (*)(const MeshDraw&, const resources::MemoryPool&);

auto validateDraws(const RenderView& view, const resources::MemoryPool& pool, DrawValidation validate) -> Result<void>
{
    for (const auto& draw : view.meshes)
    {
        if (auto result = validate(draw, pool); !result)
        {
            return result;
        }
    }
    return {};
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
    static constexpr std::array<DrawValidation, 5> validations {validateMeshDraw,
                                                                validateSrgbMaps,
                                                                validateMetallicRoughnessTexture,
                                                                validateNormalTexture,
                                                                validateOcclusionTexture};
    for (const auto validate : validations)
    {
        if (auto result = validateDraws(view, pool, validate); !result)
        {
            return result;
        }
    }
    return {};
}

auto validateRenderViewTextures(const RenderView& view) -> Result<void>
{
    for (const auto& draw : view.meshes)
    {
        if (auto result = validateMaterialTexturesUploaded(draw.material); !result)
        {
            return result;
        }
    }
    return {};
}
}
