#pragma once

#include <array>
#include <cstdint>
#include <panda/rendering/RenderView.hpp>
#include <string_view>

namespace panda::core::vulkan::renderer
{
struct MaterialTextureSlot
{
    std::string_view shaderName;
    Texture Material::* texture;
};

inline constexpr auto materialTextureSlots = std::array {
    MaterialTextureSlot {.shaderName = "baseColorMap",         .texture = &Material::baseColorTexture        },
    MaterialTextureSlot {.shaderName = "metallicRoughnessMap", .texture = &Material::metallicRoughnessTexture},
    MaterialTextureSlot {.shaderName = "emissiveMap",          .texture = &Material::emissiveTexture         },
    MaterialTextureSlot {.shaderName = "normalMap",            .texture = &Material::normalTexture           },
    MaterialTextureSlot {.shaderName = "occlusionMap",         .texture = &Material::occlusionTexture        }
};
}
