// clang-format off
#include <panda/base/Error.hpp>
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include "presentation/ToneMappingPass.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <panda/vulkan/Error.hpp>
#include <span>
#include <utility>
#include <vector>

#include "BuiltinShaders.hpp"
#include "VulkanError.hpp"
#include "presentation/FrameExecution.hpp"
#include "renderer/ShaderInterface.inc"

namespace panda::core::vulkan::presentation
{
using detail::checkedCreation;

namespace
{
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

auto isSrgb(vk::Format format) -> bool
{
    return format == vk::Format::eB8G8R8A8Srgb || format == vk::Format::eR8G8B8A8Srgb;
}

constexpr auto inputAssembly =
    vk::PipelineInputAssemblyStateCreateInfo {.topology = vk::PrimitiveTopology::eTriangleList};
constexpr auto vertexInput = vk::PipelineVertexInputStateCreateInfo {};
constexpr auto viewportState = vk::PipelineViewportStateCreateInfo {.viewportCount = 1, .scissorCount = 1};
constexpr auto rasterization = vk::PipelineRasterizationStateCreateInfo {.polygonMode = vk::PolygonMode::eFill,
                                                                         .cullMode = vk::CullModeFlagBits::eNone,
                                                                         .frontFace = vk::FrontFace::eCounterClockwise,
                                                                         .lineWidth = 1.0F};
constexpr auto multisampling =
    vk::PipelineMultisampleStateCreateInfo {.rasterizationSamples = vk::SampleCountFlagBits::e1};
constexpr auto blendAttachment = vk::PipelineColorBlendAttachmentState {
    .blendEnable = vk::False,
    .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB |
                      vk::ColorComponentFlagBits::eA};
constexpr auto blend = vk::PipelineColorBlendStateCreateInfo {.attachmentCount = 1, .pAttachments = &blendAttachment};
constexpr auto dynamicStates = std::array {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
constexpr auto dynamic = vk::PipelineDynamicStateCreateInfo {
    .dynamicStateCount = static_cast<std::uint32_t>(dynamicStates.size()), .pDynamicStates = dynamicStates.data()};

auto createPipelineForStages(const vk::raii::Device& device,
                             vk::PipelineLayout layout,
                             vk::Format outputFormat,
                             const std::array<vk::PipelineShaderStageCreateInfo, 2>& stages)
    -> std::expected<vk::raii::Pipeline, Error>
{
    const auto rendering =
        vk::PipelineRenderingCreateInfo {.colorAttachmentCount = 1, .pColorAttachmentFormats = &outputFormat};
    const auto createInfo = vk::GraphicsPipelineCreateInfo {.pNext = &rendering,
                                                            .stageCount = static_cast<std::uint32_t>(stages.size()),
                                                            .pStages = stages.data(),
                                                            .pVertexInputState = &vertexInput,
                                                            .pInputAssemblyState = &inputAssembly,
                                                            .pViewportState = &viewportState,
                                                            .pRasterizationState = &rasterization,
                                                            .pMultisampleState = &multisampling,
                                                            .pColorBlendState = &blend,
                                                            .pDynamicState = &dynamic,
                                                            .layout = layout,
                                                            .basePipelineIndex = -1};
    return checkedCreation(device.createGraphicsPipeline(nullptr, createInfo));
}

auto createToneMappingPipeline(const vk::raii::Device& device, vk::PipelineLayout layout, vk::Format outputFormat)
    -> std::expected<vk::raii::Pipeline, Error>
{
    auto vertex = createShaderModule(device, renderer::shaders::toneMapVertex);
    if (!vertex)
    {
        return std::unexpected {vertex.error()};
    }
    auto fragment = createShaderModule(device, renderer::shaders::toneMapFragment);
    if (!fragment)
    {
        return std::unexpected {fragment.error()};
    }
    const auto outputIsSrgb = static_cast<std::uint32_t>(isSrgb(outputFormat));
    static constexpr auto specializationEntry =
        vk::SpecializationMapEntry {.constantID = PANDA_OUTPUT_SRGB_SPECIALIZATION_ID,
                                    .offset = 0,
                                    .size = sizeof(std::uint32_t)};
    const auto specialization = vk::SpecializationInfo {.mapEntryCount = 1,
                                                        .pMapEntries = &specializationEntry,
                                                        .dataSize = sizeof(outputIsSrgb),
                                                        .pData = &outputIsSrgb};
    const auto stages = std::array {
        vk::PipelineShaderStageCreateInfo {.stage = vk::ShaderStageFlagBits::eVertex,
                                           .module = **vertex,
                                           .pName = "main"},
        vk::PipelineShaderStageCreateInfo {.stage = vk::ShaderStageFlagBits::eFragment,
                                           .module = **fragment,
                                           .pName = "main",
                                           .pSpecializationInfo = &specialization}
    };
    return createPipelineForStages(device, layout, outputFormat, stages);
}
}

ToneMappingPass::ToneMappingPass(const vk::raii::Device& device)
    : _device {device}
{
}

ToneMappingPass::~ToneMappingPass() = default;

auto ToneMappingPass::create(const vk::raii::Device& device) -> std::expected<std::unique_ptr<ToneMappingPass>, Error>
{
    auto pass = std::unique_ptr<ToneMappingPass> {new ToneMappingPass {device}};
    if (const auto initialized = pass->initialize(); !initialized)
    {
        return std::unexpected {initialized.error()};
    }
    return pass;
}

auto ToneMappingPass::initialize() -> std::expected<void, Error>
{
    if (const auto sampler = initializeSampler(); !sampler)
    {
        return sampler;
    }
    if (const auto layouts = initializeLayouts(); !layouts)
    {
        return layouts;
    }
    return initializeDescriptors();
}

auto ToneMappingPass::initializeSampler() -> std::expected<void, Error>
{
    static constexpr auto samplerInfo = vk::SamplerCreateInfo {.magFilter = vk::Filter::eNearest,
                                                               .minFilter = vk::Filter::eNearest,
                                                               .mipmapMode = vk::SamplerMipmapMode::eNearest,
                                                               .addressModeU = vk::SamplerAddressMode::eClampToEdge,
                                                               .addressModeV = vk::SamplerAddressMode::eClampToEdge,
                                                               .addressModeW = vk::SamplerAddressMode::eClampToEdge,
                                                               .maxLod = 0.0F};
    const auto sampler = static_cast<vk::Device>(_device).createSampler(samplerInfo);
    if (sampler.result != vk::Result::eSuccess)
    {
        return std::unexpected {makeVulkanError(sampler.result)};
    }
    _sampler = vk::raii::Sampler {_device, sampler.value};
    return {};
}

auto ToneMappingPass::initializeLayouts() -> std::expected<void, Error>
{
    static constexpr auto binding =
        vk::DescriptorSetLayoutBinding {.binding = 0,
                                        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                        .descriptorCount = 1,
                                        .stageFlags = vk::ShaderStageFlagBits::eFragment};
    const auto layout = (*_device).createDescriptorSetLayout(
        vk::DescriptorSetLayoutCreateInfo {.bindingCount = 1, .pBindings = &binding},
        nullptr,
        *_device.getDispatcher());
    if (layout.result != vk::Result::eSuccess)
    {
        return std::unexpected {makeVulkanError(layout.result)};
    }
    _descriptorSetLayout =
        vk::raii::DescriptorSetLayout {_device, static_cast<vk::DescriptorSetLayout::CType>(layout.value)};

    const auto descriptorSetLayout = *_descriptorSetLayout;
    auto pipelineLayout = checkedCreation(_device.createPipelineLayout(
        vk::PipelineLayoutCreateInfo {.setLayoutCount = 1, .pSetLayouts = &descriptorSetLayout}));
    if (!pipelineLayout)
    {
        return std::unexpected {pipelineLayout.error()};
    }
    _pipelineLayout = std::move(*pipelineLayout);
    return {};
}

auto ToneMappingPass::initializeDescriptors() -> std::expected<void, Error>
{
    static constexpr auto poolSize = vk::DescriptorPoolSize {.type = vk::DescriptorType::eCombinedImageSampler,
                                                             .descriptorCount = static_cast<std::uint32_t>(frameCount)};
    auto pool = checkedCreation(_device.createDescriptorPool(
        vk::DescriptorPoolCreateInfo {.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
                                      .maxSets = static_cast<std::uint32_t>(frameCount),
                                      .poolSizeCount = 1,
                                      .pPoolSizes = &poolSize}));
    if (!pool)
    {
        return std::unexpected {pool.error()};
    }
    _descriptorPool = std::move(*pool);

    const auto layouts = std::array<vk::DescriptorSetLayout, frameCount> {*_descriptorSetLayout, *_descriptorSetLayout};
    auto descriptorSets = _device.allocateDescriptorSets(
        vk::DescriptorSetAllocateInfo {.descriptorPool = *_descriptorPool,
                                       .descriptorSetCount = static_cast<std::uint32_t>(layouts.size()),
                                       .pSetLayouts = layouts.data()});
    if (descriptorSets.result != vk::Result::eSuccess)
    {
        return std::unexpected {makeVulkanError(descriptorSets.result)};
    }
    _descriptorSets = std::move(descriptorSets.value);
    return {};
}

auto ToneMappingPass::setInputViews(const std::array<vk::ImageView, frameCount>& views) -> std::expected<void, Error>
{
    auto imageInfos = std::array<vk::DescriptorImageInfo, frameCount> {};
    auto writes = std::array<vk::WriteDescriptorSet, frameCount> {};
    for (auto index = std::size_t {}; index < frameCount; ++index)
    {
        if (!views.at(index))
        {
            return std::unexpected {makeError(ErrorCode::InvalidArgument, "Tone mapping needs valid HDR image views")};
        }
        imageInfos.at(index) = {.sampler = *_sampler,
                                .imageView = views.at(index),
                                .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal};
        writes.at(index) = {.dstSet = *_descriptorSets.at(index),
                            .dstBinding = 0,
                            .descriptorCount = 1,
                            .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                            .pImageInfo = &imageInfos.at(index)};
    }
    _device.updateDescriptorSets(writes, {});
    return {};
}

auto ToneMappingPass::prepare(vk::Format outputFormat) -> std::expected<void, Error>
{
    if (_pipelines.contains(outputFormat))
    {
        return {};
    }
    auto pipeline = createPipeline(outputFormat);
    if (!pipeline)
    {
        return std::unexpected {pipeline.error()};
    }
    _pipelines.emplace(outputFormat, std::move(*pipeline));
    return {};
}

auto ToneMappingPass::createPipeline(vk::Format outputFormat) const -> std::expected<vk::raii::Pipeline, Error>
{
    return createToneMappingPipeline(_device, *_pipelineLayout, outputFormat);
}

void ToneMappingPass::record(vk::CommandBuffer command, std::size_t frameIndex, const FrameOutput& output) const
{
    const auto attachment = vk::RenderingAttachmentInfo {.imageView = output.imageView,
                                                         .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
                                                         .loadOp = vk::AttachmentLoadOp::eDontCare,
                                                         .storeOp = vk::AttachmentStoreOp::eStore};
    command.beginRendering(vk::RenderingInfo {.renderArea = vk::Rect2D {.extent = output.extent},
                                              .layerCount = 1,
                                              .colorAttachmentCount = 1,
                                              .pColorAttachments = &attachment});
    command.bindPipeline(vk::PipelineBindPoint::eGraphics, *_pipelines.at(output.colorFormat));
    const auto viewport = vk::Viewport {.x = 0.0F,
                                        .y = 0.0F,
                                        .width = static_cast<float>(output.extent.width),
                                        .height = static_cast<float>(output.extent.height),
                                        .minDepth = 0.0F,
                                        .maxDepth = 1.0F};
    command.setViewport(0, viewport);
    command.setScissor(0, vk::Rect2D {.extent = output.extent});
    const auto descriptorSet = *_descriptorSets.at(frameIndex);
    command.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *_pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
    command.draw(3, 1, 0, 0);
    command.endRendering();
}
}
