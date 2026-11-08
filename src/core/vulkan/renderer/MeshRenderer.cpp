// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include "renderer/MeshRenderer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <glm/ext/matrix_float3x3.hpp>
#include <glm/ext/matrix_float3x4.hpp>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float4.hpp>
#include <glm/geometric.hpp>
#include <glm/matrix.hpp>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <panda/Buffer.hpp>
#include <panda/Error.hpp>
#include <panda/Mesh.hpp>
#include <panda/RenderView.hpp>
#include <panda/Texture.hpp>
#include <span>
#include <utility>
#include <vector>

#include "BuiltinShaders.hpp"
#include "ShaderLocations.inc"
#include "VulkanError.hpp"
#include "presentation/SwapchainGeneration.hpp"
#include "renderer/ShadowView.hpp"
#include "resources/BufferResource.hpp"
#include "resources/MeshResource.hpp"
#include "resources/TextureResource.hpp"

namespace panda::core::vulkan::renderer
{
using detail::checkedCreation;

namespace
{
constexpr auto textureDescriptorsPerDraw = std::uint32_t {3};

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
static_assert(sizeof(MaterialRecord) == 48);
static_assert(offsetof(MaterialRecord, alphaCutoff) == 16);
static_assert(offsetof(MaterialRecord, hasBaseColorTexture) == 20);
static_assert(offsetof(MaterialRecord, hasMetallicRoughnessTexture) == 24);
static_assert(offsetof(MaterialRecord, hasEmissiveTexture) == 28);
static_assert(offsetof(MaterialRecord, emissive) == 32);
static_assert(static_cast<std::uint32_t>(VertexAttributeSemantic::Position) ==
              PANDA_VERTEX_ATTRIBUTE_POSITION_LOCATION);
static_assert(static_cast<std::uint32_t>(VertexAttributeSemantic::Normal) == PANDA_VERTEX_ATTRIBUTE_NORMAL_LOCATION);
static_assert(static_cast<std::uint32_t>(VertexAttributeSemantic::TexCoord0) ==
              PANDA_VERTEX_ATTRIBUTE_TEX_COORD_0_LOCATION);
static_assert(static_cast<std::uint32_t>(VertexAttributeSemantic::Tangent) == PANDA_VERTEX_ATTRIBUTE_TANGENT_LOCATION);
static_assert(static_cast<std::uint32_t>(VertexAttributeSemantic::Color) == PANDA_VERTEX_ATTRIBUTE_COLOR_LOCATION);

auto isSrgb(vk::Format format) -> bool
{
    return format == vk::Format::eB8G8R8A8Srgb || format == vk::Format::eR8G8B8A8Srgb;
}

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
constexpr auto shadowExtent = vk::Extent2D {.width = 2048, .height = 2048};
constexpr auto dynamic = vk::PipelineDynamicStateCreateInfo {
    .dynamicStateCount = static_cast<std::uint32_t>(dynamicStates.size()), .pDynamicStates = dynamicStates.data()};

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

auto makeFrameUniform(const RenderView& view) -> std::expected<FrameUniform, Error>
{
    const auto lightViewProjection = createDirectionalShadowView(view.camera, view.directionalLight);
    if (!lightViewProjection)
    {
        return std::unexpected {lightViewProjection.error()};
    }
    const auto direction = glm::normalize(view.directionalLight.direction);
    return FrameUniform {
        .viewProjection = view.camera.viewProjection,
        .lightViewProjection = *lightViewProjection,
        .cameraPosition = glm::vec4 {view.camera.position, 1.0F},
        .lightDirection = glm::vec4 {direction, 0.0F},
        .lightColorIntensity = glm::vec4 {view.directionalLight.color, view.directionalLight.intensity},
        .ambient = glm::vec4 {1.0F, 1.0F, 1.0F, 0.06F}
    };
}

auto makeDrawConstants(const MeshDraw& draw) -> DrawConstants
{
    const auto normalMatrix = glm::transpose(glm::inverse(glm::mat3 {draw.modelMatrix}));
    const auto constants = DrawConstants {
        .modelMatrix = draw.modelMatrix,
        .normalColumns = {glm::vec4 {normalMatrix[0], 0.0F},
                          glm::vec4 {normalMatrix[1], 0.0F},
                          glm::vec4 {normalMatrix[2], 0.0F}},
        .tint = draw.tint * draw.material.baseColor
    };
    return constants;
}

auto makeMaterialRecord(const Material& material) -> MaterialRecord
{
    return MaterialRecord {
        .metallic = std::clamp(material.metallic, 0.0F, 1.0F),
        .roughness = std::clamp(material.roughness, 0.0F, 1.0F),
        .isLit = material.model == MaterialModel::Lit ? 1U : 0U,
        .renderFace = static_cast<std::uint32_t>(material.renderFace),
        .alphaCutoff = material.alphaCutoff,
        .hasBaseColorTexture = material.baseColorTexture.isValid() ? 1U : 0U,
        .hasMetallicRoughnessTexture = material.metallicRoughnessTexture.isValid() ? 1U : 0U,
        .hasEmissiveTexture = material.emissiveTexture.isValid() ? 1U : 0U,
        .emissive = glm::vec4 {material.emissive, 0.0F}
    };
}

auto cameraDepthForMesh(const glm::mat4& viewProjection, const glm::mat4& modelMatrix) -> float
{
    const auto meshOrigin = glm::vec4 {0.0F, 0.0F, 0.0F, 1.0F};
    const auto clipPosition = viewProjection * modelMatrix * meshOrigin;
    if (!std::isfinite(clipPosition.z) || !std::isfinite(clipPosition.w) || clipPosition.w <= 0.0F)
    {
        return std::numeric_limits<float>::infinity();
    }
    const auto depth = clipPosition.z / clipPosition.w;
    return std::isfinite(depth) ? depth : std::numeric_limits<float>::infinity();
}

auto createDepthFormatSupport(vk::PhysicalDevice physicalDevice, vk::Format format) -> std::expected<bool, Error>
{
    constexpr auto usage = vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled;
    const auto support =
        physicalDevice.getImageFormatProperties(format, vk::ImageType::e2D, vk::ImageTiling::eOptimal, usage, {});
    if (support.result == vk::Result::eErrorFormatNotSupported)
    {
        return false;
    }
    if (support.result != vk::Result::eSuccess)
    {
        return std::unexpected {makeVulkanError(support.result)};
    }
    return (support.value.sampleCounts & vk::SampleCountFlagBits::e1) != vk::SampleCountFlags {} &&
           support.value.maxExtent.width >= shadowExtent.width && support.value.maxExtent.height >= shadowExtent.height;
}

auto querySampleCounts(vk::PhysicalDevice physicalDevice,
                       vk::Format format,
                       vk::ImageUsageFlags usage,
                       const char* unsupportedMessage) -> std::expected<vk::SampleCountFlags, Error>
{
    const auto support =
        physicalDevice.getImageFormatProperties(format, vk::ImageType::e2D, vk::ImageTiling::eOptimal, usage, {});
    if (support.result == vk::Result::eErrorFormatNotSupported)
    {
        return std::unexpected {makeError(ErrorCode::Unsupported, unsupportedMessage)};
    }
    if (support.result != vk::Result::eSuccess)
    {
        return std::unexpected {makeVulkanError(support.result)};
    }
    return support.value.sampleCounts;
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
    std::array<vk::VertexInputAttributeDescription, 3> attributes;
    std::uint32_t attributeCount {};
};

struct ShadowVertexInputKey
{
    std::uint32_t vertexStride;
    std::uint32_t positionOffset;
    std::uint32_t texCoordOffset;
    std::uint32_t colorOffset;
    bool hasColor;
    bool hasBaseColorTexture;
};

auto makeShadowVertexInput(const ShadowVertexInputKey& key) -> ShadowVertexInput
{
    auto result = ShadowVertexInput {
        .binding = {.binding = 0, .stride = key.vertexStride, .inputRate = vk::VertexInputRate::eVertex},
        .attributes = {},
        .attributeCount = 0
    };
    result.attributes[result.attributeCount++] = {.location = PANDA_VERTEX_ATTRIBUTE_POSITION_LOCATION,
                                                  .binding = 0,
                                                  .format = vk::Format::eR32G32B32Sfloat,
                                                  .offset = key.positionOffset};
    if (key.hasBaseColorTexture)
    {
        result.attributes[result.attributeCount++] = {.location = PANDA_VERTEX_ATTRIBUTE_TEX_COORD_0_LOCATION,
                                                      .binding = 0,
                                                      .format = vk::Format::eR32G32Sfloat,
                                                      .offset = key.texCoordOffset};
    }
    if (key.hasColor)
    {
        result.attributes[result.attributeCount++] = {.location = PANDA_VERTEX_ATTRIBUTE_COLOR_LOCATION,
                                                      .binding = 0,
                                                      .format = vk::Format::eR32G32B32A32Sfloat,
                                                      .offset = key.colorOffset};
    }
    return result;
}

auto meshVertexShaderWords(bool lit, bool hasColor, bool hasTextureCoordinates) -> std::span<const std::uint32_t>
{
    constexpr auto variants = std::array {
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

struct TextureDescriptorResources
{
    vk::raii::DescriptorPool pool {nullptr};
    std::vector<vk::raii::DescriptorSet> sets;
};

auto makeTextureDescriptorInfo(const Texture& texture, vk::Sampler sampler, vk::ImageView fallbackView)
    -> vk::DescriptorImageInfo
{
    if (!texture.isValid())
    {
        return {.sampler = sampler, .imageView = fallbackView, .imageLayout = vk::ImageLayout::eDepthReadOnlyOptimal};
    }
    return {.sampler = sampler,
            .imageView = ::panda::detail::TextureResource::getResource(texture)->getNativeView(),
            .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal};
}

auto createTextureDescriptorResources(const vk::raii::Device& device,
                                      vk::DescriptorSetLayout layout,
                                      std::size_t drawCount) -> std::expected<TextureDescriptorResources, Error>
{
    if (drawCount > std::numeric_limits<std::uint32_t>::max() / textureDescriptorsPerDraw)
    {
        return std::unexpected {
            makeError(ErrorCode::ResourceExhausted, "Material texture descriptor count exceeds Vulkan limits")};
    }
    const auto setCount = static_cast<std::uint32_t>(drawCount);
    const auto poolSize = vk::DescriptorPoolSize {.type = vk::DescriptorType::eCombinedImageSampler,
                                                  .descriptorCount = setCount * textureDescriptorsPerDraw};
    auto pool = checkedCreation(device.createDescriptorPool(
        vk::DescriptorPoolCreateInfo {.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
                                      .maxSets = setCount,
                                      .poolSizeCount = 1,
                                      .pPoolSizes = &poolSize}));
    if (!pool)
    {
        return std::unexpected {pool.error()};
    }
    const auto layouts = std::vector(setCount, layout);
    const auto allocation = vk::DescriptorSetAllocateInfo {.descriptorPool = **pool,
                                                           .descriptorSetCount = setCount,
                                                           .pSetLayouts = layouts.data()};
    auto sets = device.allocateDescriptorSets(allocation);
    if (sets.result != vk::Result::eSuccess)
    {
        return std::unexpected {makeVulkanError(sets.result)};
    }
    return TextureDescriptorResources {.pool = std::move(*pool), .sets = std::move(sets.value)};
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
        vk::SpecializationMapEntry {.constantID = 0,
                                    .offset = offsetof(FragmentSpecializationData, outputIsSrgb),
                                    .size = sizeof(std::uint32_t)},
        vk::SpecializationMapEntry {.constantID = 1,
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
    static constexpr auto entry =
        vk::SpecializationMapEntry {.constantID = 0, .offset = 0, .size = sizeof(std::uint32_t)};
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
    static constexpr auto entry =
        vk::SpecializationMapEntry {.constantID = 0, .offset = 0, .size = sizeof(std::uint32_t)};
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

auto makeVertexInputState(const vk::VertexInputBindingDescription& binding,
                          std::span<const vk::VertexInputAttributeDescription> attributes)
    -> vk::PipelineVertexInputStateCreateInfo
{
    return {.vertexBindingDescriptionCount = 1,
            .pVertexBindingDescriptions = &binding,
            .vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size()),
            .pVertexAttributeDescriptions = attributes.data()};
}

}

MeshRenderer::MeshRenderer(const MeshRendererCreateInfo& info)
    : _physicalDevice {info.physicalDevice},
      _device {info.device},
      _pool {info.pool},
      _frames(info.frameCount)
{
}

MeshRenderer::~MeshRenderer() = default;

auto MeshRenderer::create(const MeshRendererCreateInfo& info) -> std::expected<std::unique_ptr<MeshRenderer>, Error>
{
    if (info.frameCount == 0 || info.frameCount > std::numeric_limits<std::uint32_t>::max() || info.extent.width == 0 ||
        info.extent.height == 0 || info.colorFormat == vk::Format::eUndefined)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Invalid mesh renderer creation parameters")};
    }
    auto renderer = std::unique_ptr<MeshRenderer> {new MeshRenderer {info}};
    if (const auto initialized = renderer->initialize(info.extent, info.colorFormat); !initialized)
    {
        return std::unexpected {initialized.error()};
    }
    return renderer;
}

auto MeshRenderer::initialize(vk::Extent2D extent, vk::Format colorFormat) -> std::expected<void, Error>
{
    if (_physicalDevice.getProperties().limits.maxPushConstantsSize < sizeof(DrawConstants))
    {
        return std::unexpected {
            makeError(ErrorCode::Unsupported, "Device push-constant limit is below renderer needs")};
    }
    if (_physicalDevice.getProperties().limits.maxStorageBufferRange < sizeof(MaterialRecord))
    {
        return std::unexpected {
            makeError(ErrorCode::Unsupported, "Device storage-buffer limit is below renderer needs")};
    }
    return initializeDescriptorState()
        .and_then([this, extent, colorFormat] {
            return initializeDepthAndPipeline(extent, colorFormat);
        })
        .and_then([this] {
            return initializeFrameBuffers();
        });
}

auto MeshRenderer::initializeDescriptorState() -> std::expected<void, Error>
{
    if (const auto descriptorSetLayout = initializeDescriptorSetLayout(); !descriptorSetLayout)
    {
        return descriptorSetLayout;
    }
    if (const auto textureDescriptorSetLayout = initializeTextureDescriptorSetLayout(); !textureDescriptorSetLayout)
    {
        return textureDescriptorSetLayout;
    }
    if (const auto pipelineLayout = initializePipelineLayout(); !pipelineLayout)
    {
        return pipelineLayout;
    }
    return createDescriptorPool();
}

auto MeshRenderer::initializeDescriptorSetLayout() -> std::expected<void, Error>
{
    static constexpr auto bindings = std::array {
        vk::DescriptorSetLayoutBinding {
                                        .binding = 0,
                                        .descriptorType = vk::DescriptorType::eUniformBuffer,
                                        .descriptorCount = 1,
                                        .stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment},
        vk::DescriptorSetLayoutBinding {.binding = 1,
                                        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                        .descriptorCount = 1,
                                        .stageFlags = vk::ShaderStageFlagBits::eFragment                                   },
        vk::DescriptorSetLayoutBinding {.binding = 2,
                                        .descriptorType = vk::DescriptorType::eStorageBuffer,
                                        .descriptorCount = 1,
                                        .stageFlags = vk::ShaderStageFlagBits::eFragment                                   }
    };
    const auto setLayout = (*_device).createDescriptorSetLayout(
        vk::DescriptorSetLayoutCreateInfo {.bindingCount = static_cast<std::uint32_t>(bindings.size()),
                                           .pBindings = bindings.data()},
        nullptr,
        *_device.getDispatcher());
    if (setLayout.result != vk::Result::eSuccess)
    {
        return std::unexpected {makeVulkanError(setLayout.result)};
    }
    _descriptorSetLayout =
        vk::raii::DescriptorSetLayout {_device, static_cast<vk::DescriptorSetLayout::CType>(setLayout.value)};
    return {};
}

auto MeshRenderer::initializeTextureDescriptorSetLayout() -> std::expected<void, Error>
{
    static constexpr auto bindings = std::array {
        vk::DescriptorSetLayoutBinding {.binding = 0,
                                        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                        .descriptorCount = 1,
                                        .stageFlags = vk::ShaderStageFlagBits::eFragment},
        vk::DescriptorSetLayoutBinding {.binding = 1,
                                        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                        .descriptorCount = 1,
                                        .stageFlags = vk::ShaderStageFlagBits::eFragment},
        vk::DescriptorSetLayoutBinding {.binding = 2,
                                        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                        .descriptorCount = 1,
                                        .stageFlags = vk::ShaderStageFlagBits::eFragment}
    };
    const auto setLayout = (*_device).createDescriptorSetLayout(
        vk::DescriptorSetLayoutCreateInfo {.bindingCount = static_cast<std::uint32_t>(bindings.size()),
                                           .pBindings = bindings.data()},
        nullptr,
        *_device.getDispatcher());
    if (setLayout.result != vk::Result::eSuccess)
    {
        return std::unexpected {makeVulkanError(setLayout.result)};
    }
    _textureDescriptorSetLayout =
        vk::raii::DescriptorSetLayout {_device, static_cast<vk::DescriptorSetLayout::CType>(setLayout.value)};
    return {};
}

auto MeshRenderer::initializePipelineLayout() -> std::expected<void, Error>
{
    static constexpr auto pushRange = vk::PushConstantRange {.stageFlags = vk::ShaderStageFlagBits::eVertex,
                                                             .offset = 0,
                                                             .size = sizeof(DrawConstants)};
    const auto descriptorSetLayouts = std::array {*_descriptorSetLayout, *_textureDescriptorSetLayout};
    const auto layoutInfo =
        vk::PipelineLayoutCreateInfo {.setLayoutCount = static_cast<std::uint32_t>(descriptorSetLayouts.size()),
                                      .pSetLayouts = descriptorSetLayouts.data(),
                                      .pushConstantRangeCount = 1,
                                      .pPushConstantRanges = &pushRange};
    auto pipelineLayout = checkedCreation(_device.createPipelineLayout(layoutInfo));
    if (!pipelineLayout)
    {
        return std::unexpected {pipelineLayout.error()};
    }
    _pipelineLayout = std::move(*pipelineLayout);
    return {};
}

auto MeshRenderer::createDescriptorPool() -> std::expected<void, Error>
{
    const auto setCount = static_cast<std::uint32_t>(_frames.size());
    const auto poolSizes = std::array {
        vk::DescriptorPoolSize {.type = vk::DescriptorType::eUniformBuffer,        .descriptorCount = setCount},
        vk::DescriptorPoolSize {.type = vk::DescriptorType::eCombinedImageSampler, .descriptorCount = setCount},
        vk::DescriptorPoolSize {.type = vk::DescriptorType::eStorageBuffer,        .descriptorCount = setCount}
    };
    auto pool = checkedCreation(_device.createDescriptorPool(
        vk::DescriptorPoolCreateInfo {.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
                                      .maxSets = setCount,
                                      .poolSizeCount = static_cast<std::uint32_t>(poolSizes.size()),
                                      .pPoolSizes = poolSizes.data()}));
    if (!pool)
    {
        return std::unexpected {pool.error()};
    }
    _descriptorPool = std::move(*pool);
    return allocateDescriptorSets();
}

auto MeshRenderer::allocateDescriptorSets() -> std::expected<void, Error>
{
    const auto count = static_cast<std::uint32_t>(_frames.size());
    const auto layouts = std::vector(count, *_descriptorSetLayout);
    const auto allocation = vk::DescriptorSetAllocateInfo {.descriptorPool = *_descriptorPool,
                                                           .descriptorSetCount = count,
                                                           .pSetLayouts = layouts.data()};
    auto sets = _device.allocateDescriptorSets(allocation);
    if (sets.result != vk::Result::eSuccess)
    {
        return std::unexpected {makeVulkanError(sets.result)};
    }
    for (auto index = std::size_t {}; index < _frames.size(); ++index)
    {
        _frames[index].descriptorSet = std::move(sets.value.at(index));
    }
    return {};
}

auto MeshRenderer::initializeFrameBuffers() -> std::expected<void, Error>
{
    for (auto& frame : _frames)
    {
        static constexpr auto description = BufferDesc {.size = sizeof(FrameUniform),
                                                        .usage = vk::BufferUsageFlagBits::eUniformBuffer,
                                                        .memory = BufferMemory::Upload};
        auto buffer = ::panda::detail::BufferResource::create(_device, _pool, description);
        if (!buffer)
        {
            return std::unexpected {buffer.error()};
        }
        frame.uniformBuffer = std::move(*buffer);
        static constexpr auto materialDescription = BufferDesc {.size = sizeof(MaterialRecord),
                                                                .usage = vk::BufferUsageFlagBits::eStorageBuffer,
                                                                .memory = BufferMemory::Upload};
        auto materialBuffer = ::panda::detail::BufferResource::create(_device, _pool, materialDescription);
        if (!materialBuffer)
        {
            return std::unexpected {materialBuffer.error()};
        }
        frame.materialBuffer = std::move(*materialBuffer);
        frame.materialBufferCapacity = sizeof(MaterialRecord);
        updateDescriptorSet(frame);
    }
    return {};
}

void MeshRenderer::updateDescriptorSet(const FrameResources& frame)
{
    const auto bufferInfo = vk::DescriptorBufferInfo {.buffer = frame.uniformBuffer->getNative(),
                                                      .offset = 0,
                                                      .range = sizeof(FrameUniform)};
    const auto imageInfo = vk::DescriptorImageInfo {.sampler = *_shadowSampler,
                                                    .imageView = frame.shadowMap->getNativeView(),
                                                    .imageLayout = vk::ImageLayout::eDepthReadOnlyOptimal};
    const auto materialInfo = vk::DescriptorBufferInfo {.buffer = frame.materialBuffer->getNative(),
                                                        .offset = 0,
                                                        .range = frame.materialBufferCapacity};
    const auto writes = std::array {
        vk::WriteDescriptorSet {.dstSet = *frame.descriptorSet,
                                .dstBinding = 0,
                                .descriptorCount = 1,
                                .descriptorType = vk::DescriptorType::eUniformBuffer,
                                .pBufferInfo = &bufferInfo  },
        vk::WriteDescriptorSet {.dstSet = *frame.descriptorSet,
                                .dstBinding = 1,
                                .descriptorCount = 1,
                                .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                .pImageInfo = &imageInfo    },
        vk::WriteDescriptorSet {.dstSet = *frame.descriptorSet,
                                .dstBinding = 2,
                                .descriptorCount = 1,
                                .descriptorType = vk::DescriptorType::eStorageBuffer,
                                .pBufferInfo = &materialInfo}
    };
    _device.updateDescriptorSets(writes, {});
}

auto MeshRenderer::initializeDepthAndPipeline(vk::Extent2D extent, vk::Format colorFormat) -> std::expected<void, Error>
{
    const auto selectedDepthFormat = chooseDepthFormat();
    if (!selectedDepthFormat)
    {
        return std::unexpected {selectedDepthFormat.error()};
    }
    _depthFormat = *selectedDepthFormat;
    if (const auto shadowResources = initializeShadowResources(); !shadowResources)
    {
        return shadowResources;
    }
    return resize(extent, colorFormat);
}

auto MeshRenderer::initializeShadowResources() -> std::expected<void, Error>
{
    if (const auto maps = initializeShadowMaps(); !maps)
    {
        return maps;
    }
    if (const auto sampler = initializeShadowSampler(); !sampler)
    {
        return sampler;
    }
    return initializeTextureSampler();
}

auto MeshRenderer::initializeShadowMaps() -> std::expected<void, Error>
{
    auto shadowMaps = createShadowMaps();
    if (!shadowMaps)
    {
        return std::unexpected {shadowMaps.error()};
    }
    for (auto index = std::size_t {}; index < _frames.size(); ++index)
    {
        _frames[index].shadowMap = std::move((*shadowMaps)[index]);
    }
    return {};
}

auto MeshRenderer::initializeShadowSampler() -> std::expected<void, Error>
{
    auto sampler = static_cast<vk::Device>(_device).createSampler(
        vk::SamplerCreateInfo {.magFilter = vk::Filter::eNearest,
                               .minFilter = vk::Filter::eNearest,
                               .mipmapMode = vk::SamplerMipmapMode::eNearest,
                               .addressModeU = vk::SamplerAddressMode::eClampToEdge,
                               .addressModeV = vk::SamplerAddressMode::eClampToEdge,
                               .addressModeW = vk::SamplerAddressMode::eClampToEdge,
                               .maxAnisotropy = 1.0F,
                               .minLod = 0.0F,
                               .maxLod = 0.0F,
                               .borderColor = vk::BorderColor::eFloatOpaqueWhite});
    if (sampler.result != vk::Result::eSuccess)
    {
        return std::unexpected {::panda::makeVulkanError(sampler.result)};
    }
    _shadowSampler = vk::raii::Sampler {_device, sampler.value};
    return {};
}

auto MeshRenderer::initializeTextureSampler() -> std::expected<void, Error>
{
    auto sampler = static_cast<vk::Device>(_device).createSampler(
        vk::SamplerCreateInfo {.magFilter = vk::Filter::eLinear,
                               .minFilter = vk::Filter::eLinear,
                               .mipmapMode = vk::SamplerMipmapMode::eNearest,
                               .addressModeU = vk::SamplerAddressMode::eRepeat,
                               .addressModeV = vk::SamplerAddressMode::eRepeat,
                               .addressModeW = vk::SamplerAddressMode::eRepeat,
                               .maxAnisotropy = 1.0F,
                               .minLod = 0.0F,
                               .maxLod = 0.0F});
    if (sampler.result != vk::Result::eSuccess)
    {
        return std::unexpected {::panda::makeVulkanError(sampler.result)};
    }
    _textureSampler = vk::raii::Sampler {_device, sampler.value};
    return {};
}

auto MeshRenderer::createDepthAttachments(vk::Extent2D extent, vk::SampleCountFlagBits samples)
    -> std::expected<std::vector<std::shared_ptr<::panda::detail::TextureResource>>, Error>
{
    auto attachments = std::vector<std::shared_ptr<::panda::detail::TextureResource>> {};
    attachments.reserve(_frames.size());
    for (auto index = std::size_t {}; index < _frames.size(); ++index)
    {
        auto attachment = ::panda::detail::TextureResource::create(
            _device,
            _pool,
            {.extent = extent, .format = _depthFormat, .usage = vk::ImageUsageFlagBits::eDepthStencilAttachment},
            samples);
        if (!attachment)
        {
            return std::unexpected {attachment.error()};
        }
        attachments.push_back(std::move(*attachment));
    }
    return attachments;
}

auto MeshRenderer::createColorAttachments(vk::Extent2D extent, vk::Format format, vk::SampleCountFlagBits samples)
    -> std::expected<std::vector<std::shared_ptr<::panda::detail::TextureResource>>, Error>
{
    auto attachments = std::vector<std::shared_ptr<::panda::detail::TextureResource>> {};
    if (samples == vk::SampleCountFlagBits::e1)
    {
        return attachments;
    }
    attachments.reserve(_frames.size());
    for (auto index = std::size_t {}; index < _frames.size(); ++index)
    {
        auto attachment = ::panda::detail::TextureResource::create(
            _device,
            _pool,
            {.extent = extent, .format = format, .usage = vk::ImageUsageFlagBits::eColorAttachment},
            samples);
        if (!attachment)
        {
            return std::unexpected {attachment.error()};
        }
        attachments.push_back(std::move(*attachment));
    }
    return attachments;
}

auto MeshRenderer::createShadowMaps()
    -> std::expected<std::vector<std::shared_ptr<::panda::detail::TextureResource>>, Error>
{
    auto maps = std::vector<std::shared_ptr<::panda::detail::TextureResource>> {};
    maps.reserve(_frames.size());
    for (auto index = std::size_t {}; index < _frames.size(); ++index)
    {
        auto map = ::panda::detail::TextureResource::create(
            _device,
            _pool,
            {.extent = shadowExtent,
             .format = _depthFormat,
             .usage = vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled});
        if (!map)
        {
            return std::unexpected {map.error()};
        }
        maps.push_back(std::move(*map));
    }
    return maps;
}

auto MeshRenderer::chooseDepthFormat() const -> std::expected<vk::Format, Error>
{
    for (const auto format : {vk::Format::eD32Sfloat, vk::Format::eD16Unorm})
    {
        const auto supported = createDepthFormatSupport(_physicalDevice, format);
        if (!supported)
        {
            return std::unexpected {supported.error()};
        }
        if (*supported)
        {
            return format;
        }
    }
    return std::unexpected {makeError(ErrorCode::Unsupported, "Device has no supported depth attachment format")};
}

auto MeshRenderer::chooseSampleCount(vk::Format colorFormat) const -> std::expected<vk::SampleCountFlagBits, Error>
{
    const auto colorSamples = querySampleCounts(_physicalDevice,
                                                colorFormat,
                                                vk::ImageUsageFlagBits::eColorAttachment,
                                                "Swapchain format cannot be multisampled");
    if (!colorSamples)
    {
        return std::unexpected {colorSamples.error()};
    }
    const auto depthSamples = querySampleCounts(_physicalDevice,
                                                _depthFormat,
                                                vk::ImageUsageFlagBits::eDepthStencilAttachment,
                                                "Depth format cannot be multisampled");
    if (!depthSamples)
    {
        return std::unexpected {depthSamples.error()};
    }
    const auto limits = _physicalDevice.getProperties().limits;
    const auto framebufferSamples = limits.framebufferColorSampleCounts & limits.framebufferDepthSampleCounts;
    const auto supportedSamples = *colorSamples & *depthSamples & framebufferSamples;
    for (const auto samples : {vk::SampleCountFlagBits::e4, vk::SampleCountFlagBits::e2, vk::SampleCountFlagBits::e1})
    {
        if ((supportedSamples & samples) != vk::SampleCountFlags {})
        {
            return samples;
        }
    }
    return std::unexpected {makeError(ErrorCode::Unsupported, "No common color and depth sample count is supported")};
}

auto MeshRenderer::resize(vk::Extent2D extent, vk::Format colorFormat) -> std::expected<void, Error>
{
    if (extent.width == 0 || extent.height == 0 || colorFormat == vk::Format::eUndefined)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Invalid renderer output extent or format")};
    }
    if (extent.width == _extent.width && extent.height == _extent.height && colorFormat == _colorFormat)
    {
        return {};
    }
    const auto extentChanged = extent.width != _extent.width || extent.height != _extent.height;
    const auto formatChanged = colorFormat != _colorFormat;
    const auto sampleCount = chooseSampleCount(colorFormat);
    if (!sampleCount)
    {
        return std::unexpected {sampleCount.error()};
    }
    const auto request = ResizeRequest {.extent = extent,
                                        .colorFormat = colorFormat,
                                        .sampleCount = *sampleCount,
                                        .extentChanged = extentChanged,
                                        .formatChanged = formatChanged,
                                        .sampleCountChanged = *sampleCount != _sampleCount};
    auto resources = createResizeResources(request);
    if (!resources)
    {
        return std::unexpected {resources.error()};
    }
    publishResize(request, std::move(*resources));
    return {};
}

auto MeshRenderer::createResizeResources(const ResizeRequest& request) -> std::expected<ResizeResources, Error>
{
    auto depthAttachments = createDepthAttachmentsForResize(request);
    if (!depthAttachments)
    {
        return std::unexpected {depthAttachments.error()};
    }
    auto colorAttachments = createColorAttachmentsForResize(request);
    if (!colorAttachments)
    {
        return std::unexpected {colorAttachments.error()};
    }
    return ResizeResources {.colorAttachments = std::move(*colorAttachments),
                            .depthAttachments = std::move(*depthAttachments)};
}

auto MeshRenderer::createDepthAttachmentsForResize(const ResizeRequest& request)
    -> std::expected<std::vector<std::shared_ptr<::panda::detail::TextureResource>>, Error>
{
    if (request.extentChanged || request.sampleCountChanged)
    {
        return createDepthAttachments(request.extent, request.sampleCount);
    }
    return std::vector<std::shared_ptr<::panda::detail::TextureResource>> {};
}

auto MeshRenderer::createColorAttachmentsForResize(const ResizeRequest& request)
    -> std::expected<std::vector<std::shared_ptr<::panda::detail::TextureResource>>, Error>
{
    if (request.extentChanged || request.formatChanged || request.sampleCountChanged)
    {
        return createColorAttachments(request.extent, request.colorFormat, request.sampleCount);
    }
    return std::vector<std::shared_ptr<::panda::detail::TextureResource>> {};
}

void MeshRenderer::publishResize(const ResizeRequest& request, ResizeResources resources)
{
    if (request.extentChanged || request.sampleCountChanged)
    {
        publishDepthAttachments(std::move(resources.depthAttachments));
    }
    if (request.extentChanged || request.formatChanged || request.sampleCountChanged)
    {
        publishColorAttachments(std::move(resources.colorAttachments));
    }
    if (request.formatChanged || request.sampleCountChanged)
    {
        _pipelines.clear();
    }
    _colorFormat = request.colorFormat;
    _sampleCount = request.sampleCount;
    _extent = request.extent;
}

void MeshRenderer::publishDepthAttachments(std::vector<std::shared_ptr<::panda::detail::TextureResource>> attachments)
{
    for (auto index = std::size_t {}; index < _frames.size(); ++index)
    {
        _frames[index].depthAttachment = std::move(attachments[index]);
        _frames[index].depthInitialized = false;
    }
}

void MeshRenderer::publishColorAttachments(std::vector<std::shared_ptr<::panda::detail::TextureResource>> attachments)
{
    for (auto index = std::size_t {}; index < _frames.size(); ++index)
    {
        _frames[index].colorAttachment = attachments.empty() ? nullptr : std::move(attachments[index]);
        _frames[index].colorInitialized = false;
    }
}

auto MeshRenderer::getMeshPipelineKey(const MeshDraw& draw) -> MeshPipelineKey
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

auto MeshRenderer::getShadowPipelineKey(const MeshDraw& draw) -> ShadowPipelineKey
{
    const auto attributes = draw.mesh.getVertexAttributes();
    const auto position = std::ranges::find(attributes, VertexAttributeSemantic::Position, &VertexAttribute::semantic);
    const auto color = std::ranges::find(attributes, VertexAttributeSemantic::Color, &VertexAttribute::semantic);
    const auto hasColor = draw.material.alphaMode == AlphaMode::Mask && color != attributes.end();
    const auto hasBaseColorTexture =
        draw.material.alphaMode == AlphaMode::Mask && draw.material.baseColorTexture.isValid();
    return {
        .vertexStride = draw.mesh.getVertexStride(),
        .positionOffset = position->offset,
        .texCoordOffset =
            hasBaseColorTexture
                ? std::ranges::find(attributes, VertexAttributeSemantic::TexCoord0, &VertexAttribute::semantic)->offset
                : 0U,
        .colorOffset = hasColor ? color->offset : 0U,
        .hasColor = hasColor,
        .hasBaseColorTexture = hasBaseColorTexture,
        .renderFace = draw.material.renderFace,
        .alphaMode = draw.material.alphaMode,
        .mirrored = glm::determinant(glm::mat3 {draw.modelMatrix}) < 0.0F};
}

auto MeshRenderer::makeVertexAttributeDescriptions(const MeshPipelineKey& key)
    -> std::vector<vk::VertexInputAttributeDescription>
{
    auto attributes = std::vector {
        vk::VertexInputAttributeDescription {.location = PANDA_VERTEX_ATTRIBUTE_POSITION_LOCATION,
                                             .binding = 0,
                                             .format = vk::Format::eR32G32B32Sfloat,
                                             .offset = key.positionOffset}
    };
    if (key.lit)
    {
        attributes.push_back({.location = PANDA_VERTEX_ATTRIBUTE_NORMAL_LOCATION,
                              .binding = 0,
                              .format = vk::Format::eR32G32B32Sfloat,
                              .offset = key.normalOffset});
    }
    if (key.hasTextureCoordinates)
    {
        attributes.push_back({.location = PANDA_VERTEX_ATTRIBUTE_TEX_COORD_0_LOCATION,
                              .binding = 0,
                              .format = vk::Format::eR32G32Sfloat,
                              .offset = key.texCoordOffset});
    }
    if (key.hasColor)
    {
        attributes.push_back({.location = PANDA_VERTEX_ATTRIBUTE_COLOR_LOCATION,
                              .binding = 0,
                              .format = vk::Format::eR32G32B32A32Sfloat,
                              .offset = key.colorOffset});
    }
    return attributes;
}

auto MeshRenderer::createPipeline(const MeshPipelineKey& key) const -> std::expected<vk::raii::Pipeline, Error>
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
    return createGraphicsPipeline(**vertex, **fragment, key);
}

auto MeshRenderer::createGraphicsPipeline(vk::ShaderModule vertex,
                                          vk::ShaderModule fragment,
                                          const MeshPipelineKey& key) const -> std::expected<vk::raii::Pipeline, Error>
{
    const auto binding = vk::VertexInputBindingDescription {.binding = 0,
                                                            .stride = key.vertexStride,
                                                            .inputRate = vk::VertexInputRate::eVertex};
    const auto attributes = makeVertexAttributeDescriptions(key);
    const auto vertexInput = makeVertexInputState(binding, attributes);
    const auto outputIsSrgb = static_cast<std::uint32_t>(isSrgb(_colorFormat));
    const auto specializationData = FragmentSpecializationData {.outputIsSrgb = outputIsSrgb,
                                                                .alphaMode = static_cast<std::uint32_t>(key.alphaMode)};
    const auto specialization = makeMeshFragmentSpecialization(key.alphaMode, specializationData);
    const auto stages = makeShaderStages(vertex, fragment, specialization);
    const auto colorOutput = _colorFormat;
    const auto rendering = vk::PipelineRenderingCreateInfo {.colorAttachmentCount = 1,
                                                            .pColorAttachmentFormats = &colorOutput,
                                                            .depthAttachmentFormat = _depthFormat};
    const auto multisampling = vk::PipelineMultisampleStateCreateInfo {.rasterizationSamples = _sampleCount};
    const auto state =
        GraphicsPipelineState {.stages = stages,
                               .rendering = rendering,
                               .multisampling = multisampling,
                               .rasterization = makeRasterizationState(key.renderFace, key.mirrored),
                               .depthStencil = key.alphaMode == AlphaMode::Blend ? blendDepthStencil : depthStencil,
                               .colorBlend = key.alphaMode == AlphaMode::Blend ? alphaBlend : opaqueBlend,
                               .vertexInput = vertexInput,
                               .layout = *_pipelineLayout};
    const auto createInfo = makeGraphicsPipelineCreateInfo(state);
    return checkedCreation(_device.createGraphicsPipeline(nullptr, createInfo));
}

auto MeshRenderer::createShadowPipeline(const ShadowPipelineKey& key) const -> std::expected<vk::raii::Pipeline, Error>
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
    const auto rendering = vk::PipelineRenderingCreateInfo {.depthAttachmentFormat = _depthFormat};
    const auto input = makeShadowVertexInput({.vertexStride = key.vertexStride,
                                              .positionOffset = key.positionOffset,
                                              .texCoordOffset = key.texCoordOffset,
                                              .colorOffset = key.colorOffset,
                                              .hasColor = key.hasColor,
                                              .hasBaseColorTexture = key.hasBaseColorTexture});
    const auto vertexInput =
        makeVertexInputState(input.binding, std::span {input.attributes}.first(input.attributeCount));
    const auto state = GraphicsPipelineState {.stages = std::span {stages}.first(stageCount),
                                              .rendering = rendering,
                                              .multisampling = multisample,
                                              .rasterization = makeRasterizationState(key.renderFace, key.mirrored),
                                              .depthStencil = depthStencil,
                                              .colorBlend = noColorBlend,
                                              .vertexInput = vertexInput,
                                              .layout = *_pipelineLayout};
    return checkedCreation(_device.createGraphicsPipeline(nullptr, makeGraphicsPipelineCreateInfo(state)));
}

auto MeshRenderer::writeFrameUniform(FrameResources& frame, const RenderView& view) -> std::expected<void, Error>
{
    const auto uniform = makeFrameUniform(view);
    if (!uniform)
    {
        return std::unexpected {uniform.error()};
    }
    const auto bytes = std::as_bytes(std::span {&*uniform, std::size_t {1}});
    if (const auto written = frame.uniformBuffer->writeMapped(0, bytes); !written)
    {
        return written;
    }
    return {};
}

auto MeshRenderer::ensureMaterialBuffer(FrameResources& frame, std::size_t drawCount) -> std::expected<void, Error>
{
    const auto maxStorageBufferRange =
        static_cast<std::uint64_t>(_physicalDevice.getProperties().limits.maxStorageBufferRange);
    if (drawCount > maxStorageBufferRange / sizeof(MaterialRecord))
    {
        return std::unexpected {
            makeError(ErrorCode::ResourceExhausted, "Mesh draw count exceeds material storage buffer limits")};
    }
    const auto requiredCapacity = sizeof(MaterialRecord) * std::max<std::size_t>(drawCount, 1);
    if (frame.materialBufferCapacity < requiredCapacity)
    {
        const auto description = BufferDesc {.size = requiredCapacity,
                                             .usage = vk::BufferUsageFlagBits::eStorageBuffer,
                                             .memory = BufferMemory::Upload};
        auto buffer = ::panda::detail::BufferResource::create(_device, _pool, description);
        if (!buffer)
        {
            return std::unexpected {buffer.error()};
        }
        frame.materialBuffer = std::move(*buffer);
        frame.materialBufferCapacity = requiredCapacity;
        updateDescriptorSet(frame);
    }
    return {};
}

auto MeshRenderer::ensureTextureDescriptorSets(FrameResources& frame, std::size_t drawCount)
    -> std::expected<void, Error>
{
    if (drawCount <= frame.textureDescriptorSets.size())
    {
        return {};
    }
    auto resources = createTextureDescriptorResources(_device, *_textureDescriptorSetLayout, drawCount);
    if (!resources)
    {
        return std::unexpected {resources.error()};
    }
    frame.textureDescriptorSets.clear();
    frame.textureDescriptorPool = std::move(resources->pool);
    frame.textureDescriptorSets = std::move(resources->sets);
    return {};
}

void MeshRenderer::writeTextureDescriptors(FrameResources& frame, const RenderView& view)
{
    auto imageInfos = std::vector<vk::DescriptorImageInfo>(view.meshes.size() * textureDescriptorsPerDraw);
    auto writes = std::vector<vk::WriteDescriptorSet> {};
    writes.reserve(view.meshes.size() * textureDescriptorsPerDraw);
    const auto shadowView = frame.shadowMap->getNativeView();
    for (auto index = std::size_t {}; index < view.meshes.size(); ++index)
    {
        const auto& material = view.meshes.at(index).material;
        const auto textures =
            std::array {&material.baseColorTexture, &material.metallicRoughnessTexture, &material.emissiveTexture};
        const auto descriptorSet = *frame.textureDescriptorSets.at(index);
        for (auto binding = std::size_t {}; binding < textures.size(); ++binding)
        {
            const auto imageInfoIndex = (index * textureDescriptorsPerDraw) + binding;
            imageInfos.at(imageInfoIndex) =
                makeTextureDescriptorInfo(*textures.at(binding), *_textureSampler, shadowView);
            writes.push_back({.dstSet = descriptorSet,
                              .dstBinding = static_cast<std::uint32_t>(binding),
                              .descriptorCount = 1,
                              .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                              .pImageInfo = &imageInfos.at(imageInfoIndex)});
        }
    }
    _device.updateDescriptorSets(writes, {});
}

auto MeshRenderer::writeMaterialRecords(FrameResources& frame, const RenderView& view) -> std::expected<void, Error>
{
    _materialRecords.clear();
    _materialRecords.reserve(view.meshes.size());
    std::ranges::transform(view.meshes, std::back_inserter(_materialRecords), [](const auto& draw) {
        return makeMaterialRecord(draw.material);
    });
    if (!_materialRecords.empty())
    {
        const auto materialBytes = std::as_bytes(std::span {_materialRecords});
        if (const auto written = frame.materialBuffer->writeMapped(0, materialBytes); !written)
        {
            return written;
        }
    }
    return {};
}

auto MeshRenderer::prepareDrawPipeline(const MeshDraw& draw) -> std::expected<void, Error>
{
    const auto key = getMeshPipelineKey(draw);
    if (!_pipelines.contains(key))
    {
        auto pipeline = createPipeline(key);
        if (!pipeline)
        {
            return std::unexpected {pipeline.error()};
        }
        _pipelines.emplace(key, std::move(*pipeline));
    }
    const auto shadowKey = getShadowPipelineKey(draw);
    if (draw.material.alphaMode != AlphaMode::Blend && !_shadowPipelines.contains(shadowKey))
    {
        auto pipeline = createShadowPipeline(shadowKey);
        if (!pipeline)
        {
            return std::unexpected {pipeline.error()};
        }
        _shadowPipelines.emplace(shadowKey, std::move(*pipeline));
    }
    return {};
}

auto MeshRenderer::prepareDrawPipelines(const RenderView& view) -> std::expected<void, Error>
{
    for (const auto& draw : view.meshes)
    {
        if (const auto prepared = prepareDrawPipeline(draw); !prepared)
        {
            return prepared;
        }
    }
    return {};
}

auto MeshRenderer::updateFrame(std::size_t frameIndex, const RenderView& view) -> std::expected<void, Error>
{
    auto& frame = _frames.at(frameIndex);
    if (const auto written = writeFrameUniform(frame, view); !written)
    {
        return written;
    }
    if (const auto prepared = ensureMaterialBuffer(frame, view.meshes.size()); !prepared)
    {
        return prepared;
    }
    if (const auto written = writeMaterialRecords(frame, view); !written)
    {
        return written;
    }
    if (const auto ensured = ensureTextureDescriptorSets(frame, view.meshes.size()); !ensured)
    {
        return ensured;
    }
    writeTextureDescriptors(frame, view);
    return prepareDrawPipelines(view);
}

auto MeshRenderer::getSampleCount() const noexcept -> vk::SampleCountFlagBits
{
    return _sampleCount;
}

void MeshRenderer::record(vk::CommandBuffer command,
                          vk::ImageView colorView,
                          std::size_t frameIndex,
                          const RenderView& view)
{
    auto& frame = _frames.at(frameIndex);
    recordShadowPass(command, frame, view);
    recordColorPass(command, colorView, frame, view);
}

void MeshRenderer::recordShadowPass(vk::CommandBuffer command, FrameResources& frame, const RenderView& view)
{
    transitionShadowForAttachment(command, frame);
    recordShadowAttachment(command, frame.shadowMap->getNativeView());
    setDrawState(command, frame, shadowExtent);
    auto materialIndex = std::uint32_t {};
    for (const auto& draw : view.meshes)
    {
        if (draw.material.alphaMode != AlphaMode::Blend)
        {
            recordDraw(command, frame, draw, {.shadowPass = true, .materialIndex = materialIndex});
        }
        ++materialIndex;
    }
    command.endRendering();
    transitionShadowForSampling(command, frame);
}

void MeshRenderer::recordColorPass(vk::CommandBuffer command,
                                   vk::ImageView colorView,
                                   FrameResources& frame,
                                   const RenderView& view)
{
    transitionColor(command, frame);
    transitionDepth(command, frame);
    recordAttachments(command, colorView, frame, frame.depthAttachment->getNativeView());
    setDrawState(command, frame, _extent);
    auto materialIndex = std::uint32_t {};
    for (const auto& draw : view.meshes)
    {
        if (draw.material.alphaMode == AlphaMode::Blend)
        {
            // shortcut: sort by mesh origin; intersecting meshes need finer sorting.
            _blendDraws.push_back({.draw = &draw,
                                   .materialIndex = materialIndex,
                                   .cameraDepth = cameraDepthForMesh(view.camera.viewProjection, draw.modelMatrix)});
        }
        else
        {
            recordDraw(command, frame, draw, {.shadowPass = false, .materialIndex = materialIndex});
        }
        ++materialIndex;
    }
    recordBlendedDraws(command, frame);
    command.endRendering();
}

void MeshRenderer::recordBlendedDraws(vk::CommandBuffer command, const FrameResources& frame)
{
    std::ranges::stable_sort(_blendDraws, [](const BlendDraw& left, const BlendDraw& right) {
        return left.cameraDepth > right.cameraDepth;
    });
    for (const auto& blendDraw : _blendDraws)
    {
        recordDraw(command, frame, *blendDraw.draw, {.materialIndex = blendDraw.materialIndex});
    }
    _blendDraws.clear();
}

void MeshRenderer::transitionColor(vk::CommandBuffer command, FrameResources& frame)
{
    if (_sampleCount == vk::SampleCountFlagBits::e1)
    {
        return;
    }
    const auto barrier = vk::ImageMemoryBarrier2 {
        .srcStageMask = frame.colorInitialized ? vk::PipelineStageFlagBits2::eColorAttachmentOutput
                                               : vk::PipelineStageFlagBits2::eNone,
        .srcAccessMask = frame.colorInitialized ? vk::AccessFlagBits2::eColorAttachmentWrite : vk::AccessFlags2 {},
        .dstStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        .dstAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
        .oldLayout = frame.colorInitialized ? vk::ImageLayout::eColorAttachmentOptimal : vk::ImageLayout::eUndefined,
        .newLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
        .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
        .image = frame.colorAttachment->getNative(),
        .subresourceRange = vk::ImageSubresourceRange {.aspectMask = vk::ImageAspectFlagBits::eColor,
                                                                                                                 .levelCount = 1,
                                                                                                                 .layerCount = 1}
    };
    command.pipelineBarrier2(vk::DependencyInfo {.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier});
}

void MeshRenderer::transitionDepth(vk::CommandBuffer command, FrameResources& frame)
{
    const auto barrier = vk::ImageMemoryBarrier2 {
        .srcStageMask = frame.depthInitialized ? vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                                                     vk::PipelineStageFlagBits2::eLateFragmentTests
                                               : vk::PipelineStageFlagBits2::eNone,
        .srcAccessMask =
            frame.depthInitialized ? vk::AccessFlagBits2::eDepthStencilAttachmentWrite : vk::AccessFlags2 {},
        .dstStageMask =
            vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
        .dstAccessMask =
            vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
        .oldLayout = frame.depthInitialized ? vk::ImageLayout::eDepthAttachmentOptimal : vk::ImageLayout::eUndefined,
        .newLayout = vk::ImageLayout::eDepthAttachmentOptimal,
        .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
        .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
        .image = frame.depthAttachment->getNative(),
        .subresourceRange = vk::ImageSubresourceRange {.aspectMask = vk::ImageAspectFlagBits::eDepth,
                                                                                                           .levelCount = 1,
                                                                                                           .layerCount = 1}
    };
    command.pipelineBarrier2(vk::DependencyInfo {.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier});
}

void MeshRenderer::transitionShadowForAttachment(vk::CommandBuffer command, FrameResources& frame)
{
    const auto barrier = vk::ImageMemoryBarrier2 {
        .srcStageMask =
            frame.shadowInitialized ? vk::PipelineStageFlagBits2::eFragmentShader : vk::PipelineStageFlagBits2::eNone,
        .srcAccessMask = frame.shadowInitialized ? vk::AccessFlagBits2::eShaderSampledRead : vk::AccessFlags2 {},
        .dstStageMask =
            vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
        .dstAccessMask =
            vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
        .oldLayout = frame.shadowInitialized ? vk::ImageLayout::eDepthReadOnlyOptimal : vk::ImageLayout::eUndefined,
        .newLayout = vk::ImageLayout::eDepthAttachmentOptimal,
        .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
        .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
        .image = frame.shadowMap->getNative(),
        .subresourceRange = vk::ImageSubresourceRange {.aspectMask = vk::ImageAspectFlagBits::eDepth,
                                                                                                               .levelCount = 1,
                                                                                                               .layerCount = 1}
    };
    command.pipelineBarrier2(vk::DependencyInfo {.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier});
}

void MeshRenderer::transitionShadowForSampling(vk::CommandBuffer command, FrameResources& frame)
{
    const auto barrier = vk::ImageMemoryBarrier2 {
        .srcStageMask =
            vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
        .srcAccessMask = vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader,
        .dstAccessMask = vk::AccessFlagBits2::eShaderSampledRead,
        .oldLayout = vk::ImageLayout::eDepthAttachmentOptimal,
        .newLayout = vk::ImageLayout::eDepthReadOnlyOptimal,
        .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
        .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
        .image = frame.shadowMap->getNative(),
        .subresourceRange = vk::ImageSubresourceRange {.aspectMask = vk::ImageAspectFlagBits::eDepth,
                                                       .levelCount = 1,
                                                       .layerCount = 1}
    };
    command.pipelineBarrier2(vk::DependencyInfo {.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier});
}

void MeshRenderer::recordAttachments(vk::CommandBuffer command,
                                     vk::ImageView swapchainView,
                                     const FrameResources& frame,
                                     vk::ImageView depthView) const
{
    auto color = vk::RenderingAttachmentInfo {
        .imageView =
            _sampleCount == vk::SampleCountFlagBits::e1 ? swapchainView : frame.colorAttachment->getNativeView(),
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = _sampleCount == vk::SampleCountFlagBits::e1 ? vk::AttachmentStoreOp::eStore
                                                               : vk::AttachmentStoreOp::eDontCare,
        .clearValue = vk::ClearValue {.color = encodedClear(_colorFormat)}};
    if (_sampleCount != vk::SampleCountFlagBits::e1)
    {
        color.resolveMode = vk::ResolveModeFlagBits::eAverage;
        color.resolveImageView = swapchainView;
        color.resolveImageLayout = vk::ImageLayout::eColorAttachmentOptimal;
    }
    const auto depth = vk::RenderingAttachmentInfo {
        .imageView = depthView,
        .imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eDontCare,
        .clearValue = vk::ClearValue {.depthStencil = vk::ClearDepthStencilValue {.depth = 1.0F, .stencil = 0}}};
    const auto info = vk::RenderingInfo {.renderArea = vk::Rect2D {.extent = _extent},
                                         .layerCount = 1,
                                         .colorAttachmentCount = 1,
                                         .pColorAttachments = &color,
                                         .pDepthAttachment = &depth};
    command.beginRendering(info);
}

void MeshRenderer::recordShadowAttachment(vk::CommandBuffer command, vk::ImageView depthView)
{
    const auto depth = vk::RenderingAttachmentInfo {
        .imageView = depthView,
        .imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .clearValue = vk::ClearValue {.depthStencil = vk::ClearDepthStencilValue {.depth = 1.0F, .stencil = 0}}};
    const auto info = vk::RenderingInfo {.renderArea = vk::Rect2D {.extent = shadowExtent},
                                         .layerCount = 1,
                                         .pDepthAttachment = &depth};
    command.beginRendering(info);
}

void MeshRenderer::setDrawState(vk::CommandBuffer command, const FrameResources& frame, vk::Extent2D extent) const
{
    command.setViewport(0,
                        vk::Viewport {.width = static_cast<float>(extent.width),
                                      .height = static_cast<float>(extent.height),
                                      .minDepth = 0.0F,
                                      .maxDepth = 1.0F});
    command.setScissor(0, vk::Rect2D {.extent = extent});
    const auto descriptorSet = *frame.descriptorSet;
    command.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *_pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
}

void MeshRenderer::recordDraw(vk::CommandBuffer command,
                              const FrameResources& frame,
                              const MeshDraw& draw,
                              DrawPass pass) const
{
    if (pass.shadowPass)
    {
        command.bindPipeline(vk::PipelineBindPoint::eGraphics, *_shadowPipelines.at(getShadowPipelineKey(draw)));
    }
    else
    {
        command.bindPipeline(vk::PipelineBindPoint::eGraphics, *_pipelines.at(getMeshPipelineKey(draw)));
    }
    if (!pass.shadowPass || (draw.material.alphaMode == AlphaMode::Mask && draw.material.baseColorTexture.isValid()))
    {
        const auto textureSet = *frame.textureDescriptorSets.at(pass.materialIndex);
        command.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *_pipelineLayout, 1, 1, &textureSet, 0, nullptr);
    }
    const auto constants = makeDrawConstants(draw);
    command.pushConstants(*_pipelineLayout, vk::ShaderStageFlagBits::eVertex, 0, sizeof(constants), &constants);
    const auto& mesh = *::panda::detail::MeshResource::getResource(draw.mesh);
    const auto vertexBuffer = mesh.getVertexBuffer()->getNative();
    static constexpr auto vertexOffset = vk::DeviceSize {};
    command.bindVertexBuffers(0, 1, &vertexBuffer, &vertexOffset);
    recordMeshDraw(command, mesh, pass.materialIndex);
}

void MeshRenderer::recordMeshDraw(vk::CommandBuffer command,
                                  const ::panda::detail::MeshResource& mesh,
                                  std::uint32_t materialIndex)
{
    const auto& indices = mesh.getLayout().getIndices();
    if (indices)
    {
        const auto nativeType =
            indices->type == MeshIndexType::UInt16 ? vk::IndexType::eUint16 : vk::IndexType::eUint32;
        command.bindIndexBuffer(mesh.getIndexBuffer()->getNative(), 0, nativeType);
        command.drawIndexed(indices->count, 1, 0, 0, materialIndex);
        return;
    }
    command.draw(mesh.getLayout().getVertexCount(), 1, 0, materialIndex);
}

void MeshRenderer::markSubmitted(std::size_t frameIndex) noexcept
{
    _frames[frameIndex].colorInitialized = _sampleCount != vk::SampleCountFlagBits::e1;
    _frames[frameIndex].depthInitialized = true;
    _frames[frameIndex].shadowInitialized = true;
}
}
