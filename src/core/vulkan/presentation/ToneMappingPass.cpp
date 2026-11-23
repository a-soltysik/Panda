// clang-format off
#include <panda/base/Error.hpp>
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include "presentation/ToneMappingPass.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <numeric>
#include <panda/vulkan/Error.hpp>
#include <span>
#include <utility>
#include <vector>

#include "BuiltinShaders.hpp"
#include "VulkanError.hpp"
#include "presentation/FrameExecution.hpp"
#include "renderer/DescriptorSets.hpp"
#include "renderer/ShaderReflection.hpp"

namespace panda::core::vulkan::presentation
{
using detail::checkedCreation;

namespace
{
auto isSrgb(vk::Format format) -> bool
{
    return format == vk::Format::eB8G8R8A8Srgb || format == vk::Format::eR8G8B8A8Srgb;
}

auto hasToneMappingDescriptor(const renderer::ShaderProgramLayout& program) -> bool
{
    if (program.descriptors.size() != 1)
    {
        return false;
    }
    const auto& descriptor = program.descriptors.front();
    return descriptor.kind == renderer::ShaderDescriptorKind::CombinedImageSampler && descriptor.count == 1 &&
           std::ranges::find(descriptor.names, "hdrColor") != descriptor.names.end();
}

auto reflectedStageFlags(std::span<const renderer::ShaderProgramDescriptor::StageLayout> stageLayouts)
    -> vk::ShaderStageFlags
{
    return std::accumulate(stageLayouts.begin(),
                           stageLayouts.end(),
                           vk::ShaderStageFlags {},
                           [](auto flags, const auto& stage) {
                               return flags | (stage.stage == renderer::ShaderStage::Vertex
                                                   ? vk::ShaderStageFlagBits::eVertex
                                                   : vk::ShaderStageFlagBits::eFragment);
                           });
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

auto createToneMappingPipeline(const vk::raii::Device& device,
                               vk::PipelineLayout layout,
                               vk::Format outputFormat,
                               std::uint32_t specializationId) -> std::expected<vk::raii::Pipeline, Error>
{
    return detail::createShaderModule(device, renderer::shaders::toneMapVertex).and_then([&](auto vertex) {
        return detail::createShaderModule(device, renderer::shaders::toneMapFragment).and_then([&](auto fragment) {
            const auto outputIsSrgb = static_cast<std::uint32_t>(isSrgb(outputFormat));
            const auto specializationEntry =
                vk::SpecializationMapEntry {.constantID = specializationId, .offset = 0, .size = sizeof(std::uint32_t)};
            const auto specialization = vk::SpecializationInfo {.mapEntryCount = 1,
                                                                .pMapEntries = &specializationEntry,
                                                                .dataSize = sizeof(outputIsSrgb),
                                                                .pData = &outputIsSrgb};
            const auto stages = std::array {
                vk::PipelineShaderStageCreateInfo {.stage = vk::ShaderStageFlagBits::eVertex,
                                                   .module = *vertex,
                                                   .pName = "main"},
                vk::PipelineShaderStageCreateInfo {.stage = vk::ShaderStageFlagBits::eFragment,
                                                   .module = *fragment,
                                                   .pName = "main",
                                                   .pSpecializationInfo = &specialization}
            };
            return createPipelineForStages(device, layout, outputFormat, stages);
        });
    });
}
}

ToneMappingPass::ToneMappingPass(const vk::raii::Device& device, std::uint32_t maxBoundDescriptorSets)
    : _device {device},
      _maxBoundDescriptorSets {maxBoundDescriptorSets}
{
}

ToneMappingPass::~ToneMappingPass() = default;

auto ToneMappingPass::create(const vk::raii::Device& device, std::uint32_t maxBoundDescriptorSets)
    -> std::expected<std::unique_ptr<ToneMappingPass>, Error>
{
    auto pass = std::unique_ptr<ToneMappingPass> {
        new ToneMappingPass {device, maxBoundDescriptorSets}
    };
    return pass->initialize().transform([&] {
        return std::move(pass);
    });
}

auto ToneMappingPass::initialize() -> std::expected<void, Error>
{
    return initializeLayouts()
        .and_then([this] {
            return initializeSampler();
        })
        .and_then([this] {
            return initializeDescriptors();
        });
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
    return detail::createVulkanSampler(_device, samplerInfo).transform([this](auto sampler) {
        _sampler = std::move(sampler);
    });
}

auto ToneMappingPass::reflectInterface() -> std::expected<void, Error>
{
    const auto program =
        renderer::reflectGraphicsProgram(renderer::shaders::toneMapVertex,
                                         std::span<const std::uint32_t> {renderer::shaders::toneMapFragment},
                                         "tone mapping pipeline");
    if (!program)
    {
        return std::unexpected {program.error()};
    }
    return reflectDescriptor(*program)
        .and_then([&] {
            return renderer::specializationConstantId(*program,
                                                      renderer::ShaderStage::Fragment,
                                                      "outputIsSrgb",
                                                      "tone mapping pipeline");
        })
        .transform([this](auto id) {
            _outputSrgbSpecializationId = id;
        });
}

auto ToneMappingPass::reflectDescriptor(const renderer::ShaderProgramLayout& program) -> Result<void>
{
    if (!hasToneMappingDescriptor(program))
    {
        return std::unexpected {
            makeError(ErrorCode::InvalidArgument, "Tone mapping shader has an incompatible descriptor interface")};
    }
    const auto& descriptor = program.descriptors.front();
    if (descriptor.set >= _maxBoundDescriptorSets)
    {
        return std::unexpected {
            makeError(ErrorCode::Unsupported, "Tone mapping shader descriptor set exceeds device limits")};
    }
    const auto stages = reflectedStageFlags(descriptor.stageLayouts);
    if (!stages)
    {
        return std::unexpected {
            makeError(ErrorCode::InvalidArgument, "Tone mapping shader descriptor has no reflected stages")};
    }
    _descriptorSetIndex = descriptor.set;
    _binding = {.binding = descriptor.binding,
                .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                .descriptorCount = descriptor.count,
                .stageFlags = stages};
    return {};
}

auto ToneMappingPass::initializeLayouts() -> Result<void>
{
    return reflectInterface()
        .and_then([this] {
            return createSetLayout();
        })
        .and_then([this] {
            return createPipelineLayout();
        });
}

auto ToneMappingPass::createSetLayout() -> Result<void>
{
    const auto layout = (*_device).createDescriptorSetLayout(
        vk::DescriptorSetLayoutCreateInfo {.bindingCount = 1, .pBindings = &_binding},
        nullptr,
        *_device.getDispatcher());
    if (layout.result != vk::Result::eSuccess)
    {
        return std::unexpected {makeVulkanError(layout.result)};
    }
    _descriptorSetLayout =
        vk::raii::DescriptorSetLayout {_device, static_cast<vk::DescriptorSetLayout::CType>(layout.value)};

    return {};
}

auto ToneMappingPass::createPipelineLayout() -> Result<void>
{
    auto layouts = std::vector<vk::DescriptorSetLayout>(_descriptorSetIndex + 1U);
    for (auto index = std::uint32_t {}; index < _descriptorSetIndex; ++index)
    {
        auto emptyLayout = checkedCreation(_device.createDescriptorSetLayout({}));
        if (!emptyLayout)
        {
            return std::unexpected {emptyLayout.error()};
        }
        _emptyDescriptorSetLayouts.push_back(std::move(*emptyLayout));
        layouts.at(index) = *_emptyDescriptorSetLayouts.back();
    }
    layouts.back() = *_descriptorSetLayout;
    auto pipelineLayout = checkedCreation(_device.createPipelineLayout(
        vk::PipelineLayoutCreateInfo {.setLayoutCount = static_cast<std::uint32_t>(layouts.size()),
                                      .pSetLayouts = layouts.data()}));
    return std::move(pipelineLayout).transform([this](auto layout) {
        _pipelineLayout = std::move(layout);
    });
}

auto ToneMappingPass::initializeDescriptors() -> std::expected<void, Error>
{
    return renderer::createDescriptorSets(_device, *_descriptorSetLayout, std::span {&_binding, 1U}, frameCount)
        .transform([this](auto descriptors) {
            _descriptors = std::move(descriptors);
        });
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
        writes.at(index) = {.dstSet = *_descriptors.sets.at(index),
                            .dstBinding = _binding.binding,
                            .descriptorCount = _binding.descriptorCount,
                            .descriptorType = _binding.descriptorType,
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
    return createToneMappingPipeline(_device, *_pipelineLayout, outputFormat, _outputSrgbSpecializationId);
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
    const auto descriptorSet = *_descriptors.sets.at(frameIndex);
    command.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                               *_pipelineLayout,
                               _descriptorSetIndex,
                               1,
                               &descriptorSet,
                               0,
                               nullptr);
    command.draw(3, 1, 0, 0);
    command.endRendering();
}
}
