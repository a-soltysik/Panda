// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include "renderer/MeshPipelineCache.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <glm/ext/matrix_float3x3.hpp>
#include <glm/matrix.hpp>
#include <optional>
#include <panda/Error.hpp>
#include <panda/Logger.hpp>
#include <panda/Mesh.hpp>
#include <panda/RenderView.hpp>
#include <span>
#include <utility>
#include <vector>

#include "BuiltinShaders.hpp"
#include "VulkanError.hpp"
#include "renderer/ShaderData.hpp"
#include "resources/MeshResource.hpp"

namespace panda::core::vulkan::renderer
{
using detail::checkedCreation;

namespace
{
constexpr auto meshVertexBufferBinding = std::uint32_t {0};
constexpr auto dynamicStates = std::array {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
constexpr auto inputAssembly =
    vk::PipelineInputAssemblyStateCreateInfo {.topology = vk::PrimitiveTopology::eTriangleList};
constexpr auto viewport = vk::PipelineViewportStateCreateInfo {.viewportCount = 1, .scissorCount = 1};
constexpr auto multisample =
    vk::PipelineMultisampleStateCreateInfo {.rasterizationSamples = vk::SampleCountFlagBits::e1};
constexpr auto depthStencil = vk::PipelineDepthStencilStateCreateInfo {
    .depthTestEnable = vk::True, .depthWriteEnable = vk::True, .depthCompareOp = vk::CompareOp::eLess};
constexpr auto blendDepthStencil = vk::PipelineDepthStencilStateCreateInfo {
    .depthTestEnable = vk::True, .depthWriteEnable = vk::False, .depthCompareOp = vk::CompareOp::eLess};
constexpr auto opaqueBlendAttachment = vk::PipelineColorBlendAttachmentState {
    .blendEnable = vk::False,
    .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB |
                      vk::ColorComponentFlagBits::eA};
constexpr auto alphaBlendAttachment = vk::PipelineColorBlendAttachmentState {
    .blendEnable = vk::True,
    .srcColorBlendFactor = vk::BlendFactor::eSrcAlpha,
    .dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
    .colorBlendOp = vk::BlendOp::eAdd,
    .srcAlphaBlendFactor = vk::BlendFactor::eOne,
    .dstAlphaBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
    .alphaBlendOp = vk::BlendOp::eAdd,
    .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB |
                      vk::ColorComponentFlagBits::eA};
constexpr auto opaqueBlend =
    vk::PipelineColorBlendStateCreateInfo {.attachmentCount = 1, .pAttachments = &opaqueBlendAttachment};
constexpr auto alphaBlend =
    vk::PipelineColorBlendStateCreateInfo {.attachmentCount = 1, .pAttachments = &alphaBlendAttachment};
constexpr auto noColorBlend = vk::PipelineColorBlendStateCreateInfo {};
constexpr auto dynamic = vk::PipelineDynamicStateCreateInfo {
    .dynamicStateCount = static_cast<std::uint32_t>(dynamicStates.size()), .pDynamicStates = dynamicStates.data()};

auto findVertexAttribute(const Mesh& mesh, VertexAttributeSemantic semantic) -> const VertexAttribute*
{
    const auto& attributes = mesh.getVertexAttributes();
    const auto attribute = std::ranges::find(attributes, semantic, &VertexAttribute::semantic);
    return attribute == attributes.end() ? nullptr : &*attribute;
}

auto hasVertexAttribute(const Mesh& mesh, VertexAttributeSemantic semantic) -> bool
{
    return mesh.isValid() && ::panda::detail::MeshResource::getResource(mesh)->getLayout().hasVertexAttribute(semantic);
}

auto usesNormalMap(const MeshDraw& draw, bool hasUv, bool hasTangent) -> bool
{
    return draw.material.model == MaterialModel::Lit && draw.material.normalTexture.isValid() && hasUv && hasTangent;
}

auto usesTextureCoordinates(const Material& material, bool hasNormalMap) -> bool
{
    return material.baseColorTexture.isValid() || material.metallicRoughnessTexture.isValid() ||
           material.emissiveTexture.isValid() || material.occlusionTexture.isValid() || hasNormalMap;
}

auto vertexAttributeOffset(const VertexAttribute* attribute) -> std::uint32_t
{
    return attribute != nullptr ? attribute->offset : 0U;
}

auto makeVertexAttributeDescription(std::uint32_t location, vk::Format format, std::uint32_t offset)
    -> vk::VertexInputAttributeDescription
{
    return {.location = location, .binding = meshVertexBufferBinding, .format = format, .offset = offset};
}

auto isSrgb(vk::Format format) -> bool
{
    return format == vk::Format::eB8G8R8A8Srgb || format == vk::Format::eR8G8B8A8Srgb;
}

auto makeRasterizationState(RenderFace renderFace, bool mirrored) -> vk::PipelineRasterizationStateCreateInfo
{
    auto cullMode = vk::CullModeFlagBits::eNone;
    switch (renderFace)
    {
    case RenderFace::Front:
        cullMode = vk::CullModeFlagBits::eBack;
        break;
    case RenderFace::Back:
        cullMode = vk::CullModeFlagBits::eFront;
        break;
    case RenderFace::Both:
        break;
    }
    return {.polygonMode = vk::PolygonMode::eFill,
            .cullMode = cullMode,
            .frontFace = mirrored ? vk::FrontFace::eClockwise : vk::FrontFace::eCounterClockwise,
            .lineWidth = 1.0F};
}

auto makeShaderStages(vk::ShaderModule vertex, vk::ShaderModule fragment, const vk::SpecializationInfo& specialization)
    -> std::array<vk::PipelineShaderStageCreateInfo, 2>
{
    return {
        vk::PipelineShaderStageCreateInfo {.stage = vk::ShaderStageFlagBits::eVertex,
                                           .module = vertex,
                                           .pName = "main"},
        vk::PipelineShaderStageCreateInfo {.stage = vk::ShaderStageFlagBits::eFragment,
                                           .module = fragment,
                                           .pName = "main",
                                           .pSpecializationInfo = &specialization}
    };
}

auto makeShadowShaderStages(vk::ShaderModule vertex,
                            const std::optional<vk::raii::ShaderModule>& fragment,
                            const vk::SpecializationInfo& specialization)
    -> std::pair<std::array<vk::PipelineShaderStageCreateInfo, 2>, std::size_t>
{
    const auto vertexStage = vk::PipelineShaderStageCreateInfo {.stage = vk::ShaderStageFlagBits::eVertex,
                                                                .module = vertex,
                                                                .pName = "main"};
    if (!fragment)
    {
        return {
            std::array {vertexStage, vertexStage},
            1U
        };
    }
    const auto fragmentStage = vk::PipelineShaderStageCreateInfo {.stage = vk::ShaderStageFlagBits::eFragment,
                                                                  .module = **fragment,
                                                                  .pName = "main",
                                                                  .pSpecializationInfo = &specialization};
    return {
        std::array {vertexStage, fragmentStage},
        2U
    };
}

struct GraphicsPipelineState
{
    std::span<const vk::PipelineShaderStageCreateInfo> stages;
    const vk::PipelineRenderingCreateInfo& rendering;
    const vk::PipelineMultisampleStateCreateInfo& multisampling;
    vk::PipelineRasterizationStateCreateInfo rasterization;
    const vk::PipelineDepthStencilStateCreateInfo& depthStencil;
    const vk::PipelineColorBlendStateCreateInfo& colorBlend;
    const vk::PipelineVertexInputStateCreateInfo& vertexInput;
    vk::PipelineLayout layout;
};

struct ShadowVertexInput
{
    vk::VertexInputBindingDescription binding;
    std::vector<vk::VertexInputAttributeDescription> attributes;
};

auto makeShadowVertexInput(const MeshPipelineCache::ShadowPipelineKey& key) -> ShadowVertexInput
{
    auto result = ShadowVertexInput {
        .binding = {.binding = meshVertexBufferBinding,
                    .stride = key.vertexStride,
                    .inputRate = vk::VertexInputRate::eVertex},
        .attributes = {{.location = PANDA_VERTEX_ATTRIBUTE_POSITION_LOCATION,
                        .binding = meshVertexBufferBinding,
                        .format = vk::Format::eR32G32B32Sfloat,
                        .offset = key.positionOffset}}
    };
    if (key.hasBaseColorTexture)
    {
        result.attributes.push_back({.location = PANDA_VERTEX_ATTRIBUTE_TEX_COORD_0_LOCATION,
                                     .binding = meshVertexBufferBinding,
                                     .format = vk::Format::eR32G32Sfloat,
                                     .offset = key.texCoordOffset});
    }
    if (key.hasColor)
    {
        result.attributes.push_back({.location = PANDA_VERTEX_ATTRIBUTE_COLOR_LOCATION,
                                     .binding = meshVertexBufferBinding,
                                     .format = vk::Format::eR32G32B32A32Sfloat,
                                     .offset = key.colorOffset});
    }
    return result;
}

auto meshVertexShaderWords(const MeshPipelineCache::MeshPipelineKey& key) -> std::span<const std::uint32_t>
{
    if (key.hasNormalMap)
    {
        return key.hasColor ? std::span<const std::uint32_t> {shaders::meshLitColorNormalVertex}
                            : std::span<const std::uint32_t> {shaders::meshLitPlainNormalVertex};
    }
    static constexpr auto variants = std::array {
        std::span<const std::uint32_t> {shaders::meshUnlitPlainVertex},
        std::span<const std::uint32_t> {shaders::meshUnlitPlainTextureVertex},
        std::span<const std::uint32_t> {shaders::meshUnlitColorVertex},
        std::span<const std::uint32_t> {shaders::meshUnlitColorTextureVertex},
        std::span<const std::uint32_t> {shaders::meshLitPlainVertex},
        std::span<const std::uint32_t> {shaders::meshLitPlainTextureVertex},
        std::span<const std::uint32_t> {shaders::meshLitColorVertex},
        std::span<const std::uint32_t> {shaders::meshLitColorTextureVertex},
    };
    const auto variantIndex = (static_cast<std::size_t>(key.lit) << 2U) |
                              (static_cast<std::size_t>(key.hasColor) << 1U) |
                              static_cast<std::size_t>(key.hasTextureCoordinates);
    return variants.at(variantIndex);
}

auto shadowVertexShaderWords(bool hasColor, bool hasTexture) -> std::span<const std::uint32_t>
{
    if (hasColor)
    {
        return hasTexture ? std::span<const std::uint32_t> {shaders::shadowVertexAlphaTextureVertex}
                          : std::span<const std::uint32_t> {shaders::shadowVertexAlphaVertex};
    }
    return hasTexture ? std::span<const std::uint32_t> {shaders::shadowTextureVertex}
                      : std::span<const std::uint32_t> {shaders::shadowVertex};
}

auto makeVertexInputState(const vk::VertexInputBindingDescription& binding,
                          std::span<const vk::VertexInputAttributeDescription> attributes)
    -> vk::PipelineVertexInputStateCreateInfo
{
    return {.vertexBindingDescriptionCount = 1,
            .pVertexBindingDescriptions = &binding,
            .vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size()),
            .pVertexAttributeDescriptions = attributes.data()};
}

auto makeGraphicsPipelineCreateInfo(const GraphicsPipelineState& state) -> vk::GraphicsPipelineCreateInfo
{
    return {.pNext = &state.rendering,
            .stageCount = static_cast<std::uint32_t>(state.stages.size()),
            .pStages = state.stages.data(),
            .pVertexInputState = &state.vertexInput,
            .pInputAssemblyState = &inputAssembly,
            .pViewportState = &viewport,
            .pRasterizationState = &state.rasterization,
            .pMultisampleState = &state.multisampling,
            .pDepthStencilState = &state.depthStencil,
            .pColorBlendState = &state.colorBlend,
            .pDynamicState = &dynamic,
            .layout = state.layout,
            .renderPass = {},
            .basePipelineIndex = -1};
}

struct FragmentSpecializationData
{
    std::uint32_t outputIsSrgb;
    std::uint32_t alphaMode;
};

auto makeFragmentSpecialization(const FragmentSpecializationData& data) -> vk::SpecializationInfo
{
    static constexpr auto entries = std::array {
        vk::SpecializationMapEntry {.constantID = PANDA_MESH_OUTPUT_SRGB_SPECIALIZATION_ID,
                                    .offset = offsetof(FragmentSpecializationData, outputIsSrgb),
                                    .size = sizeof(std::uint32_t)},
        vk::SpecializationMapEntry {.constantID = PANDA_MESH_ALPHA_MODE_SPECIALIZATION_ID,
                                    .offset = offsetof(FragmentSpecializationData, alphaMode),
                                    .size = sizeof(std::uint32_t)}
    };
    return {.mapEntryCount = static_cast<std::uint32_t>(entries.size()),
            .pMapEntries = entries.data(),
            .dataSize = sizeof(data),
            .pData = &data};
}

auto makeOutputSrgbSpecialization(const std::uint32_t& outputIsSrgb) -> vk::SpecializationInfo
{
    static constexpr auto entry = vk::SpecializationMapEntry {.constantID = PANDA_MESH_OUTPUT_SRGB_SPECIALIZATION_ID,
                                                              .offset = 0,
                                                              .size = sizeof(std::uint32_t)};
    return {.mapEntryCount = 1, .pMapEntries = &entry, .dataSize = sizeof(outputIsSrgb), .pData = &outputIsSrgb};
}

auto makeMeshFragmentSpecialization(AlphaMode alphaMode, const FragmentSpecializationData& data)
    -> vk::SpecializationInfo
{
    return alphaMode == AlphaMode::Opaque ? makeOutputSrgbSpecialization(data.outputIsSrgb)
                                          : makeFragmentSpecialization(data);
}

auto makeAlphaModeSpecialization(const std::uint32_t& alphaMode) -> vk::SpecializationInfo
{
    static constexpr auto entry = vk::SpecializationMapEntry {.constantID = PANDA_SHADOW_ALPHA_MODE_SPECIALIZATION_ID,
                                                              .offset = 0,
                                                              .size = sizeof(std::uint32_t)};
    return {.mapEntryCount = 1, .pMapEntries = &entry, .dataSize = sizeof(alphaMode), .pData = &alphaMode};
}

auto createShaderModule(const vk::raii::Device& device, std::span<const std::uint32_t> words)
    -> std::expected<vk::raii::ShaderModule, Error>
{
    const auto info = vk::ShaderModuleCreateInfo {.codeSize = words.size_bytes(), .pCode = words.data()};
    const auto module = (*device).createShaderModule(info, nullptr, *device.getDispatcher());
    if (module.result != vk::Result::eSuccess)
    {
        return std::unexpected {makeVulkanError(module.result)};
    }
    return std::expected<vk::raii::ShaderModule, Error> {std::in_place,
                                                         device,
                                                         static_cast<vk::ShaderModule::CType>(module.value)};
}

struct ShadowShaderModules
{
    vk::raii::ShaderModule vertex {nullptr};
    std::optional<vk::raii::ShaderModule> fragment;
};

auto createShadowShaderModules(const vk::raii::Device& device,
                               std::span<const std::uint32_t> vertexWords,
                               bool hasAlphaMask,
                               bool hasBaseColorTexture) -> std::expected<ShadowShaderModules, Error>
{
    auto vertex = createShaderModule(device, vertexWords);
    if (!vertex)
    {
        return std::unexpected {vertex.error()};
    }
    auto fragment = std::optional<vk::raii::ShaderModule> {};
    if (hasAlphaMask)
    {
        const auto fragmentWords = hasBaseColorTexture ? std::span<const std::uint32_t> {shaders::shadowTextureFragment}
                                                       : std::span<const std::uint32_t> {shaders::shadowFragment};
        auto created = createShaderModule(device, fragmentWords);
        if (!created)
        {
            return std::unexpected {created.error()};
        }
        fragment.emplace(std::move(*created));
    }
    return ShadowShaderModules {.vertex = std::move(*vertex), .fragment = std::move(fragment)};
}
}

MeshPipelineCache::MeshPipelineCache(const vk::raii::Device& device, vk::PipelineLayout pipelineLayout)
    : _device {device},
      _pipelineLayout {pipelineLayout}
{
}

auto MeshPipelineCache::makeMeshPipelineKey(const MeshDraw& draw) -> MeshPipelineKey
{
    const auto lit = draw.material.model == MaterialModel::Lit;
    const auto* position = findVertexAttribute(draw.mesh, VertexAttributeSemantic::Position);
    const auto* normal = lit ? findVertexAttribute(draw.mesh, VertexAttributeSemantic::Normal) : nullptr;
    const auto* color = findVertexAttribute(draw.mesh, VertexAttributeSemantic::Color);
    const auto hasUv = hasVertexAttribute(draw.mesh, VertexAttributeSemantic::TexCoord0);
    const auto hasTangentAttribute = hasVertexAttribute(draw.mesh, VertexAttributeSemantic::Tangent);
    const auto hasNormalMap = usesNormalMap(draw, hasUv, hasTangentAttribute);
    const auto hasTextureCoordinates = usesTextureCoordinates(draw.material, hasNormalMap);
    const auto* texCoord =
        hasTextureCoordinates ? findVertexAttribute(draw.mesh, VertexAttributeSemantic::TexCoord0) : nullptr;
    const auto* tangent = hasNormalMap ? findVertexAttribute(draw.mesh, VertexAttributeSemantic::Tangent) : nullptr;
    return {.lit = lit,
            .hasColor = color != nullptr,
            .hasTextureCoordinates = hasTextureCoordinates,
            .hasNormalMap = hasNormalMap,
            .vertexStride = draw.mesh.getVertexStride(),
            .positionOffset = vertexAttributeOffset(position),
            .normalOffset = vertexAttributeOffset(normal),
            .texCoordOffset = vertexAttributeOffset(texCoord),
            .tangentOffset = vertexAttributeOffset(tangent),
            .colorOffset = vertexAttributeOffset(color),
            .renderFace = draw.material.renderFace,
            .alphaMode = draw.material.alphaMode,
            .mirrored = glm::determinant(glm::mat3 {draw.modelMatrix}) < 0.0F};
}

auto MeshPipelineCache::makeShadowPipelineKey(const MeshDraw& draw) -> ShadowPipelineKey
{
    const auto* position = findVertexAttribute(draw.mesh, VertexAttributeSemantic::Position);
    const auto* texCoord = findVertexAttribute(draw.mesh, VertexAttributeSemantic::TexCoord0);
    const auto* color = findVertexAttribute(draw.mesh, VertexAttributeSemantic::Color);
    const auto hasColor = draw.material.alphaMode == AlphaMode::Mask && color != nullptr;
    const auto hasBaseColorTexture =
        draw.material.alphaMode == AlphaMode::Mask && draw.material.baseColorTexture.isValid();
    return {.vertexStride = draw.mesh.getVertexStride(),
            .positionOffset = position != nullptr ? position->offset : 0U,
            .texCoordOffset = hasBaseColorTexture && texCoord != nullptr ? texCoord->offset : 0U,
            .colorOffset = hasColor ? color->offset : 0U,
            .hasColor = hasColor,
            .hasBaseColorTexture = hasBaseColorTexture,
            .renderFace = draw.material.renderFace,
            .alphaMode = draw.material.alphaMode,
            .mirrored = glm::determinant(glm::mat3 {draw.modelMatrix}) < 0.0F};
}

void MeshPipelineCache::setOutputCompatibility(OutputCompatibility compatibility)
{
    if (compatibility.colorFormat != _outputCompatibility.colorFormat ||
        compatibility.depthFormat != _outputCompatibility.depthFormat ||
        compatibility.sampleCount != _outputCompatibility.sampleCount)
    {
        _pipelines.clear();
    }
    if (compatibility.depthFormat != _outputCompatibility.depthFormat)
    {
        _shadowPipelines.clear();
    }
    _outputCompatibility = compatibility;
}

auto MeshPipelineCache::prepare(const RenderView& view) -> std::expected<void, Error>
{
    for (const auto& draw : view.meshes)
    {
        if (const auto prepared = prepareDraw(draw); !prepared)
        {
            return prepared;
        }
    }
    return {};
}

auto MeshPipelineCache::prepareDraw(const MeshDraw& draw) -> std::expected<void, Error>
{
    if (draw.material.model == MaterialModel::Lit && draw.material.normalTexture.isValid())
    {
        const auto hasUv = hasVertexAttribute(draw.mesh, VertexAttributeSemantic::TexCoord0);
        const auto hasTangent = hasVertexAttribute(draw.mesh, VertexAttributeSemantic::Tangent);
        if (!hasUv && !_warnedMissingNormalMapUv)
        {
            panda::log::warning("Ignoring normal map because the mesh has no TexCoord0 attribute");
            _warnedMissingNormalMapUv = true;
        }
        else if (hasUv && !hasTangent && !_warnedMissingNormalMapTangent)
        {
            panda::log::warning("Ignoring normal map because the mesh has no Tangent attribute");
            _warnedMissingNormalMapTangent = true;
        }
    }
    if (const auto prepared = prepare(makeMeshPipelineKey(draw)); !prepared)
    {
        return prepared;
    }
    return draw.material.alphaMode == AlphaMode::Blend ? std::expected<void, Error> {}
                                                       : prepare(makeShadowPipelineKey(draw));
}

auto MeshPipelineCache::prepare(const MeshPipelineKey& key) -> std::expected<void, Error>
{
    if (_pipelines.contains(key))
    {
        return {};
    }
    auto pipeline = createPipeline(key);
    if (!pipeline)
    {
        return std::unexpected {pipeline.error()};
    }
    _pipelines.emplace(key, std::move(*pipeline));
    return {};
}

auto MeshPipelineCache::prepare(const ShadowPipelineKey& key) -> std::expected<void, Error>
{
    if (_shadowPipelines.contains(key))
    {
        return {};
    }
    auto pipeline = createShadowPipeline(key);
    if (!pipeline)
    {
        return std::unexpected {pipeline.error()};
    }
    _shadowPipelines.emplace(key, std::move(*pipeline));
    return {};
}

auto MeshPipelineCache::getPipeline(const MeshDraw& draw) const -> vk::Pipeline
{
    return *_pipelines.at(makeMeshPipelineKey(draw));
}

auto MeshPipelineCache::getShadowPipeline(const MeshDraw& draw) const -> vk::Pipeline
{
    return *_shadowPipelines.at(makeShadowPipelineKey(draw));
}

auto MeshPipelineCache::makeVertexAttributeDescriptions(const MeshPipelineKey& key)
    -> std::vector<vk::VertexInputAttributeDescription>
{
    auto attributes = std::vector {makeVertexAttributeDescription(PANDA_VERTEX_ATTRIBUTE_POSITION_LOCATION,
                                                                  vk::Format::eR32G32B32Sfloat,
                                                                  key.positionOffset)};
    if (key.lit)
    {
        attributes.push_back(makeVertexAttributeDescription(PANDA_VERTEX_ATTRIBUTE_NORMAL_LOCATION,
                                                            vk::Format::eR32G32B32Sfloat,
                                                            key.normalOffset));
    }
    if (key.hasNormalMap)
    {
        attributes.push_back(makeVertexAttributeDescription(PANDA_VERTEX_ATTRIBUTE_TANGENT_LOCATION,
                                                            vk::Format::eR32G32B32A32Sfloat,
                                                            key.tangentOffset));
    }
    if (key.hasTextureCoordinates)
    {
        attributes.push_back(makeVertexAttributeDescription(PANDA_VERTEX_ATTRIBUTE_TEX_COORD_0_LOCATION,
                                                            vk::Format::eR32G32Sfloat,
                                                            key.texCoordOffset));
    }
    if (key.hasColor)
    {
        attributes.push_back(makeVertexAttributeDescription(PANDA_VERTEX_ATTRIBUTE_COLOR_LOCATION,
                                                            vk::Format::eR32G32B32A32Sfloat,
                                                            key.colorOffset));
    }
    return attributes;
}

auto MeshPipelineCache::createPipeline(const MeshPipelineKey& key) const -> std::expected<vk::raii::Pipeline, Error>
{
    const auto vertexWords = meshVertexShaderWords(key);
    auto vertex = createShaderModule(_device, vertexWords);
    if (!vertex)
    {
        return std::unexpected {vertex.error()};
    }
    const auto fragmentWords = key.hasNormalMap ? std::span<const std::uint32_t> {shaders::meshNormalFragment}
                                                : std::span<const std::uint32_t> {shaders::meshFragment};
    auto fragment = createShaderModule(_device, fragmentWords);
    if (!fragment)
    {
        return std::unexpected {fragment.error()};
    }
    return createMeshPipeline(key, **vertex, **fragment);
}

auto MeshPipelineCache::createMeshPipeline(const MeshPipelineKey& key,
                                           vk::ShaderModule vertex,
                                           vk::ShaderModule fragment) const -> std::expected<vk::raii::Pipeline, Error>
{
    const auto binding = vk::VertexInputBindingDescription {.binding = meshVertexBufferBinding,
                                                            .stride = key.vertexStride,
                                                            .inputRate = vk::VertexInputRate::eVertex};
    const auto attributes = makeVertexAttributeDescriptions(key);
    const auto vertexInput = makeVertexInputState(binding, attributes);
    const auto outputIsSrgb = static_cast<std::uint32_t>(isSrgb(_outputCompatibility.colorFormat));
    const auto specializationData = FragmentSpecializationData {.outputIsSrgb = outputIsSrgb,
                                                                .alphaMode = static_cast<std::uint32_t>(key.alphaMode)};
    const auto specialization = makeMeshFragmentSpecialization(key.alphaMode, specializationData);
    const auto stages = makeShaderStages(vertex, fragment, specialization);
    const auto colorOutput = _outputCompatibility.colorFormat;
    const auto rendering = vk::PipelineRenderingCreateInfo {.colorAttachmentCount = 1,
                                                            .pColorAttachmentFormats = &colorOutput,
                                                            .depthAttachmentFormat = _outputCompatibility.depthFormat};
    const auto multisampling =
        vk::PipelineMultisampleStateCreateInfo {.rasterizationSamples = _outputCompatibility.sampleCount};
    const auto state =
        GraphicsPipelineState {.stages = stages,
                               .rendering = rendering,
                               .multisampling = multisampling,
                               .rasterization = makeRasterizationState(key.renderFace, key.mirrored),
                               .depthStencil = key.alphaMode == AlphaMode::Blend ? blendDepthStencil : depthStencil,
                               .colorBlend = key.alphaMode == AlphaMode::Blend ? alphaBlend : opaqueBlend,
                               .vertexInput = vertexInput,
                               .layout = _pipelineLayout};
    const auto createInfo = makeGraphicsPipelineCreateInfo(state);
    return checkedCreation(_device.createGraphicsPipeline(nullptr, createInfo));
}

auto MeshPipelineCache::createShadowPipeline(const ShadowPipelineKey& key) const
    -> std::expected<vk::raii::Pipeline, Error>
{
    const auto vertexWords = shadowVertexShaderWords(key.hasColor, key.hasBaseColorTexture);
    auto modules =
        createShadowShaderModules(_device, vertexWords, key.alphaMode == AlphaMode::Mask, key.hasBaseColorTexture);
    if (!modules)
    {
        return std::unexpected {modules.error()};
    }
    const auto alphaMode = static_cast<std::uint32_t>(key.alphaMode);
    const auto specialization = makeAlphaModeSpecialization(alphaMode);
    const auto [stages, stageCount] = makeShadowShaderStages(*modules->vertex, modules->fragment, specialization);
    const auto rendering = vk::PipelineRenderingCreateInfo {.depthAttachmentFormat = _outputCompatibility.depthFormat};
    const auto input = makeShadowVertexInput(key);
    const auto vertexInput = makeVertexInputState(input.binding, input.attributes);
    const auto state = GraphicsPipelineState {.stages = std::span {stages}.first(stageCount),
                                              .rendering = rendering,
                                              .multisampling = multisample,
                                              .rasterization = makeRasterizationState(key.renderFace, key.mirrored),
                                              .depthStencil = depthStencil,
                                              .colorBlend = noColorBlend,
                                              .vertexInput = vertexInput,
                                              .layout = _pipelineLayout};
    const auto createInfo = makeGraphicsPipelineCreateInfo(state);
    return checkedCreation(_device.createGraphicsPipeline(nullptr, createInfo));
}
}
