// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include "renderer/MeshRenderer.hpp"

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
#include <limits>
#include <memory>
#include <panda/Buffer.hpp>
#include <panda/Error.hpp>
#include <panda/Geometry.hpp>
#include <panda/Mesh.hpp>
#include <panda/RenderView.hpp>
#include <span>
#include <utility>
#include <vector>

#include "BuiltinShaders.hpp"
#include "VulkanError.hpp"
#include "presentation/SwapchainGeneration.hpp"
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

static_assert(sizeof(FrameUniform) == 128);
static_assert(sizeof(glm::vec4) == 16);
static_assert(offsetof(FrameUniform, cameraPosition) == 64);
static_assert(offsetof(FrameUniform, lightDirection) == 80);
static_assert(offsetof(FrameUniform, lightColorIntensity) == 96);
static_assert(offsetof(FrameUniform, ambient) == 112);
static_assert(sizeof(DrawConstants) == 128);
static_assert(offsetof(DrawConstants, normalColumns) == 64);
static_assert(offsetof(DrawConstants, tint) == 112);

auto isSrgb(vk::Format format) -> bool
{
    return format == vk::Format::eB8G8R8A8Srgb || format == vk::Format::eR8G8B8A8Srgb;
}

constexpr auto makeVertexBindings() -> std::array<vk::VertexInputBindingDescription, 1>
{
    return {
        vk::VertexInputBindingDescription {.binding = 0,
                                           .stride = standardMeshVertexStrideBytes,
                                           .inputRate = vk::VertexInputRate::eVertex}
    };
}

constexpr auto makeVertexAttributes() -> std::array<vk::VertexInputAttributeDescription, 3>
{
    return {
        vk::VertexInputAttributeDescription {.location = 0,
                                             .binding = 0,
                                             .format = vk::Format::eR32G32B32Sfloat,
                                             .offset = offsetof(MeshVertex, position)},
        vk::VertexInputAttributeDescription {.location = 1,
                                             .binding = 0,
                                             .format = vk::Format::eR32G32B32Sfloat,
                                             .offset = offsetof(MeshVertex, normal)  },
        vk::VertexInputAttributeDescription {.location = 4,
                                             .binding = 0,
                                             .format = vk::Format::eR32G32B32A32Sfloat,
                                             .offset = offsetof(MeshVertex, color)   }
    };
}

constexpr auto vertexBindings = makeVertexBindings();
constexpr auto vertexAttributes = makeVertexAttributes();
constexpr auto dynamicStates = std::array {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
constexpr auto vertexInput = vk::PipelineVertexInputStateCreateInfo {
    .vertexBindingDescriptionCount = static_cast<std::uint32_t>(vertexBindings.size()),
    .pVertexBindingDescriptions = vertexBindings.data(),
    .vertexAttributeDescriptionCount = static_cast<std::uint32_t>(vertexAttributes.size()),
    .pVertexAttributeDescriptions = vertexAttributes.data()};
constexpr auto inputAssembly =
    vk::PipelineInputAssemblyStateCreateInfo {.topology = vk::PrimitiveTopology::eTriangleList};
constexpr auto viewport = vk::PipelineViewportStateCreateInfo {.viewportCount = 1, .scissorCount = 1};
constexpr auto rasterization = vk::PipelineRasterizationStateCreateInfo {.polygonMode = vk::PolygonMode::eFill,
                                                                         .cullMode = vk::CullModeFlagBits::eNone,
                                                                         .frontFace = vk::FrontFace::eCounterClockwise,
                                                                         .lineWidth = 1.0F};
constexpr auto multisample =
    vk::PipelineMultisampleStateCreateInfo {.rasterizationSamples = vk::SampleCountFlagBits::e1};
constexpr auto depthStencil = vk::PipelineDepthStencilStateCreateInfo {
    .depthTestEnable = vk::True, .depthWriteEnable = vk::True, .depthCompareOp = vk::CompareOp::eLess};
constexpr auto blendAttachment = vk::PipelineColorBlendAttachmentState {
    .blendEnable = vk::False,
    .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB |
                      vk::ColorComponentFlagBits::eA};
constexpr auto blend = vk::PipelineColorBlendStateCreateInfo {.attachmentCount = 1, .pAttachments = &blendAttachment};
constexpr auto dynamic = vk::PipelineDynamicStateCreateInfo {
    .dynamicStateCount = static_cast<std::uint32_t>(dynamicStates.size()), .pDynamicStates = dynamicStates.data()};

auto makeFrameUniform(const RenderView& view) -> FrameUniform
{
    const auto direction = glm::normalize(view.directionalLight.direction);
    return {
        .viewProjection = view.camera.viewProjection,
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
        .tint = draw.tint
    };
    return constants;
}

auto createDepthFormatSupport(vk::PhysicalDevice physicalDevice, vk::Format format) -> std::expected<bool, Error>
{
    const auto support = physicalDevice.getImageFormatProperties(format,
                                                                 vk::ImageType::e2D,
                                                                 vk::ImageTiling::eOptimal,
                                                                 vk::ImageUsageFlagBits::eDepthStencilAttachment,
                                                                 {});
    if (support.result == vk::Result::eErrorFormatNotSupported)
    {
        return false;
    }
    if (support.result != vk::Result::eSuccess)
    {
        return std::unexpected {makeVulkanError(support.result)};
    }
    return (support.value.sampleCounts & vk::SampleCountFlagBits::e1) != vk::SampleCountFlags {};
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

auto makeGraphicsPipelineCreateInfo(vk::PipelineLayout layout,
                                    const std::array<vk::PipelineShaderStageCreateInfo, 2>& stages,
                                    const vk::PipelineRenderingCreateInfo& rendering) -> vk::GraphicsPipelineCreateInfo
{
    return {.pNext = &rendering,
            .stageCount = static_cast<std::uint32_t>(stages.size()),
            .pStages = stages.data(),
            .pVertexInputState = &vertexInput,
            .pInputAssemblyState = &inputAssembly,
            .pViewportState = &viewport,
            .pRasterizationState = &rasterization,
            .pMultisampleState = &multisample,
            .pDepthStencilState = &depthStencil,
            .pColorBlendState = &blend,
            .pDynamicState = &dynamic,
            .layout = layout,
            .renderPass = {},
            .basePipelineIndex = -1};
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
    if (const auto descriptors = initializeDescriptorState(); !descriptors)
    {
        return descriptors;
    }
    if (const auto buffers = initializeFrameBuffers(); !buffers)
    {
        return buffers;
    }
    return initializeDepthAndPipeline(extent, colorFormat);
}

auto MeshRenderer::initializeDescriptorState() -> std::expected<void, Error>
{
    static constexpr auto binding = vk::DescriptorSetLayoutBinding {
        .binding = 0,
        .descriptorType = vk::DescriptorType::eUniformBuffer,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment};
    const auto setLayout = (*_device).createDescriptorSetLayout(
        vk::DescriptorSetLayoutCreateInfo {.bindingCount = 1, .pBindings = &binding},
        nullptr,
        *_device.getDispatcher());
    if (setLayout.result != vk::Result::eSuccess)
    {
        return std::unexpected {makeVulkanError(setLayout.result)};
    }
    _descriptorSetLayout =
        vk::raii::DescriptorSetLayout {_device, static_cast<vk::DescriptorSetLayout::CType>(setLayout.value)};
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
    return createDescriptorPool();
}

auto MeshRenderer::createDescriptorPool() -> std::expected<void, Error>
{
    const auto setCount = static_cast<std::uint32_t>(_frames.size());
    const auto poolSize =
        vk::DescriptorPoolSize {.type = vk::DescriptorType::eUniformBuffer, .descriptorCount = setCount};
    auto pool = checkedCreation(_device.createDescriptorPool(
        vk::DescriptorPoolCreateInfo {.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
                                      .maxSets = setCount,
                                      .poolSizeCount = 1,
                                      .pPoolSizes = &poolSize}));
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
        updateDescriptorSet(frame);
    }
    return {};
}

void MeshRenderer::updateDescriptorSet(const FrameResources& frame)
{
    const auto bufferInfo = vk::DescriptorBufferInfo {.buffer = frame.uniformBuffer->getNative(),
                                                      .offset = 0,
                                                      .range = sizeof(FrameUniform)};
    const auto write = vk::WriteDescriptorSet {.dstSet = *frame.descriptorSet,
                                               .dstBinding = 0,
                                               .descriptorCount = 1,
                                               .descriptorType = vk::DescriptorType::eUniformBuffer,
                                               .pBufferInfo = &bufferInfo};
    _device.updateDescriptorSets(write, {});
}

auto MeshRenderer::initializeDepthAndPipeline(vk::Extent2D extent, vk::Format colorFormat) -> std::expected<void, Error>
{
    const auto selectedDepthFormat = chooseDepthFormat();
    if (!selectedDepthFormat)
    {
        return std::unexpected {selectedDepthFormat.error()};
    }
    _depthFormat = *selectedDepthFormat;
    return resize(extent, colorFormat);
}

auto MeshRenderer::createDepthAttachments(vk::Extent2D extent)
    -> std::expected<std::vector<std::shared_ptr<::panda::detail::TextureResource>>, Error>
{
    auto attachments = std::vector<std::shared_ptr<::panda::detail::TextureResource>> {};
    attachments.reserve(_frames.size());
    for (auto index = std::size_t {}; index < _frames.size(); ++index)
    {
        auto attachment = ::panda::detail::TextureResource::create(
            _device,
            _pool,
            {.extent = extent, .format = _depthFormat, .usage = vk::ImageUsageFlagBits::eDepthStencilAttachment});
        if (!attachment)
        {
            return std::unexpected {attachment.error()};
        }
        attachments.push_back(std::move(*attachment));
    }
    return attachments;
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

auto MeshRenderer::resize(vk::Extent2D extent, vk::Format colorFormat) -> std::expected<void, Error>
{
    if (extent.width == 0 || extent.height == 0 || colorFormat == vk::Format::eUndefined)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Invalid renderer output extent or format")};
    }
    const auto extentChanged = extent.width != _extent.width || extent.height != _extent.height;
    const auto formatChanged = colorFormat != _colorFormat;
    if (!extentChanged && !formatChanged)
    {
        return {};
    }
    auto resources = createResizeResources(extent, colorFormat, extentChanged, formatChanged);
    if (!resources)
    {
        return std::unexpected {resources.error()};
    }
    publishResize(extent, colorFormat, std::move(resources->depthAttachments), std::move(resources->pipeline));
    return {};
}

auto MeshRenderer::createResizeResources(vk::Extent2D extent,
                                         vk::Format colorFormat,
                                         bool extentChanged,
                                         bool formatChanged) -> std::expected<ResizeResources, Error>
{
    auto resources = ResizeResources {};
    if (extentChanged)
    {
        auto attachments = createDepthAttachments(extent);
        if (!attachments)
        {
            return std::unexpected {attachments.error()};
        }
        resources.depthAttachments = std::move(*attachments);
    }
    if (formatChanged)
    {
        auto pipeline = createPipeline(colorFormat);
        if (!pipeline)
        {
            return std::unexpected {pipeline.error()};
        }
        resources.pipeline = std::move(*pipeline);
    }
    return resources;
}

void MeshRenderer::publishResize(vk::Extent2D extent,
                                 vk::Format colorFormat,
                                 std::vector<std::shared_ptr<::panda::detail::TextureResource>> attachments,
                                 vk::raii::Pipeline pipeline)
{
    const auto extentChanged = extent.width != _extent.width || extent.height != _extent.height;
    if (extentChanged)
    {
        for (auto index = std::size_t {}; index < _frames.size(); ++index)
        {
            _frames[index].depthAttachment = std::move(attachments[index]);
            _frames[index].depthInitialized = false;
        }
        _extent = extent;
    }
    const auto formatChanged = colorFormat != _colorFormat;
    if (formatChanged)
    {
        _pipeline = std::move(pipeline);
        _colorFormat = colorFormat;
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

auto MeshRenderer::createPipeline(vk::Format colorFormat) const -> std::expected<vk::raii::Pipeline, Error>
{
    auto vertex = createShaderModule(shaders::meshVertex);
    if (!vertex)
    {
        return std::unexpected {vertex.error()};
    }
    auto fragment = createShaderModule(shaders::meshFragment);
    if (!fragment)
    {
        return std::unexpected {fragment.error()};
    }
    return createGraphicsPipeline(colorFormat, **vertex, **fragment);
}

auto MeshRenderer::createGraphicsPipeline(vk::Format colorFormat,
                                          vk::ShaderModule vertex,
                                          vk::ShaderModule fragment) const -> std::expected<vk::raii::Pipeline, Error>
{
    const auto outputIsSrgb = static_cast<std::uint32_t>(isSrgb(colorFormat));
    static constexpr auto specializationEntry =
        vk::SpecializationMapEntry {.constantID = 0, .offset = 0, .size = sizeof(outputIsSrgb)};
    const auto specialization = vk::SpecializationInfo {.mapEntryCount = 1,
                                                        .pMapEntries = &specializationEntry,
                                                        .dataSize = sizeof(outputIsSrgb),
                                                        .pData = &outputIsSrgb};
    const auto stages = makeShaderStages(vertex, fragment, specialization);
    const auto colorOutput = colorFormat;
    const auto rendering = vk::PipelineRenderingCreateInfo {.colorAttachmentCount = 1,
                                                            .pColorAttachmentFormats = &colorOutput,
                                                            .depthAttachmentFormat = _depthFormat};
    const auto createInfo = makeGraphicsPipelineCreateInfo(_pipelineLayout, stages, rendering);
    return checkedCreation(_device.createGraphicsPipeline(nullptr, createInfo));
}

auto MeshRenderer::updateFrame(std::size_t frameIndex, const RenderView& view) -> std::expected<void, Error>
{
    const auto uniform = makeFrameUniform(view);
    const auto bytes = std::as_bytes(std::span {&uniform, std::size_t {1}});
    return _frames.at(frameIndex).uniformBuffer->writeMapped(0, bytes);
}

void MeshRenderer::record(vk::CommandBuffer command,
                          vk::ImageView colorView,
                          std::size_t frameIndex,
                          const RenderView& view)
{
    auto& frame = _frames.at(frameIndex);
    transitionDepth(command, frame);
    recordAttachments(command, colorView, frame.depthAttachment->getNativeView());
    setDrawState(command, frame);
    for (const auto& draw : view.meshes)
    {
        recordDraw(command, draw);
    }
    command.endRendering();
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

void MeshRenderer::recordAttachments(vk::CommandBuffer command, vk::ImageView colorView, vk::ImageView depthView) const
{
    const auto color = vk::RenderingAttachmentInfo {.imageView = colorView,
                                                    .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
                                                    .loadOp = vk::AttachmentLoadOp::eClear,
                                                    .storeOp = vk::AttachmentStoreOp::eStore,
                                                    .clearValue = vk::ClearValue {.color = encodedClear(_colorFormat)}};
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

void MeshRenderer::setDrawState(vk::CommandBuffer command, const FrameResources& frame) const
{
    command.setViewport(0,
                        vk::Viewport {.width = static_cast<float>(_extent.width),
                                      .height = static_cast<float>(_extent.height),
                                      .minDepth = 0.0F,
                                      .maxDepth = 1.0F});
    command.setScissor(0, vk::Rect2D {.extent = _extent});
    command.bindPipeline(vk::PipelineBindPoint::eGraphics, *_pipeline);
    const auto descriptorSet = *frame.descriptorSet;
    command.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *_pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
}

void MeshRenderer::recordDraw(vk::CommandBuffer command, const MeshDraw& draw) const
{
    const auto constants = makeDrawConstants(draw);
    command.pushConstants(*_pipelineLayout, vk::ShaderStageFlagBits::eVertex, 0, sizeof(constants), &constants);
    const auto& mesh = *::panda::detail::MeshResource::getResource(draw.mesh);
    const auto vertexBuffer = mesh.getVertexBuffer()->getNative();
    static constexpr auto vertexOffset = vk::DeviceSize {};
    command.bindVertexBuffers(0, 1, &vertexBuffer, &vertexOffset);
    recordMeshDraw(command, mesh);
}

void MeshRenderer::recordMeshDraw(vk::CommandBuffer command, const ::panda::detail::MeshResource& mesh)
{
    const auto& indices = mesh.getLayout().getIndices();
    if (indices)
    {
        const auto nativeType =
            indices->type == MeshIndexType::UInt16 ? vk::IndexType::eUint16 : vk::IndexType::eUint32;
        command.bindIndexBuffer(mesh.getIndexBuffer()->getNative(), 0, nativeType);
        command.drawIndexed(indices->count, 1, 0, 0, 0);
        return;
    }
    command.draw(mesh.getLayout().getVertexCount(), 1, 0, 0);
}

void MeshRenderer::markSubmitted(std::size_t frameIndex) noexcept
{
    _frames[frameIndex].depthInitialized = true;
}
}
