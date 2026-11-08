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
#include <panda/Mesh.hpp>
#include <panda/RenderView.hpp>
#include <span>
#include <utility>
#include <vector>

#include "BuiltinShaders.hpp"
#include "VulkanError.hpp"
#include "renderer/ShaderData.hpp"

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

auto meshVertexShaderWords(bool lit, bool hasColor, bool hasTextureCoordinates) -> std::span<const std::uint32_t>
{
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
    const auto variantIndex = (static_cast<std::size_t>(lit) << 2U) | (static_cast<std::size_t>(hasColor) << 1U) |
                              static_cast<std::size_t>(hasTextureCoordinates);
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
    const auto attributes = draw.mesh.getVertexAttributes();
    const auto findOffset = [&attributes](VertexAttributeSemantic semantic) {
        const auto attribute = std::ranges::find(attributes, semantic, &VertexAttribute::semantic);
        return attribute == attributes.end() ? 0U : attribute->offset;
    };
    const auto lit = draw.material.model == MaterialModel::Lit;
    const auto hasColor = std::ranges::any_of(attributes, [](const auto& attribute) {
        return attribute.semantic == VertexAttributeSemantic::Color;
    });
    const auto hasTextureCoordinates = draw.material.baseColorTexture.isValid() ||
                                       draw.material.metallicRoughnessTexture.isValid() ||
                                       draw.material.emissiveTexture.isValid();
    return {.lit = lit,
            .hasColor = hasColor,
            .hasTextureCoordinates = hasTextureCoordinates,
            .vertexStride = draw.mesh.getVertexStride(),
            .positionOffset = findOffset(VertexAttributeSemantic::Position),
            .normalOffset = lit ? findOffset(VertexAttributeSemantic::Normal) : 0U,
            .texCoordOffset = hasTextureCoordinates ? findOffset(VertexAttributeSemantic::TexCoord0) : 0U,
            .colorOffset = hasColor ? findOffset(VertexAttributeSemantic::Color) : 0U,
            .renderFace = draw.material.renderFace,
            .alphaMode = draw.material.alphaMode,
            .mirrored = glm::determinant(glm::mat3 {draw.modelMatrix}) < 0.0F};
}

auto MeshPipelineCache::makeShadowPipelineKey(const MeshDraw& draw) -> ShadowPipelineKey
{
    const auto attributes = draw.mesh.getVertexAttributes();
    const auto findOffset = [&attributes](VertexAttributeSemantic semantic) {
        const auto attribute = std::ranges::find(attributes, semantic, &VertexAttribute::semantic);
        return attribute == attributes.end() ? 0U : attribute->offset;
    };
    const auto hasColor =
        draw.material.alphaMode == AlphaMode::Mask && std::ranges::any_of(attributes, [](const auto& attribute) {
            return attribute.semantic == VertexAttributeSemantic::Color;
        });
    const auto hasBaseColorTexture =
        draw.material.alphaMode == AlphaMode::Mask && draw.material.baseColorTexture.isValid();
    return {.vertexStride = draw.mesh.getVertexStride(),
            .positionOffset = findOffset(VertexAttributeSemantic::Position),
            .texCoordOffset = hasBaseColorTexture ? findOffset(VertexAttributeSemantic::TexCoord0) : 0U,
            .colorOffset = hasColor ? findOffset(VertexAttributeSemantic::Color) : 0U,
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
    auto attributes = std::vector {
        vk::VertexInputAttributeDescription {.location = PANDA_VERTEX_ATTRIBUTE_POSITION_LOCATION,
                                             .binding = meshVertexBufferBinding,
                                             .format = vk::Format::eR32G32B32Sfloat,
                                             .offset = key.positionOffset}
    };
    if (key.lit)
    {
        attributes.push_back({.location = PANDA_VERTEX_ATTRIBUTE_NORMAL_LOCATION,
                              .binding = meshVertexBufferBinding,
                              .format = vk::Format::eR32G32B32Sfloat,
                              .offset = key.normalOffset});
    }
    if (key.hasTextureCoordinates)
    {
        attributes.push_back({.location = PANDA_VERTEX_ATTRIBUTE_TEX_COORD_0_LOCATION,
                              .binding = meshVertexBufferBinding,
                              .format = vk::Format::eR32G32Sfloat,
                              .offset = key.texCoordOffset});
    }
    if (key.hasColor)
    {
        attributes.push_back({.location = PANDA_VERTEX_ATTRIBUTE_COLOR_LOCATION,
                              .binding = meshVertexBufferBinding,
                              .format = vk::Format::eR32G32B32A32Sfloat,
                              .offset = key.colorOffset});
    }
    return attributes;
}

auto MeshPipelineCache::createPipeline(const MeshPipelineKey& key) const -> std::expected<vk::raii::Pipeline, Error>
{
    const auto vertexWords = meshVertexShaderWords(key.lit, key.hasColor, key.hasTextureCoordinates);
    auto vertex = createShaderModule(_device, vertexWords);
    if (!vertex)
    {
        return std::unexpected {vertex.error()};
    }
    auto fragment = createShaderModule(_device, shaders::meshFragment);
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
