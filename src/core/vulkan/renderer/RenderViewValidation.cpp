// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include "RenderViewValidation.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <expected>
#include <glm/ext/matrix_float3x3.hpp>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_access.hpp>
#include <glm/matrix.hpp>
#include <optional>
#include <panda/Error.hpp>
#include <panda/Mesh.hpp>
#include <panda/RenderView.hpp>
#include <panda/Texture.hpp>
#include <variant>

#include "Math.hpp"
#include "Overloaded.hpp"
#include "resources/MeshResource.hpp"
#include "resources/TextureResource.hpp"
#include "resources/memory/MemoryPool.hpp"

namespace panda::core::vulkan::renderer
{
namespace
{
using panda::detail::isFinite;
constexpr auto minimumLightDirectionLengthSquared = 1.0e-12F;

auto isInvertible(const glm::mat4& matrix) -> bool
{
    const auto determinant = glm::determinant(matrix);
    return determinant != 0.0F && isFinite(glm::inverse(matrix));
}

auto isAffine(const glm::mat4& model) -> bool
{
    static constexpr auto tolerance = 1.0e-6F;
    const auto affineRow = glm::row(model, 3);
    return std::abs(affineRow.x) <= tolerance && std::abs(affineRow.y) <= tolerance &&
           std::abs(affineRow.z) <= tolerance && std::abs(affineRow.w - 1.0F) <= tolerance;
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
            makeError(ErrorCode::InvalidArgument, "Mesh draw requires a compatible vertex layout from this Engine")};
    }
    if (!hasAttribute(draw.mesh, VertexAttributeSemantic::Position) ||
        (draw.material.model == MaterialModel::Lit && !hasAttribute(draw.mesh, VertexAttributeSemantic::Normal)))
    {
        return std::unexpected {
            makeError(ErrorCode::InvalidArgument, "Mesh draw requires a compatible vertex layout from this Engine")};
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
                                      "from this Engine")};
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
                  "this Engine")};
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
                  "Normal map requires a sampled RGBA8 UNORM texture for a Lit material from this Engine")};
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
                  "Engine")};
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

auto validateLightColorAndIntensity(glm::vec3 color, float intensity) -> Result<void>
{
    if (!isFinite(color) || !std::isfinite(intensity))
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Light contains non-finite data")};
    }
    if (intensity < 0.0F || color.r < 0.0F || color.g < 0.0F || color.b < 0.0F)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Light color and intensity must be nonnegative")};
    }
    return {};
}

auto validateLightDirection(glm::vec3 direction) -> Result<void>
{
    if (!isFinite(direction))
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Light direction contains non-finite data")};
    }
    const auto directionLengthSquared = glm::dot(direction, direction);
    if (!std::isfinite(directionLengthSquared) || directionLengthSquared <= minimumLightDirectionLengthSquared)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Light direction must be nonzero")};
    }
    return {};
}

auto validateLocalLightRange(float range) -> Result<void>
{
    if (!std::isfinite(range) || range <= 0.0F)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Local light range must be finite and positive")};
    }
    return {};
}

auto validateSpotCone(float innerConeAngleRadians, float outerConeAngleRadians) -> Result<void>
{
    if (!std::isfinite(innerConeAngleRadians) || !std::isfinite(outerConeAngleRadians) ||
        innerConeAngleRadians < 0.0F || outerConeAngleRadians <= innerConeAngleRadians ||
        outerConeAngleRadians > glm::half_pi<float>() ||
        std::cos(innerConeAngleRadians) <= std::cos(outerConeAngleRadians))
    {
        return std::unexpected {
            makeError(ErrorCode::InvalidArgument, "Spot cone angles must satisfy 0 <= inner < outer <= pi/2")};
    }
    return {};
}

auto validateLightPosition(glm::vec3 position) -> Result<void>
{
    if (!isFinite(position))
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Local light position is non-finite")};
    }
    return {};
}

auto validateShadowSettings(const std::optional<ShadowSettings>& shadow) -> Result<void>
{
    if (!shadow)
    {
        return {};
    }
    constexpr auto supportedResolutions = std::array {256U, 512U, 1024U, 2048U};
    const auto validResolution = !shadow->resolution || std::ranges::find(supportedResolutions, *shadow->resolution) !=
                                                            supportedResolutions.end();
    if (!validResolution || !std::isfinite(shadow->depthBias) || shadow->depthBias < 0.0F ||
        !std::isfinite(shadow->normalOffset) || shadow->normalOffset < 0.0F)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Shadow settings are invalid")};
    }
    return {};
}

auto validateDirectionalLight(const DirectionalLight& light) -> Result<void>
{
    return validateLightColorAndIntensity(light.color, light.intensity)
        .and_then([&] {
            return validateLightDirection(light.direction);
        })
        .and_then([&] {
            return validateShadowSettings(light.shadow);
        })
        .and_then([&] {
            if (light.shadow && (!std::isfinite(light.shadowCoverageMeters) || light.shadowCoverageMeters <= 0.0F))
            {
                return Result<void> {std::unexpected {
                    makeError(ErrorCode::InvalidArgument, "Directional shadow coverage must be finite and positive")}};
            }
            return Result<void> {};
        });
}

auto validatePointLight(const PointLight& light) -> Result<void>
{
    return validateLightColorAndIntensity(light.color, light.intensity)
        .and_then([&] {
            return validateLightPosition(light.position);
        })
        .and_then([&] {
            return validateLocalLightRange(light.range);
        })
        .and_then([&] {
            return validateShadowSettings(light.shadow);
        });
}

auto validateSpotLight(const SpotLight& light) -> Result<void>
{
    return validateLightColorAndIntensity(light.color, light.intensity)
        .and_then([&] {
            return validateLightPosition(light.position);
        })
        .and_then([&] {
            return validateLightDirection(light.direction);
        })
        .and_then([&] {
            return validateLocalLightRange(light.range);
        })
        .and_then([&] {
            return validateSpotCone(light.innerConeAngleRadians, light.outerConeAngleRadians);
        })
        .and_then([&] {
            return validateShadowSettings(light.shadow);
        });
}

auto validateLight(const Light& light) -> Result<void>
{
    return std::visit(::panda::detail::Overloaded {[](const DirectionalLight& value) {
                                                       return validateDirectionalLight(value);
                                                   },
                                                   [](const PointLight& value) {
                                                       return validatePointLight(value);
                                                   },
                                                   [](const SpotLight& value) {
                                                       return validateSpotLight(value);
                                                   }},
                      light);
}

}

auto validateRenderViewData(const RenderView& view) -> Result<void>
{
    if (!isFinite(view.camera.viewProjection) || !isInvertible(view.camera.viewProjection) ||
        !isFinite(view.camera.position))
    {
        return std::unexpected {
            makeError(ErrorCode::InvalidArgument, "Render view contains non-finite or singular data")};
    }
    for (const auto& light : view.lights)
    {
        if (auto result = validateLight(light); !result)
        {
            return result;
        }
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

auto validateRenderViewLightCount(const RenderView& view, std::uint32_t maxLights) -> Result<void>
{
    if (view.lights.size() > maxLights)
    {
        return std::unexpected {
            makeError(ErrorCode::ResourceExhausted, "Render view light count exceeds the Engine maxLights budget")};
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

auto validateRenderView(const RenderView& view, std::uint32_t maxLights, const resources::MemoryPool& pool)
    -> Result<void>
{
    if (auto result = validateRenderViewLightCount(view, maxLights); !result)
    {
        return result;
    }
    if (auto result = validateRenderViewData(view); !result)
    {
        return result;
    }
    return validateRenderViewMeshes(view, pool);
}
}
