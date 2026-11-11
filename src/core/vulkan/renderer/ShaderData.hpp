#pragma once

#include <cstddef>
#include <cstdint>
#include <glm/ext/matrix_float3x4.hpp>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float4.hpp>
#include <panda/Mesh.hpp>

#include "ShaderInterface.inc"

namespace panda::core::vulkan::renderer
{
// These literals pin the host structures to the shader ABI.
// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
struct alignas(16) FrameUniform
{
    glm::mat4 viewProjection;
    glm::mat4 lightViewProjection;
    glm::vec4 cameraPosition;
    glm::vec4 lightDirection;
    glm::vec4 lightColorIntensity;
    glm::vec4 ambient;
};

struct alignas(16) DrawConstants
{
    glm::mat4 modelMatrix;
    glm::mat3x4 normalColumns;
    glm::vec4 tint;
};

struct alignas(16) MaterialRecord
{
    float metallic {};
    float roughness {};
    std::uint32_t isLit {};
    std::uint32_t renderFace {};
    float alphaCutoff {};
    std::uint32_t hasBaseColorTexture {};
    std::uint32_t hasMetallicRoughnessTexture {};
    std::uint32_t hasEmissiveTexture {};
    glm::vec4 emissiveAndNormalScale {};
    std::uint32_t hasOcclusionTexture {};
    float occlusionStrength {1.0F};
    glm::vec2 padding {};
};

static_assert(sizeof(FrameUniform) == 192);
static_assert(sizeof(glm::vec4) == 16);
static_assert(offsetof(FrameUniform, lightViewProjection) == 64);
static_assert(offsetof(FrameUniform, cameraPosition) == 128);
static_assert(offsetof(FrameUniform, lightDirection) == 144);
static_assert(offsetof(FrameUniform, lightColorIntensity) == 160);
static_assert(offsetof(FrameUniform, ambient) == 176);
static_assert(sizeof(DrawConstants) == 128);
static_assert(offsetof(DrawConstants, normalColumns) == 64);
static_assert(offsetof(DrawConstants, tint) == 112);
static_assert(offsetof(MaterialRecord, metallic) == 0);
static_assert(offsetof(MaterialRecord, roughness) == 4);
static_assert(offsetof(MaterialRecord, isLit) == 8);
static_assert(offsetof(MaterialRecord, renderFace) == 12);
static_assert(sizeof(MaterialRecord) == 64);
static_assert(offsetof(MaterialRecord, alphaCutoff) == 16);
static_assert(offsetof(MaterialRecord, hasBaseColorTexture) == 20);
static_assert(offsetof(MaterialRecord, hasMetallicRoughnessTexture) == 24);
static_assert(offsetof(MaterialRecord, hasEmissiveTexture) == 28);
static_assert(offsetof(MaterialRecord, emissiveAndNormalScale) == 32);
static_assert(offsetof(MaterialRecord, hasOcclusionTexture) == 48);
static_assert(offsetof(MaterialRecord, occlusionStrength) == 52);
static_assert(offsetof(MaterialRecord, padding) == 56);
static_assert(static_cast<std::uint32_t>(VertexAttributeSemantic::Position) ==
              PANDA_VERTEX_ATTRIBUTE_POSITION_LOCATION);
static_assert(static_cast<std::uint32_t>(VertexAttributeSemantic::Normal) == PANDA_VERTEX_ATTRIBUTE_NORMAL_LOCATION);
static_assert(static_cast<std::uint32_t>(VertexAttributeSemantic::TexCoord0) ==
              PANDA_VERTEX_ATTRIBUTE_TEX_COORD_0_LOCATION);
static_assert(static_cast<std::uint32_t>(VertexAttributeSemantic::Tangent) == PANDA_VERTEX_ATTRIBUTE_TANGENT_LOCATION);
static_assert(static_cast<std::uint32_t>(VertexAttributeSemantic::Color) == PANDA_VERTEX_ATTRIBUTE_COLOR_LOCATION);
static_assert(PANDA_MESH_FRAGMENT_COLOR_LOCATION == 0);
// NOLINTEND(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
}
