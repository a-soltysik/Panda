#pragma once

#include <array>
#include <cstdint>
#include <panda/RenderView.hpp>

#include "renderer/ShaderInterface.inc"

namespace panda::core::vulkan::renderer
{
struct MaterialTextureSlot
{
    std::uint32_t binding;
    Texture Material::* texture;
};

inline constexpr auto materialTextureSlots = std::array {
    MaterialTextureSlot {.binding = PANDA_TEXTURE_BASE_COLOR_BINDING,         .texture = &Material::baseColorTexture},
    MaterialTextureSlot {.binding = PANDA_TEXTURE_METALLIC_ROUGHNESS_BINDING,
                         .texture = &Material::metallicRoughnessTexture                                             },
    MaterialTextureSlot {.binding = PANDA_TEXTURE_EMISSIVE_BINDING,           .texture = &Material::emissiveTexture },
    MaterialTextureSlot {.binding = PANDA_TEXTURE_NORMAL_BINDING,             .texture = &Material::normalTexture   }
};
}
