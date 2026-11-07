// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include "renderer/MeshRenderer.hpp"

#include <algorithm>
#include <array>
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
#include <span>
#include <utility>
#include <vector>

#include "BuiltinShaders.hpp"
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
static_assert(sizeof(MaterialRecord) == 16);
static_assert(offsetof(MaterialRecord, metallic) == 0);
static_assert(offsetof(MaterialRecord, roughness) == 4);
static_assert(offsetof(MaterialRecord, isLit) == 8);
static_assert(offsetof(MaterialRecord, renderFace) == 12);

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
constexpr auto blendAttachment = vk::PipelineColorBlendAttachmentState {
    .blendEnable = vk::False,
    .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB |
                      vk::ColorComponentFlagBits::eA};
constexpr auto blend = vk::PipelineColorBlendStateCreateInfo {.attachmentCount = 1, .pAttachments = &blendAttachment};
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
            .frontFace = mirrored ? vk::FrontFace::eCounterClockwise : vk::FrontFace::eClockwise,
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
    return MaterialRecord {.metallic = std::clamp(material.metallic, 0.0F, 1.0F),
                           .roughness = std::clamp(material.roughness, 0.0F, 1.0F),
                           .isLit = material.model == MaterialModel::Lit ? 1U : 0U,
                           .renderFace = static_cast<std::uint32_t>(material.renderFace)};
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

struct GraphicsPipelineState
{
    std::span<const vk::PipelineShaderStageCreateInfo> stages;
    const vk::PipelineRenderingCreateInfo& rendering;
    const vk::PipelineMultisampleStateCreateInfo& multisampling;
    vk::PipelineRasterizationStateCreateInfo rasterization;
    const vk::PipelineColorBlendStateCreateInfo& colorBlend;
    const vk::PipelineVertexInputStateCreateInfo& vertexInput;
    vk::PipelineLayout layout;
};

struct ShadowVertexInput
{
    vk::VertexInputBindingDescription binding;
    vk::VertexInputAttributeDescription attribute;
};

auto makeShadowVertexInput(std::uint32_t vertexStride, std::uint32_t positionOffset) -> ShadowVertexInput
{
    return {
        .binding = {.binding = 0, .stride = vertexStride, .inputRate = vk::VertexInputRate::eVertex},
        .attribute = {.location = 0, .binding = 0, .format = vk::Format::eR32G32B32Sfloat, .offset = positionOffset}
    };
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
            .pDepthStencilState = &depthStencil,
            .pColorBlendState = &state.colorBlend,
            .pDynamicState = &dynamic,
            .layout = state.layout,
            .renderPass = {},
            .basePipelineIndex = -1};
}

auto makeOutputSrgbSpecialization(const std::uint32_t& outputIsSrgb) -> vk::SpecializationInfo
{
    static constexpr auto entry =
        vk::SpecializationMapEntry {.constantID = 0, .offset = 0, .size = sizeof(std::uint32_t)};
    return {.mapEntryCount = 1, .pMapEntries = &entry, .dataSize = sizeof(outputIsSrgb), .pData = &outputIsSrgb};
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

auto MeshRenderer::initializePipelineLayout() -> std::expected<void, Error>
{
    static constexpr auto pushRange = vk::PushConstantRange {.stageFlags = vk::ShaderStageFlagBits::eVertex,
                                                             .offset = 0,
                                                             .size = sizeof(DrawConstants)};
    const auto layoutInfo = vk::PipelineLayoutCreateInfo {.setLayoutCount = 1,
                                                          .pSetLayouts = &*_descriptorSetLayout,
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
    return {};
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

auto MeshRenderer::createShaderModule(std::span<const std::uint32_t> words) const
    -> std::expected<vk::raii::ShaderModule, Error>
{
    const auto info = vk::ShaderModuleCreateInfo {.codeSize = words.size_bytes(), .pCode = words.data()};
    const auto module = (*_device).createShaderModule(info, nullptr, *_device.getDispatcher());
    if (module.result != vk::Result::eSuccess)
    {
        return std::unexpected {makeVulkanError(module.result)};
    }
    return std::expected<vk::raii::ShaderModule, Error> {std::in_place,
                                                         _device,
                                                         static_cast<vk::ShaderModule::CType>(module.value)};
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
    return {.lit = lit,
            .hasColor = hasColor,
            .vertexStride = draw.mesh.getVertexStride(),
            .positionOffset = findOffset(VertexAttributeSemantic::Position),
            .normalOffset = lit ? findOffset(VertexAttributeSemantic::Normal) : 0U,
            .colorOffset = hasColor ? findOffset(VertexAttributeSemantic::Color) : 0U,
            .renderFace = draw.material.renderFace,
            .mirrored = glm::determinant(glm::mat3 {draw.modelMatrix}) < 0.0F};
}

auto MeshRenderer::getShadowPipelineKey(const MeshDraw& draw) -> ShadowPipelineKey
{
    const auto attributes = draw.mesh.getVertexAttributes();
    const auto position = std::ranges::find(attributes, VertexAttributeSemantic::Position, &VertexAttribute::semantic);
    return {.vertexStride = draw.mesh.getVertexStride(),
            .positionOffset = position->offset,
            .renderFace = draw.material.renderFace,
            .mirrored = glm::determinant(glm::mat3 {draw.modelMatrix}) < 0.0F};
}

auto MeshRenderer::makeVertexAttributeDescriptions(const MeshPipelineKey& key)
    -> std::vector<vk::VertexInputAttributeDescription>
{
    auto attributes = std::vector<vk::VertexInputAttributeDescription> {
        vk::VertexInputAttributeDescription {.location = 0,
                                             .binding = 0,
                                             .format = vk::Format::eR32G32B32Sfloat,
                                             .offset = key.positionOffset}
    };
    if (key.lit)
    {
        attributes.push_back(
            {.location = 1, .binding = 0, .format = vk::Format::eR32G32B32Sfloat, .offset = key.normalOffset});
    }
    if (key.hasColor)
    {
        attributes.push_back(
            {.location = 4, .binding = 0, .format = vk::Format::eR32G32B32A32Sfloat, .offset = key.colorOffset});
    }
    return attributes;
}

auto MeshRenderer::createPipeline(const MeshPipelineKey& key) const -> std::expected<vk::raii::Pipeline, Error>
{
    const auto vertexWords = [&]() -> std::span<const std::uint32_t> {
        if (key.lit)
        {
            return key.hasColor ? std::span<const std::uint32_t> {shaders::meshLitColorVertex}
                                : std::span<const std::uint32_t> {shaders::meshLitPlainVertex};
        }
        return key.hasColor ? std::span<const std::uint32_t> {shaders::meshUnlitColorVertex}
                            : std::span<const std::uint32_t> {shaders::meshUnlitPlainVertex};
    }();
    auto vertex = createShaderModule(vertexWords);
    if (!vertex)
    {
        return std::unexpected {vertex.error()};
    }
    auto fragment = createShaderModule(shaders::meshFragment);
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
    const auto vertexInput = vk::PipelineVertexInputStateCreateInfo {
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &binding,
        .vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size()),
        .pVertexAttributeDescriptions = attributes.data()};
    const auto outputIsSrgb = static_cast<std::uint32_t>(isSrgb(_colorFormat));
    const auto specialization = makeOutputSrgbSpecialization(outputIsSrgb);
    const auto stages = makeShaderStages(vertex, fragment, specialization);
    const auto colorOutput = _colorFormat;
    const auto rendering = vk::PipelineRenderingCreateInfo {.colorAttachmentCount = 1,
                                                            .pColorAttachmentFormats = &colorOutput,
                                                            .depthAttachmentFormat = _depthFormat};
    const auto multisampling = vk::PipelineMultisampleStateCreateInfo {.rasterizationSamples = _sampleCount};
    const auto state = GraphicsPipelineState {.stages = stages,
                                              .rendering = rendering,
                                              .multisampling = multisampling,
                                              .rasterization = makeRasterizationState(key.renderFace, key.mirrored),
                                              .colorBlend = blend,
                                              .vertexInput = vertexInput,
                                              .layout = *_pipelineLayout};
    const auto createInfo = makeGraphicsPipelineCreateInfo(state);
    return checkedCreation(_device.createGraphicsPipeline(nullptr, createInfo));
}

auto MeshRenderer::createShadowPipeline(const ShadowPipelineKey& key) const -> std::expected<vk::raii::Pipeline, Error>
{
    auto vertex = createShaderModule(shaders::shadowVertex);
    if (!vertex)
    {
        return std::unexpected {vertex.error()};
    }
    const auto stages = std::array {
        vk::PipelineShaderStageCreateInfo {.stage = vk::ShaderStageFlagBits::eVertex,
                                           .module = **vertex,
                                           .pName = "main"}
    };
    const auto rendering = vk::PipelineRenderingCreateInfo {.depthAttachmentFormat = _depthFormat};
    const auto input = makeShadowVertexInput(key.vertexStride, key.positionOffset);
    const auto vertexInput = vk::PipelineVertexInputStateCreateInfo {.vertexBindingDescriptionCount = 1,
                                                                     .pVertexBindingDescriptions = &input.binding,
                                                                     .vertexAttributeDescriptionCount = 1,
                                                                     .pVertexAttributeDescriptions = &input.attribute};
    const auto state = GraphicsPipelineState {.stages = stages,
                                              .rendering = rendering,
                                              .multisampling = multisample,
                                              .rasterization = makeRasterizationState(key.renderFace, key.mirrored),
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
    if (!_shadowPipelines.contains(shadowKey))
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
        recordDraw(command, draw, true, materialIndex);
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
        recordDraw(command, draw, false, materialIndex);
        ++materialIndex;
    }
    command.endRendering();
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
                              const MeshDraw& draw,
                              bool shadowPass,
                              std::uint32_t materialIndex) const
{
    if (shadowPass)
    {
        command.bindPipeline(vk::PipelineBindPoint::eGraphics, *_shadowPipelines.at(getShadowPipelineKey(draw)));
    }
    else
    {
        command.bindPipeline(vk::PipelineBindPoint::eGraphics, *_pipelines.at(getMeshPipelineKey(draw)));
    }
    const auto constants = makeDrawConstants(draw);
    command.pushConstants(*_pipelineLayout, vk::ShaderStageFlagBits::eVertex, 0, sizeof(constants), &constants);
    const auto& mesh = *::panda::detail::MeshResource::getResource(draw.mesh);
    const auto vertexBuffer = mesh.getVertexBuffer()->getNative();
    static constexpr auto vertexOffset = vk::DeviceSize {};
    command.bindVertexBuffers(0, 1, &vertexBuffer, &vertexOffset);
    recordMeshDraw(command, mesh, materialIndex);
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
