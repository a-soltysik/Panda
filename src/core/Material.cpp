#include <cstddef>
#include <panda/rendering/RenderView.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>  // IWYU pragma: keep
#undef GLM_ENABLE_EXPERIMENTAL
#include <glm/vector_relational.hpp>

#include "Hash.hpp"

namespace panda
{
auto Material::isCompatibleForInstancing(const Material& other) const noexcept -> bool
{
    return model == other.model && glm::all(glm::equal(baseColor, other.baseColor)) &&
           baseColorTexture == other.baseColorTexture && metallicRoughnessTexture == other.metallicRoughnessTexture &&
           emissiveTexture == other.emissiveTexture && normalTexture == other.normalTexture &&
           normalScale == other.normalScale && glm::all(glm::equal(emissive, other.emissive)) &&
           metallic == other.metallic && roughness == other.roughness && renderFace == other.renderFace &&
           alphaMode == other.alphaMode && alphaCutoff == other.alphaCutoff &&
           occlusionTexture == other.occlusionTexture && occlusionStrength == other.occlusionStrength;
}

auto Material::hashForInstancing() const noexcept -> std::size_t
{
    auto seed = std::size_t {};
    detail::combineHash(seed, model);
    detail::combineHash(seed, baseColor);
    detail::combineHash(seed, baseColorTexture);
    detail::combineHash(seed, metallicRoughnessTexture);
    detail::combineHash(seed, emissiveTexture);
    detail::combineHash(seed, normalTexture);
    detail::combineHash(seed, normalScale);
    detail::combineHash(seed, emissive);
    detail::combineHash(seed, metallic);
    detail::combineHash(seed, roughness);
    detail::combineHash(seed, renderFace);
    detail::combineHash(seed, alphaMode);
    detail::combineHash(seed, alphaCutoff);
    detail::combineHash(seed, occlusionTexture);
    detail::combineHash(seed, occlusionStrength);
    return seed;
}
}
