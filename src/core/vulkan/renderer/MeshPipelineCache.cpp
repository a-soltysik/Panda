// clang-format off
#include <panda/base/Error.hpp>
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include "renderer/MeshPipelineCache.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <glm/ext/matrix_float3x3.hpp>
#include <glm/ext/vector_float4.hpp>
#include <glm/matrix.hpp>
#include <iterator>
#include <memory>
#include <optional>
#include <panda/diagnostics/Assert.hpp>
#include <panda/diagnostics/Logger.hpp>
#include <panda/geometry/VertexLayout.hpp>
#include <panda/rendering/RenderView.hpp>
#include <panda/rendering/ShaderProgram.hpp>
#include <panda/resources/Mesh.hpp>
#include <ranges>
#include <span>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "BuiltinShaders.hpp"
#include "VulkanError.hpp"
#include "renderer/ApplicationShaderBindings.hpp"
#include "renderer/ShaderData.hpp"
#include "renderer/ShaderProgramState.hpp"
#include "renderer/ShaderReflection.hpp"
#include "resources/MeshResource.hpp"

namespace panda::core::vulkan::renderer
{
using detail::checkedCreation;

namespace
{
constexpr auto meshVertexBufferBinding = std::uint32_t {0};
constexpr auto meshInstanceVertexBufferBinding = std::uint32_t {1};
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

auto shadowAttributeOffset(const VertexAttribute* attribute, bool used) -> std::uint32_t
{
    return used ? vertexAttributeOffset(attribute) : 0U;
}

auto hasCustomShadow(const auto& program) -> bool
{
    return program && std::get_if<ProgramVariant>(&program->shadow) != nullptr;
}

auto hasShadowPass(std::span<const Light> lights) -> bool
{
    return std::ranges::any_of(lights, [](const auto& light) {
        return std::visit(
            [](const auto& value) {
                return value.shadow.has_value();
            },
            light);
    });
}

auto makeColorPipelineRendering(const MeshPipelineCache::OutputCompatibility& compatibility)
    -> vk::PipelineRenderingCreateInfo
{
    return {.colorAttachmentCount = 1,
            .pColorAttachmentFormats = &compatibility.colorFormat,
            .depthAttachmentFormat = compatibility.depthFormat};
}

auto makePipelineMultisampling(vk::SampleCountFlagBits sampleCount) -> vk::PipelineMultisampleStateCreateInfo
{
    return {.rasterizationSamples = sampleCount};
}

constexpr auto shaderAttributeName(VertexAttributeSemantic semantic) -> std::string_view
{
    switch (semantic)
    {
    case VertexAttributeSemantic::Position:
        return "vertexPosition";
    case VertexAttributeSemantic::Normal:
        return "vertexNormal";
    case VertexAttributeSemantic::TexCoord0:
        return "vertexTextureCoordinates";
    case VertexAttributeSemantic::Tangent:
        return "vertexTangent";
    case VertexAttributeSemantic::Color:
        return "vertexColor";
    }
    return {};
}

auto reflectedFormat(vk::Format format) -> std::optional<ShaderInterfaceFormat>
{
    // Map the supported subset without treating every Vulkan format as required.
    switch (std::to_underlying(format))
    {
    case std::to_underlying(vk::Format::eR32G32Sfloat):
        return ShaderInterfaceFormat::Float2;
    case std::to_underlying(vk::Format::eR32G32B32Sfloat):
        return ShaderInterfaceFormat::Float3;
    case std::to_underlying(vk::Format::eR32G32B32A32Sfloat):
        return ShaderInterfaceFormat::Float4;
    default:
        return std::nullopt;
    }
}

auto makeVertexAttributeDescription(std::span<const ShaderInterfaceVariable> vertexInputs,
                                    VertexAttributeSemantic semantic,
                                    vk::Format format,
                                    std::uint32_t offset) -> Result<vk::VertexInputAttributeDescription>
{
    const auto name = shaderAttributeName(semantic);
    const auto input = std::ranges::find(vertexInputs, name, &ShaderInterfaceVariable::name);
    const auto expectedFormat = reflectedFormat(format);
    if (input == vertexInputs.end() || !expectedFormat || input->format != *expectedFormat)
    {
        return std::unexpected {makeError(
            ErrorCode::InvalidArgument,
            "Vertex shader input '" + std::string {name} + "' is missing or incompatible with the mesh layout")};
    }
    return vk::VertexInputAttributeDescription {.location = input->location,
                                                .binding = meshVertexBufferBinding,
                                                .format = format,
                                                .offset = offset};
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

auto makeShaderStages(vk::ShaderModule vertex, vk::ShaderModule fragment)
    -> std::array<vk::PipelineShaderStageCreateInfo, 2>
{
    return {
        vk::PipelineShaderStageCreateInfo {.stage = vk::ShaderStageFlagBits::eVertex,
                                           .module = vertex,
                                           .pName = "main"},
        vk::PipelineShaderStageCreateInfo {.stage = vk::ShaderStageFlagBits::eFragment,
                                           .module = fragment,
                                           .pName = "main"}
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

struct VertexAttributeSource
{
    VertexAttributeSemantic semantic;
    vk::Format format;
    std::uint32_t offset;
    bool enabled = true;
};

auto meshVertexAttributeSources(const MeshPipelineCache::MeshPipelineKey& key) -> std::array<VertexAttributeSource, 5>
{
    using enum VertexAttributeSemantic;
    return {
        VertexAttributeSource {.semantic = Position,
                               .format = vk::Format::eR32G32B32Sfloat,
                               .offset = key.positionOffset},
        VertexAttributeSource {.semantic = Normal,
                               .format = vk::Format::eR32G32B32Sfloat,
                               .offset = key.normalOffset,
                               .enabled = key.lit},
        VertexAttributeSource {.semantic = Tangent,
                               .format = vk::Format::eR32G32B32A32Sfloat,
                               .offset = key.tangentOffset,
                               .enabled = key.hasNormalMap},
        VertexAttributeSource {.semantic = TexCoord0,
                               .format = vk::Format::eR32G32Sfloat,
                               .offset = key.texCoordOffset,
                               .enabled = key.hasTextureCoordinates},
        VertexAttributeSource {.semantic = Color,
                               .format = vk::Format::eR32G32B32A32Sfloat,
                               .offset = key.colorOffset,
                               .enabled = key.hasColor}
    };
}

auto collectVertexAttributes(std::span<const ShaderInterfaceVariable> inputs,
                             std::span<const VertexAttributeSource> sources,
                             bool onlyReflected = false) -> Result<std::vector<vk::VertexInputAttributeDescription>>
{
    auto attributes = std::vector<vk::VertexInputAttributeDescription> {};
    attributes.reserve(sources.size());
    for (const auto& source : sources | std::views::filter(&VertexAttributeSource::enabled))
    {
        if (onlyReflected &&
            std::ranges::find(inputs, shaderAttributeName(source.semantic), &ShaderInterfaceVariable::name) ==
                inputs.end())
        {
            continue;
        }
        auto attribute = makeVertexAttributeDescription(inputs, source.semantic, source.format, source.offset);
        if (!attribute)
        {
            return std::unexpected {std::move(attribute.error())};
        }
        attributes.push_back(*attribute);
    }
    if (attributes.size() != inputs.size())
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Vertex shader declares an unsupported input")};
    }
    return attributes;
}

struct InstanceAttributeSource
{
    std::string_view name;
    vk::Format format;
    std::uint32_t offset;
    std::uint32_t matrixColumns {1};
    bool shadowInput {false};
};

constexpr auto instanceAttributeSources = std::array {
    InstanceAttributeSource {.name = "instanceModelMatrix",
                             .format = vk::Format::eR32G32B32A32Sfloat,
                             .offset = offsetof(MeshInstanceData, modelMatrix),
                             .matrixColumns = 4,
                             .shadowInput = true},
    InstanceAttributeSource {.name = "instanceNormalColumns",
                             .format = vk::Format::eR32G32B32A32Sfloat,
                             .offset = offsetof(MeshInstanceData, normalColumns),
                             .matrixColumns = 3},
    InstanceAttributeSource {.name = "instanceTint",
                             .format = vk::Format::eR32G32B32A32Sfloat,
                             .offset = offsetof(MeshInstanceData, tint),
                             .shadowInput = true},
    InstanceAttributeSource {.name = "instanceMaterialIndex",
                             .format = vk::Format::eR32Uint,
                             .offset = offsetof(MeshInstanceData, materialIndex),
                             .shadowInput = true}
};

auto isInstanceAttribute(std::string_view name) -> bool
{
    return std::ranges::any_of(instanceAttributeSources, [name](const auto& source) {
        return source.name == name;
    });
}

auto meshVertexInputs(std::span<const ShaderInterfaceVariable> inputs) -> std::vector<ShaderInterfaceVariable>
{
    auto meshInputs = std::vector<ShaderInterfaceVariable> {};
    std::ranges::copy_if(inputs, std::back_inserter(meshInputs), [](const auto& input) {
        return !isInstanceAttribute(input.name);
    });
    return meshInputs;
}

auto isMeshDrawMirrored(const MeshDraw& draw) -> bool
{
    return !draw.instances.empty() && glm::determinant(glm::mat3 {draw.instances.front().modelMatrix}) < 0.0F;
}

auto matchesInstanceAttributeFormat(vk::Format format, ShaderInterfaceFormat reflectedFormat) -> bool
{
    if (format == vk::Format::eR32Uint)
    {
        return reflectedFormat == ShaderInterfaceFormat::UnsignedInteger;
    }
    if (format == vk::Format::eR32Sfloat)
    {
        return reflectedFormat == ShaderInterfaceFormat::Float;
    }
    return reflectedFormat == ShaderInterfaceFormat::Float4;
}

auto isCompatibleInstanceInput(const ShaderInterfaceVariable& input, const InstanceAttributeSource& source) -> bool
{
    return input.matrixColumns == source.matrixColumns && matchesInstanceAttributeFormat(source.format, input.format);
}

void appendInstanceAttributeDescriptions(std::vector<vk::VertexInputAttributeDescription>& attributes,
                                         const ShaderInterfaceVariable& input,
                                         const InstanceAttributeSource& source)
{
    for (auto column = std::uint32_t {}; column < source.matrixColumns; ++column)
    {
        attributes.push_back({.location = input.location + column,
                              .binding = meshInstanceVertexBufferBinding,
                              .format = source.format,
                              .offset = source.offset + (column * static_cast<std::uint32_t>(sizeof(glm::vec4)))});
    }
}

auto makeInstanceAttributeDescriptions(std::span<const ShaderInterfaceVariable> inputs, bool shadow)
    -> Result<std::vector<vk::VertexInputAttributeDescription>>
{
    auto attributes = std::vector<vk::VertexInputAttributeDescription> {};
    for (const auto& source : instanceAttributeSources)
    {
        if (shadow && !source.shadowInput)
        {
            continue;
        }
        const auto input = std::ranges::find(inputs, source.name, &ShaderInterfaceVariable::name);
        if (input == inputs.end())
        {
            return std::unexpected {
                makeError(ErrorCode::InvalidArgument, "Instanced vertex shader is missing an instance input")};
        }
        if (!isCompatibleInstanceInput(*input, source))
        {
            return std::unexpected {makeError(ErrorCode::InvalidArgument,
                                              "Instanced vertex shader declares an incompatible instance input")};
        }
        appendInstanceAttributeDescriptions(attributes, *input, source);
    }
    return attributes;
}

auto appendInstancedVertexAttributes(std::vector<vk::VertexInputAttributeDescription>& attributes,
                                     std::span<const ShaderInterfaceVariable> inputs) -> Result<void>
{
    auto instanceAttributes = makeInstanceAttributeDescriptions(inputs, false);
    if (!instanceAttributes)
    {
        return std::unexpected {instanceAttributes.error()};
    }
    std::ranges::move(*instanceAttributes, std::back_inserter(attributes));
    return {};
}

auto shadowVertexAttributes(const MeshPipelineCache::ShadowPipelineKey& key,
                            std::span<const ShaderInterfaceVariable> inputs)
    -> Result<std::vector<vk::VertexInputAttributeDescription>>
{
    using enum VertexAttributeSemantic;
    const auto sources = std::array {
        VertexAttributeSource {.semantic = Position,
                               .format = vk::Format::eR32G32B32Sfloat,
                               .offset = key.positionOffset},
        VertexAttributeSource {.semantic = TexCoord0,
                               .format = vk::Format::eR32G32Sfloat,
                               .offset = key.texCoordOffset,
                               .enabled = key.hasBaseColorTexture},
        VertexAttributeSource {.semantic = Color,
                               .format = vk::Format::eR32G32B32A32Sfloat,
                               .offset = key.colorOffset,
                               .enabled = key.hasColor}
    };
    auto meshInputs = meshVertexInputs(inputs);
    auto attributes = collectVertexAttributes(meshInputs, sources);
    if (!attributes || !key.instanced)
    {
        return attributes;
    }
    return makeInstanceAttributeDescriptions(inputs, true).transform([&](auto instanceAttributes) {
        std::ranges::move(instanceAttributes, std::back_inserter(*attributes));
        return std::move(*attributes);
    });
}

auto customShadowMeshKey(const MeshPipelineCache::ShadowPipelineKey& key) -> MeshPipelineCache::MeshPipelineKey
{
    return {.lit = key.hasNormal,
            .hasColor = key.hasColor,
            .hasTextureCoordinates = key.hasTextureCoordinates,
            .hasNormalMap = key.hasTangent,
            .vertexStride = key.vertexStride,
            .positionOffset = key.positionOffset,
            .normalOffset = key.normalOffset,
            .texCoordOffset = key.texCoordOffset,
            .tangentOffset = key.tangentOffset,
            .colorOffset = key.colorOffset,
            .renderFace = key.renderFace,
            .mirrored = key.mirrored,
            .customProgramRevision = key.customProgramRevision,
            .customProgram = key.customProgram};
}

auto meshNormalMapVertexShaderWords(bool hasColor, bool instanced) -> std::span<const std::uint32_t>
{
    static constexpr auto variants = std::array {
        std::array {std::span<const std::uint32_t> {shaders::meshLitPlainNormalVertex},
                    std::span<const std::uint32_t> {shaders::meshLitPlainNormalInstancedVertex}},
        std::array {std::span<const std::uint32_t> {shaders::meshLitColorNormalVertex},
                    std::span<const std::uint32_t> {shaders::meshLitColorNormalInstancedVertex}}
    };
    return variants.at(static_cast<std::size_t>(hasColor)).at(static_cast<std::size_t>(instanced));
}

auto meshVertexShaderWords(const MeshPipelineCache::MeshPipelineKey& key) -> std::span<const std::uint32_t>
{
    if (key.hasNormalMap)
    {
        return meshNormalMapVertexShaderWords(key.hasColor, key.instanced);
    }
    static constexpr auto variants = std::array {
        std::array {std::span<const std::uint32_t> {shaders::meshUnlitPlainVertex},
                    std::span<const std::uint32_t> {shaders::meshUnlitPlainInstancedVertex}       },
        std::array {std::span<const std::uint32_t> {shaders::meshUnlitPlainTextureVertex},
                    std::span<const std::uint32_t> {shaders::meshUnlitPlainTextureInstancedVertex}},
        std::array {std::span<const std::uint32_t> {shaders::meshUnlitColorVertex},
                    std::span<const std::uint32_t> {shaders::meshUnlitColorInstancedVertex}       },
        std::array {std::span<const std::uint32_t> {shaders::meshUnlitColorTextureVertex},
                    std::span<const std::uint32_t> {shaders::meshUnlitColorTextureInstancedVertex}},
        std::array {std::span<const std::uint32_t> {shaders::meshLitPlainVertex},
                    std::span<const std::uint32_t> {shaders::meshLitPlainInstancedVertex}         },
        std::array {std::span<const std::uint32_t> {shaders::meshLitPlainTextureVertex},
                    std::span<const std::uint32_t> {shaders::meshLitPlainTextureInstancedVertex}  },
        std::array {std::span<const std::uint32_t> {shaders::meshLitColorVertex},
                    std::span<const std::uint32_t> {shaders::meshLitColorInstancedVertex}         },
        std::array {std::span<const std::uint32_t> {shaders::meshLitColorTextureVertex},
                    std::span<const std::uint32_t> {shaders::meshLitColorTextureInstancedVertex}  }
    };
    const auto variantIndex = (static_cast<std::size_t>(key.lit) << 2U) |
                              (static_cast<std::size_t>(key.hasColor) << 1U) |
                              static_cast<std::size_t>(key.hasTextureCoordinates);
    return variants.at(variantIndex).at(static_cast<std::size_t>(key.instanced));
}

auto shadowVertexShaderWords(bool hasColor, bool hasTexture, bool instanced) -> std::span<const std::uint32_t>
{
    static constexpr auto variants =
        std::array {std::span<const std::uint32_t> {shaders::shadowVertex},
                    std::span<const std::uint32_t> {shaders::shadowInstancedVertex},
                    std::span<const std::uint32_t> {shaders::shadowTextureVertex},
                    std::span<const std::uint32_t> {shaders::shadowTextureInstancedVertex},
                    std::span<const std::uint32_t> {shaders::shadowVertexAlphaVertex},
                    std::span<const std::uint32_t> {shaders::shadowVertexAlphaInstancedVertex},
                    std::span<const std::uint32_t> {shaders::shadowVertexAlphaTextureVertex},
                    std::span<const std::uint32_t> {shaders::shadowVertexAlphaTextureInstancedVertex}};
    const auto variantIndex = (static_cast<std::size_t>(hasColor) << 2U) |
                              (static_cast<std::size_t>(hasTexture) << 1U) | static_cast<std::size_t>(instanced);
    return variants.at(variantIndex);
}

auto makeVertexInputState(std::span<const vk::VertexInputBindingDescription> bindings,
                          std::span<const vk::VertexInputAttributeDescription> attributes)
    -> vk::PipelineVertexInputStateCreateInfo
{
    return {.vertexBindingDescriptionCount = static_cast<std::uint32_t>(bindings.size()),
            .pVertexBindingDescriptions = bindings.data(),
            .vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size()),
            .pVertexAttributeDescriptions = attributes.data()};
}

auto meshVertexInputBindings(const MeshPipelineCache::MeshPipelineKey& key)
    -> std::array<vk::VertexInputBindingDescription, 2>
{
    return std::array {
        vk::VertexInputBindingDescription {.binding = meshVertexBufferBinding,
                                           .stride = key.vertexStride,
                                           .inputRate = vk::VertexInputRate::eVertex  },
        vk::VertexInputBindingDescription {.binding = meshInstanceVertexBufferBinding,
                                           .stride = static_cast<std::uint32_t>(sizeof(MeshInstanceData)),
                                           .inputRate = vk::VertexInputRate::eInstance}
    };
}

auto meshVertexInputState(const MeshPipelineCache::MeshPipelineKey& key,
                          const std::array<vk::VertexInputBindingDescription, 2>& bindings,
                          std::span<const vk::VertexInputAttributeDescription> attributes)
    -> vk::PipelineVertexInputStateCreateInfo
{
    return key.procedural ? vk::PipelineVertexInputStateCreateInfo {}
                          : makeVertexInputState(std::span {bindings}.first(key.instanced ? 2U : 1U), attributes);
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
    std::uint32_t alphaMode;
};

auto makeFragmentSpecializationEntry(std::uint32_t constantId) -> vk::SpecializationMapEntry
{
    return {.constantID = constantId,
            .offset = offsetof(FragmentSpecializationData, alphaMode),
            .size = sizeof(std::uint32_t)};
}

auto shadowFragmentShaderWords(bool hasAlphaMask, bool hasBaseColorTexture)
    -> std::optional<std::span<const std::uint32_t>>
{
    if (!hasAlphaMask)
    {
        return std::nullopt;
    }
    return hasBaseColorTexture ? std::span<const std::uint32_t> {shaders::shadowTextureFragment}
                               : std::span<const std::uint32_t> {shaders::shadowFragment};
}

auto createShadowShaderModules(const vk::raii::Device& device,
                               std::span<const std::uint32_t> vertexWords,
                               std::optional<std::span<const std::uint32_t>> fragmentWords)
    -> Result<ShadowShaderModules>
{
    auto vertex = detail::createShaderModule(device, vertexWords);
    if (!vertex)
    {
        return std::unexpected {std::move(vertex.error())};
    }
    if (!fragmentWords)
    {
        return ShadowShaderModules {.vertex = std::move(*vertex), .fragment = std::nullopt};
    }
    return detail::createShaderModule(device, *fragmentWords).transform([&](auto fragment) {
        return ShadowShaderModules {.vertex = std::move(*vertex), .fragment = std::move(fragment)};
    });
}

auto customShadowStages(const ShadowShaderModules& modules)
    -> std::pair<std::array<vk::PipelineShaderStageCreateInfo, 2>, std::uint32_t>
{
    auto stages = std::array {
        vk::PipelineShaderStageCreateInfo {.stage = vk::ShaderStageFlagBits::eVertex,
                                           .module = *modules.vertex,
                                           .pName = "main"},
        vk::PipelineShaderStageCreateInfo {}
    };
    auto stageCount = std::uint32_t {1};
    if (modules.fragment)
    {
        stages.at(1) = {.stage = vk::ShaderStageFlagBits::eFragment, .module = **modules.fragment, .pName = "main"};
        ++stageCount;
    }
    return {stages, stageCount};
}

auto meshFragmentShaderWords(const MeshPipelineCache::MeshPipelineKey& key) -> std::span<const std::uint32_t>
{
    if (key.hasEnvironment)
    {
        return key.hasNormalMap ? std::span<const std::uint32_t> {shaders::meshNormalEnvironmentFragment}
                                : std::span<const std::uint32_t> {shaders::meshEnvironmentFragment};
    }
    return key.hasNormalMap ? std::span<const std::uint32_t> {shaders::meshNormalFragment}
                            : std::span<const std::uint32_t> {shaders::meshFragment};
}

struct ShaderProgramRequest
{
    std::span<const std::uint32_t> vertexWords;
    std::optional<std::span<const std::uint32_t>> fragmentWords;
    std::span<const ShaderDescriptor> descriptorSchema;
    std::string_view label;
};

template <typename Cache, typename Key>
auto cachedShaderProgram(Cache& cache, const Key& key, const ShaderProgramRequest& request)
    -> Result<std::reference_wrapper<const ShaderProgramLayout>>
{
    if (const auto found = cache.find(key); found != cache.end())
    {
        return std::cref(found->second);
    }
    auto layout = reflectGraphicsProgram(request.vertexWords, request.fragmentWords, request.label);
    if (!layout)
    {
        return std::unexpected {layout.error()};
    }
    if (auto valid = validateShaderProgramDescriptors(*layout, request.descriptorSchema, request.label); !valid)
    {
        return std::unexpected {valid.error()};
    }
    return std::cref(cache.emplace(key, std::move(*layout)).first->second);
}
}

MeshPipelineCache::MeshPipelineCache(const vk::raii::Device& device,
                                     vk::PipelineLayout pipelineLayout,
                                     std::span<const ShaderDescriptor> shaderDescriptorSchema,
                                     const ApplicationShaderBindings* applicationBindings)
    : _device {device},
      _pipelineLayout {pipelineLayout},
      _applicationBindings {applicationBindings},
      _shaderDescriptorSchema {shaderDescriptorSchema.begin(), shaderDescriptorSchema.end()}
{
}

auto MeshPipelineCache::meshShaderLayout(const MeshPipelineKey& key)
    -> Result<std::reference_wrapper<const ShaderProgramLayout>>
{
    const auto variant = std::array {key.lit,
                                     key.hasColor,
                                     key.hasTextureCoordinates,
                                     key.hasNormalMap,
                                     key.hasEnvironment,
                                     key.instanced};
    return cachedShaderProgram(_meshShaderLayouts,
                               variant,
                               ShaderProgramRequest {.vertexWords = meshVertexShaderWords(key),
                                                     .fragmentWords = meshFragmentShaderWords(key),
                                                     .descriptorSchema = _shaderDescriptorSchema,
                                                     .label = "mesh pipeline"});
}

auto MeshPipelineCache::shadowShaderLayout(const ShadowPipelineKey& key)
    -> Result<std::reference_wrapper<const ShaderProgramLayout>>
{
    const auto variant =
        std::array {key.hasColor, key.hasBaseColorTexture, key.alphaMode == AlphaMode::Mask, key.instanced};
    return cachedShaderProgram(
        _shadowShaderLayouts,
        variant,
        ShaderProgramRequest {
            .vertexWords = shadowVertexShaderWords(key.hasColor, key.hasBaseColorTexture, key.instanced),
            .fragmentWords = shadowFragmentShaderWords(key.alphaMode == AlphaMode::Mask, key.hasBaseColorTexture),
            .descriptorSchema = _shaderDescriptorSchema,
            .label = "shadow pipeline"});
}

auto MeshPipelineCache::skyboxShaderLayout() -> Result<std::reference_wrapper<const ShaderProgramLayout>>
{
    if (!_skyboxShaderLayout)
    {
        auto layout = reflectGraphicsProgram(shaders::skyboxVertex,
                                             std::span<const std::uint32_t> {shaders::skyboxFragment},
                                             "skybox pipeline");
        if (!layout)
        {
            return std::unexpected {layout.error()};
        }
        if (auto valid = validateShaderProgramDescriptors(*layout, _shaderDescriptorSchema, "skybox pipeline"); !valid)
        {
            return std::unexpected {valid.error()};
        }
        _skyboxShaderLayout = std::move(*layout);
    }
    return std::cref(*_skyboxShaderLayout);
}

auto MeshPipelineCache::makeMeshPipelineKey(const MeshDraw& draw, bool hasEnvironment) -> MeshPipelineKey
{
    const auto program = draw.material.shaderProgram.snapshot();
    const auto lit = draw.material.model == MaterialModel::Lit;
    const auto* position = findVertexAttribute(draw.mesh, VertexAttributeSemantic::Position);
    const auto* normal = lit || program ? findVertexAttribute(draw.mesh, VertexAttributeSemantic::Normal) : nullptr;
    const auto* color = findVertexAttribute(draw.mesh, VertexAttributeSemantic::Color);
    const auto hasUv = hasVertexAttribute(draw.mesh, VertexAttributeSemantic::TexCoord0);
    const auto hasTangentAttribute = hasVertexAttribute(draw.mesh, VertexAttributeSemantic::Tangent);
    const auto hasNormalMap = program ? hasTangentAttribute : usesNormalMap(draw, hasUv, hasTangentAttribute);
    const auto hasTextureCoordinates = program ? hasUv : usesTextureCoordinates(draw.material, hasNormalMap);
    const auto* texCoord =
        hasTextureCoordinates ? findVertexAttribute(draw.mesh, VertexAttributeSemantic::TexCoord0) : nullptr;
    const auto* tangent = hasNormalMap ? findVertexAttribute(draw.mesh, VertexAttributeSemantic::Tangent) : nullptr;
    return {.lit = program ? normal != nullptr : lit,
            .hasColor = color != nullptr,
            .hasTextureCoordinates = hasTextureCoordinates,
            .hasNormalMap = hasNormalMap,
            .instanced = !program && !draw.instances.empty(),
            .vertexStride = draw.mesh.getVertexStride(),
            .positionOffset = vertexAttributeOffset(position),
            .normalOffset = vertexAttributeOffset(normal),
            .texCoordOffset = vertexAttributeOffset(texCoord),
            .tangentOffset = vertexAttributeOffset(tangent),
            .colorOffset = vertexAttributeOffset(color),
            .renderFace = draw.material.renderFace,
            .alphaMode = draw.material.alphaMode,
            .mirrored = isMeshDrawMirrored(draw),
            .hasEnvironment = !program && lit && hasEnvironment,
            .customProgramRevision = program ? program->revision : 0,
            .customProgram = program};
}

auto MeshPipelineCache::makeProceduralPipelineKey(const ProceduralDraw& draw) -> MeshPipelineKey
{
    const auto program = draw.shaderProgram.snapshot();
    return {.renderFace = RenderFace::Both,
            .procedural = true,
            .customProgramRevision = program ? program->revision : 0,
            .customProgram = program};
}

auto MeshPipelineCache::makeShadowPipelineKey(const MeshDraw& draw) -> ShadowPipelineKey
{
    auto customProgram = draw.material.shaderProgram.snapshot();
    customProgram = hasCustomShadow(customProgram) ? customProgram : nullptr;
    const auto* position = findVertexAttribute(draw.mesh, VertexAttributeSemantic::Position);
    const auto* normal = findVertexAttribute(draw.mesh, VertexAttributeSemantic::Normal);
    const auto* texCoord = findVertexAttribute(draw.mesh, VertexAttributeSemantic::TexCoord0);
    const auto* tangent = findVertexAttribute(draw.mesh, VertexAttributeSemantic::Tangent);
    const auto* color = findVertexAttribute(draw.mesh, VertexAttributeSemantic::Color);
    const auto hasColor = color != nullptr && (customProgram != nullptr || draw.material.alphaMode == AlphaMode::Mask);
    const auto hasBaseColorTexture =
        draw.material.alphaMode == AlphaMode::Mask && draw.material.baseColorTexture.isValid();
    return {.vertexStride = draw.mesh.getVertexStride(),
            .positionOffset = vertexAttributeOffset(position),
            .texCoordOffset = shadowAttributeOffset(texCoord, hasBaseColorTexture),
            .colorOffset = shadowAttributeOffset(color, hasColor),
            .hasColor = hasColor,
            .hasBaseColorTexture = hasBaseColorTexture,
            .instanced = !draw.material.shaderProgram.isValid() && !draw.instances.empty(),
            .renderFace = draw.material.renderFace,
            .alphaMode = draw.material.alphaMode,
            .mirrored = isMeshDrawMirrored(draw),
            .hasNormal = customProgram != nullptr && normal != nullptr,
            .hasTangent = customProgram != nullptr && tangent != nullptr,
            .hasTextureCoordinates = customProgram != nullptr && texCoord != nullptr,
            .normalOffset = shadowAttributeOffset(normal, customProgram != nullptr),
            .tangentOffset = shadowAttributeOffset(tangent, customProgram != nullptr),
            .customProgramRevision = customProgram ? customProgram->revision : 0,
            .customProgram = customProgram};
}

auto MeshPipelineCache::makeShadowPipelineKey(const ProceduralDraw& draw) -> ShadowPipelineKey
{
    const auto program = draw.shaderProgram.snapshot();
    return {.renderFace = RenderFace::Both,
            .procedural = true,
            .customProgramRevision = program ? program->revision : 0,
            .customProgram = program};
}

void MeshPipelineCache::setOutputCompatibility(OutputCompatibility compatibility)
{
    if (compatibility.colorFormat != _outputCompatibility.colorFormat ||
        compatibility.depthFormat != _outputCompatibility.depthFormat ||
        compatibility.sampleCount != _outputCompatibility.sampleCount)
    {
        _pipelines.clear();
        _skyboxPipeline.reset();
    }
    if (compatibility.depthFormat != _outputCompatibility.depthFormat)
    {
        _shadowPipelines.clear();
    }
    _outputCompatibility = compatibility;
}

auto MeshPipelineCache::prepare(const RenderView& view) -> std::expected<void, Error>
{
    if (view.skybox)
    {
        if (const auto prepared = prepareSkybox(); !prepared)
        {
            return prepared;
        }
    }
    const auto shadows = hasShadowPass(view.lights);
    if (auto prepared = prepareMeshDraws(view, shadows); !prepared)
    {
        return prepared;
    }
    return prepareProceduralDraws(view, shadows);
}

auto MeshPipelineCache::prepareMeshDraws(const RenderView& view, bool hasShadowPass) -> Result<void>
{
    for (const auto& draw : view.meshes)
    {
        if (draw.instances.empty())
        {
            continue;
        }
        if (const auto prepared = prepareDraw(draw, view.environment.has_value(), hasShadowPass); !prepared)
        {
            return prepared;
        }
    }
    return {};
}

auto MeshPipelineCache::prepareProceduralDraws(const RenderView& view, bool hasShadowPass) -> Result<void>
{
    for (const auto& draw : view.proceduralDraws)
    {
        if (draw.vertexCount == 0 || draw.instanceCount == 0)
        {
            continue;
        }
        if (const auto prepared = prepareProceduralDraw(draw, hasShadowPass); !prepared)
        {
            return prepared;
        }
    }
    return {};
}

auto MeshPipelineCache::prepareSkybox() -> std::expected<void, Error>
{
    if (_skyboxPipeline)
    {
        return {};
    }
    auto pipeline = createSkyboxPipeline();
    if (!pipeline)
    {
        return std::unexpected {pipeline.error()};
    }
    _skyboxPipeline.emplace(std::move(*pipeline));
    return {};
}

auto MeshPipelineCache::prepareDraw(const MeshDraw& draw, bool hasEnvironment, bool hasShadowPass) -> Result<void>
{
    if (draw.instances.size() > 1 && draw.material.shaderProgram.isValid())
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Custom mesh programs cannot be instanced")};
    }
    if (draw.material.shaderProgram.isValid())
    {
        return prepareCustomDraw(draw, hasShadowPass);
    }
    warnMissingNormalMapInputs(draw);
    return prepare(makeMeshPipelineKey(draw, hasEnvironment)).and_then([&] {
        if (draw.material.alphaMode == AlphaMode::Blend)
        {
            return Result<void> {};
        }
        return prepare(makeShadowPipelineKey(draw));
    });
}

void MeshPipelineCache::warnMissingNormalMapInputs(const MeshDraw& draw)
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
}

auto MeshPipelineCache::prepareCustomDraw(const MeshDraw& draw, bool hasShadowPass) -> Result<void>
{
    const auto camera = makeMeshPipelineKey(draw);
    const auto& program = camera.customProgram;
    if (!program || program->device != &_device || program->vertexInput != ShaderVertexInput::StandardMesh)
    {
        return std::unexpected {
            makeError(ErrorCode::InvalidArgument, "Mesh draw needs a standard mesh shader program from this Engine")};
    }
    return prepare(camera).and_then([&] {
        return prepareCustomShadowDraw(draw, camera, hasShadowPass);
    });
}

auto MeshPipelineCache::prepareCustomShadowDraw(const MeshDraw& draw, const MeshPipelineKey& camera, bool hasShadowPass)
    -> Result<void>
{
    if (!hasShadowPass || draw.material.alphaMode == AlphaMode::Blend)
    {
        return {};
    }
    const auto& program = camera.customProgram;
    const auto* explicitShadow = std::get_if<ProgramVariant>(&program->shadow);
    if (explicitShadow != nullptr)
    {
        return prepare(makeShadowPipelineKey(draw));
    }
    const auto* mode = std::get_if<ShaderShadowMode>(&program->shadow);
    if (mode == nullptr)
    {
        return std::unexpected {
            makeError(ErrorCode::Unsupported, "Explicit shader shadows are unavailable for mesh draws")};
    }
    if (*mode == ShaderShadowMode::None)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Custom mesh draw has no shadow variant")};
    }
    return prepare(makeShadowPipelineKey(draw));
}

auto MeshPipelineCache::prepareProceduralDraw(const ProceduralDraw& draw, bool hasShadowPass) -> Result<void>
{
    const auto program = draw.shaderProgram.snapshot();
    if (!program || program->device != &_device || program->vertexInput != ShaderVertexInput::Procedural)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument,
                                          "Procedural draw needs a procedural shader program from this Engine")};
    }
    if (hasShadowPass)
    {
        if (std::get_if<ProgramVariant>(&program->shadow) == nullptr)
        {
            return std::unexpected {
                makeError(ErrorCode::InvalidArgument, "Procedural draw has no explicit shadow variant")};
        }
        return prepare(makeProceduralPipelineKey(draw)).and_then([&] {
            return prepare(makeShadowPipelineKey(draw));
        });
    }
    return prepare(makeProceduralPipelineKey(draw));
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
    auto cacheKey = key;
    cacheKey.customProgram.reset();
    _pipelines.emplace(std::move(cacheKey), std::move(*pipeline));
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
    auto cacheKey = key;
    cacheKey.customProgram.reset();
    _shadowPipelines.emplace(std::move(cacheKey), std::move(*pipeline));
    return {};
}

auto MeshPipelineCache::getPipeline(const MeshDraw& draw, bool hasEnvironment) const -> vk::Pipeline
{
    return *_pipelines.at(makeMeshPipelineKey(draw, hasEnvironment));
}

auto MeshPipelineCache::getPipeline(const ProceduralDraw& draw) const -> vk::Pipeline
{
    return *_pipelines.at(makeProceduralPipelineKey(draw));
}

auto MeshPipelineCache::getShadowPipeline(const MeshDraw& draw) const -> vk::Pipeline
{
    return *_shadowPipelines.at(makeShadowPipelineKey(draw));
}

auto MeshPipelineCache::getShadowPipeline(const ProceduralDraw& draw) const -> vk::Pipeline
{
    return *_shadowPipelines.at(makeShadowPipelineKey(draw));
}

auto MeshPipelineCache::getSkyboxPipeline() const -> vk::Pipeline
{
    return *expect(_skyboxPipeline, "Skybox pipeline was not prepared");
}

auto MeshPipelineCache::makeVertexAttributeDescriptions(const MeshPipelineKey& key,
                                                        std::span<const ShaderInterfaceVariable> vertexInputs)
    -> Result<std::vector<vk::VertexInputAttributeDescription>>
{
    auto attributes = collectVertexAttributes(meshVertexInputs(vertexInputs),
                                              meshVertexAttributeSources(key),
                                              key.customProgram != nullptr);
    if (!attributes || !key.instanced)
    {
        return attributes;
    }
    if (auto appended = appendInstancedVertexAttributes(*attributes, vertexInputs); !appended)
    {
        return std::unexpected {appended.error()};
    }
    return attributes;
}

auto MeshPipelineCache::createPipeline(const MeshPipelineKey& key) -> std::expected<vk::raii::Pipeline, Error>
{
    if (key.customProgram)
    {
        return createCustomPipeline(key);
    }
    return createBuiltinPipeline(key);
}

auto MeshPipelineCache::createBuiltinPipeline(const MeshPipelineKey& key) -> std::expected<vk::raii::Pipeline, Error>
{
    const auto vertexWords = meshVertexShaderWords(key);
    const auto fragmentWords = meshFragmentShaderWords(key);
    auto programLayout = meshShaderLayout(key);
    if (!programLayout)
    {
        return std::unexpected {programLayout.error()};
    }
    const auto& layout = programLayout->get();
    auto attributes = makeVertexAttributeDescriptions(key, layout.vertexInputs);
    if (!attributes)
    {
        return std::unexpected {attributes.error()};
    }
    auto specializationId = specializationConstantId(layout, ShaderStage::Fragment, "alphaMode", "mesh pipeline");
    if (!specializationId)
    {
        return std::unexpected {specializationId.error()};
    }
    return detail::createShaderModule(_device, vertexWords).and_then([&](auto vertex) {
        return detail::createShaderModule(_device, fragmentWords).and_then([&](auto fragment) {
            return createMeshPipeline(key, {*vertex, *fragment}, *attributes, *specializationId);
        });
    });
}

auto MeshPipelineCache::createCustomPipeline(const MeshPipelineKey& key) const -> Result<vk::raii::Pipeline>
{
    const auto& camera = key.customProgram->camera;
    const auto attributes =
        key.procedural
            ? Result<std::vector<vk::VertexInputAttributeDescription>> {std::in_place,
                                                                        std::vector<
                                                                            vk::VertexInputAttributeDescription> {}}
            : makeVertexAttributeDescriptions(key, camera.layout.vertexInputs);
    return attributes.and_then([&](auto descriptions) {
        return createShadowShaderModules(_device,
                                         camera.vertex.words,
                                         std::span<const std::uint32_t> {camera.fragment->words})
            .and_then([&](auto modules) {
                return createMeshPipeline(key, {*modules.vertex, **modules.fragment}, descriptions, std::nullopt);
            });
    });
}

auto MeshPipelineCache::createMeshPipeline(const MeshPipelineKey& key,
                                           const std::array<vk::ShaderModule, 2>& modules,
                                           std::span<const vk::VertexInputAttributeDescription> attributes,
                                           std::optional<std::uint32_t> alphaModeSpecializationId) const
    -> std::expected<vk::raii::Pipeline, Error>
{
    const auto bindings = meshVertexInputBindings(key);
    const auto vertexInput = meshVertexInputState(key, bindings, attributes);
    const auto specializationData = FragmentSpecializationData {.alphaMode = static_cast<std::uint32_t>(key.alphaMode)};
    const auto specializationEntry = makeFragmentSpecializationEntry(alphaModeSpecializationId.value_or(0));
    const auto specialization = vk::SpecializationInfo {.mapEntryCount = 1,
                                                        .pMapEntries = &specializationEntry,
                                                        .dataSize = sizeof(specializationData),
                                                        .pData = &specializationData};
    const auto stages = alphaModeSpecializationId ? makeShaderStages(modules.front(), modules.back(), specialization)
                                                  : makeShaderStages(modules.front(), modules.back());
    const auto rendering = makeColorPipelineRendering(_outputCompatibility);
    const auto multisampling = makePipelineMultisampling(_outputCompatibility.sampleCount);
    const auto state =
        GraphicsPipelineState {.stages = stages,
                               .rendering = rendering,
                               .multisampling = multisampling,
                               .rasterization = makeRasterizationState(key.renderFace, key.mirrored),
                               .depthStencil = key.alphaMode == AlphaMode::Blend ? blendDepthStencil : depthStencil,
                               .colorBlend = key.alphaMode == AlphaMode::Blend ? alphaBlend : opaqueBlend,
                               .vertexInput = vertexInput,
                               .layout = key.customProgram && _applicationBindings != nullptr
                                             ? _applicationBindings->getPipelineLayout(key.customProgram)
                                             : _pipelineLayout};
    const auto createInfo = makeGraphicsPipelineCreateInfo(state);
    return checkedCreation(_device.createGraphicsPipeline(nullptr, createInfo));
}

auto MeshPipelineCache::createSkyboxPipeline() -> std::expected<vk::raii::Pipeline, Error>
{
    auto programLayout = skyboxShaderLayout();
    if (!programLayout)
    {
        return std::unexpected {programLayout.error()};
    }
    return detail::createShaderModule(_device, shaders::skyboxVertex).and_then([&](auto vertex) {
        return detail::createShaderModule(_device, shaders::skyboxFragment).and_then([&](auto fragment) {
            return createSkyboxPipelineWithModules(*vertex, *fragment);
        });
    });
}

auto MeshPipelineCache::createSkyboxPipelineWithModules(vk::ShaderModule vertex, vk::ShaderModule fragment) const
    -> std::expected<vk::raii::Pipeline, Error>
{
    const auto stages = makeShaderStages(vertex, fragment);
    const auto rendering = makeColorPipelineRendering(_outputCompatibility);
    const auto multisampling = makePipelineMultisampling(_outputCompatibility.sampleCount);
    static constexpr auto depth = vk::PipelineDepthStencilStateCreateInfo {.depthTestEnable = vk::False,
                                                                           .depthWriteEnable = vk::False,
                                                                           .depthCompareOp = vk::CompareOp::eAlways};
    static constexpr auto rasterization =
        vk::PipelineRasterizationStateCreateInfo {.polygonMode = vk::PolygonMode::eFill,
                                                  .cullMode = vk::CullModeFlagBits::eNone,
                                                  .frontFace = vk::FrontFace::eCounterClockwise,
                                                  .lineWidth = 1.0F};
    static constexpr auto vertexInput = vk::PipelineVertexInputStateCreateInfo {};
    const auto state = GraphicsPipelineState {.stages = stages,
                                              .rendering = rendering,
                                              .multisampling = multisampling,
                                              .rasterization = rasterization,
                                              .depthStencil = depth,
                                              .colorBlend = opaqueBlend,
                                              .vertexInput = vertexInput,
                                              .layout = _pipelineLayout};
    const auto createInfo = makeGraphicsPipelineCreateInfo(state);
    return checkedCreation(_device.createGraphicsPipeline(nullptr, createInfo));
}

auto MeshPipelineCache::createShadowPipeline(const ShadowPipelineKey& key) -> std::expected<vk::raii::Pipeline, Error>
{
    if (key.customProgram)
    {
        return createCustomShadowPipeline(key);
    }
    const auto vertexWords = shadowVertexShaderWords(key.hasColor, key.hasBaseColorTexture, key.instanced);
    const auto fragmentWords = shadowFragmentShaderWords(key.alphaMode == AlphaMode::Mask, key.hasBaseColorTexture);
    auto programLayout = shadowShaderLayout(key);
    if (!programLayout)
    {
        return std::unexpected {programLayout.error()};
    }
    const auto& layout = programLayout->get();
    auto attributes = shadowVertexAttributes(key, layout.vertexInputs);
    if (!attributes)
    {
        return std::unexpected {std::move(attributes.error())};
    }
    const auto id = fragmentWords
                        ? specializationConstantId(layout, ShaderStage::Fragment, "alphaMode", "shadow pipeline")
                        : Result<std::uint32_t> {0U};
    return id.and_then([&](auto specializationId) {
        return createShadowShaderModules(_device, vertexWords, fragmentWords).and_then([&](auto modules) {
            return createShadowPipelineWithModules(key, modules, *attributes, specializationId);
        });
    });
}

auto MeshPipelineCache::createCustomShadowPipeline(const ShadowPipelineKey& key) const -> Result<vk::raii::Pipeline>
{
    const auto* shadow = std::get_if<ProgramVariant>(&key.customProgram->shadow);
    if (shadow == nullptr)
    {
        return std::unexpected {
            makeError(ErrorCode::InvalidArgument, "Custom shadow pipeline has no explicit variant")};
    }
    const auto fragmentWords =
        shadow->fragment ? std::optional<std::span<const std::uint32_t>> {shadow->fragment->words} : std::nullopt;
    return createCustomMeshShadowAttributes(key, shadow->layout.vertexInputs).and_then([&](auto attributes) {
        return createShadowShaderModules(_device, shadow->vertex.words, fragmentWords).and_then([&](auto modules) {
            return createCustomShadowPipelineWithModules(key, attributes, modules);
        });
    });
}

auto MeshPipelineCache::createCustomMeshShadowAttributes(const ShadowPipelineKey& key,
                                                         std::span<const ShaderInterfaceVariable> vertexInputs)
    -> Result<std::vector<vk::VertexInputAttributeDescription>>
{
    if (key.procedural)
    {
        return std::vector<vk::VertexInputAttributeDescription> {};
    }
    return makeVertexAttributeDescriptions(customShadowMeshKey(key), vertexInputs);
}

auto MeshPipelineCache::createCustomShadowPipelineWithModules(
    const ShadowPipelineKey& key,
    std::span<const vk::VertexInputAttributeDescription> attributes,
    const ShadowShaderModules& modules) const -> Result<vk::raii::Pipeline>
{
    const auto [stages, stageCount] = customShadowStages(modules);
    const auto rendering = vk::PipelineRenderingCreateInfo {.depthAttachmentFormat = _outputCompatibility.depthFormat};
    const auto binding = vk::VertexInputBindingDescription {.binding = meshVertexBufferBinding,
                                                            .stride = key.vertexStride,
                                                            .inputRate = vk::VertexInputRate::eVertex};
    const auto bindings = std::array {binding};
    const auto vertexInput =
        key.procedural ? vk::PipelineVertexInputStateCreateInfo {} : makeVertexInputState(bindings, attributes);
    const auto layout =
        _applicationBindings != nullptr ? _applicationBindings->getPipelineLayout(key.customProgram) : _pipelineLayout;
    const auto state = GraphicsPipelineState {.stages = std::span {stages}.first(stageCount),
                                              .rendering = rendering,
                                              .multisampling = multisample,
                                              .rasterization = makeRasterizationState(key.renderFace, key.mirrored),
                                              .depthStencil = depthStencil,
                                              .colorBlend = noColorBlend,
                                              .vertexInput = vertexInput,
                                              .layout = layout};
    const auto createInfo = makeGraphicsPipelineCreateInfo(state);
    return checkedCreation(_device.createGraphicsPipeline(nullptr, createInfo));
}

auto MeshPipelineCache::createShadowPipelineWithModules(const ShadowPipelineKey& key,
                                                        const ShadowShaderModules& modules,
                                                        std::span<const vk::VertexInputAttributeDescription> attributes,
                                                        std::uint32_t specializationId) const
    -> Result<vk::raii::Pipeline>
{
    const auto alphaMode = static_cast<std::uint32_t>(key.alphaMode);
    const auto specializationData = FragmentSpecializationData {.alphaMode = alphaMode};
    const auto specializationEntry = makeFragmentSpecializationEntry(specializationId);
    const auto specialization = vk::SpecializationInfo {.mapEntryCount = 1,
                                                        .pMapEntries = &specializationEntry,
                                                        .dataSize = sizeof(specializationData),
                                                        .pData = &specializationData};
    const auto [stages, stageCount] = makeShadowShaderStages(*modules.vertex, modules.fragment, specialization);
    const auto rendering = vk::PipelineRenderingCreateInfo {.depthAttachmentFormat = _outputCompatibility.depthFormat};
    const auto bindings = std::array {
        vk::VertexInputBindingDescription {.binding = meshVertexBufferBinding,
                                           .stride = key.vertexStride,
                                           .inputRate = vk::VertexInputRate::eVertex  },
        vk::VertexInputBindingDescription {.binding = meshInstanceVertexBufferBinding,
                                           .stride = static_cast<std::uint32_t>(sizeof(MeshInstanceData)),
                                           .inputRate = vk::VertexInputRate::eInstance}
    };
    const auto vertexInput = makeVertexInputState(std::span {bindings}.first(key.instanced ? 2U : 1U), attributes);
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
