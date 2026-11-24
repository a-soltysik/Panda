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
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float4.hpp>
#include <glm/gtc/matrix_access.hpp>
#include <glm/matrix.hpp>
#include <limits>
#include <memory>
#include <optional>
#include <panda/base/Error.hpp>
#include <panda/diagnostics/Assert.hpp>
#include <panda/rendering/RenderView.hpp>
#include <panda/rendering/SampleCount.hpp>
#include <panda/rendering/TextureFilteringPreset.hpp>
#include <panda/resources/Mesh.hpp>
#include <ranges>
#include <utility>
#include <vector>

#include "Math.hpp"
#include "renderer/FrameBindings.hpp"
#include "renderer/MeshPipelineCache.hpp"
#include "renderer/RenderAttachments.hpp"
#include "renderer/ShaderData.hpp"
#include "resources/MeshResource.hpp"

namespace panda::core::vulkan::renderer
{
namespace
{
auto makeDrawConstants(const glm::mat4& modelMatrix, const glm::vec4& tint) -> DrawConstants
{
    const auto normalMatrix = glm::transpose(glm::inverse(glm::mat3 {modelMatrix}));
    const auto constants = DrawConstants {
        .modelMatrix = modelMatrix,
        .normalColumns = {glm::vec4 {glm::column(normalMatrix, 0), 0.0F},
                          glm::vec4 {glm::column(normalMatrix, 1), 0.0F},
                          glm::vec4 {glm::column(normalMatrix, 2), 0.0F}},
        .tint = tint
    };
    return constants;
}

auto makeDrawConstants(const MeshDraw& draw) -> DrawConstants
{
    return makeDrawConstants(draw.modelMatrix, draw.tint * draw.material.baseColor);
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
}

auto makeClearColor(vk::Format format) -> vk::ClearColorValue
{
    static constexpr auto clearRed = 0.025F;
    static constexpr auto clearGreen = 0.035F;
    static constexpr auto clearBlue = 0.05F;
    const auto isSrgb = format == vk::Format::eB8G8R8A8Srgb || format == vk::Format::eR8G8B8A8Srgb;
    const auto isHdr = format == hdrColorFormat;
    return vk::ClearColorValue {
        std::array {isSrgb || isHdr ? clearRed : ::panda::detail::linearToSrgb(clearRed),
                    isSrgb || isHdr ? clearGreen : ::panda::detail::linearToSrgb(clearGreen),
                    isSrgb || isHdr ? clearBlue : ::panda::detail::linearToSrgb(clearBlue),
                    1.0F}
    };
}

auto makeToneMappedClearColor(vk::Format format) -> vk::ClearColorValue
{
    constexpr auto clear = std::array {0.025F, 0.035F, 0.05F};
    const auto srgb = format == vk::Format::eB8G8R8A8Srgb || format == vk::Format::eR8G8B8A8Srgb;
    const auto transform = [srgb](float value) {
        const auto mapped =
            std::clamp((value * ((2.51F * value) + 0.03F)) / ((value * ((2.43F * value) + 0.59F)) + 0.14F), 0.0F, 1.0F);
        if (srgb)
        {
            return mapped;
        }
        return ::panda::detail::linearToSrgb(mapped);
    };
    return vk::ClearColorValue {
        std::array {transform(clear.at(0)), transform(clear.at(1)), transform(clear.at(2)), 1.0F}
    };
}

MeshRenderer::MeshRenderer(const MeshRendererCreateInfo& info)
    : _physicalDevice {info.physicalDevice},
      _device {info.device},
      _pool {info.pool},
      _frameCount {info.frameCount},
      _maxLights {info.maxLights},
      _textureFilteringPreset {info.textureFilteringPreset}
{
}

auto MeshRenderer::pipelineCache() -> MeshPipelineCache&
{
    if (_pipelineCache)
    {
        return *_pipelineCache;
    }
    panic("Mesh pipeline cache is unavailable before renderer initialization");
}

auto MeshRenderer::pipelineCache() const -> const MeshPipelineCache&
{
    if (_pipelineCache)
    {
        return *_pipelineCache;
    }
    panic("Mesh pipeline cache is unavailable before renderer initialization");
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
    if (auto initialized = renderer->initialize(info); !initialized)
    {
        const auto& failure = initialized.error();
        return std::unexpected {makeError(failure.code, failure.message, failure.native, failure.source)};
    }
    return renderer;
}

auto MeshRenderer::initialize(const MeshRendererCreateInfo& info) -> std::expected<void, Error>
{
    if (_physicalDevice.getProperties().limits.maxPushConstantsSize < sizeof(DrawConstants))
    {
        return std::unexpected {
            makeError(ErrorCode::Unsupported, "Device push-constant limit is below renderer needs")};
    }
    if (const auto supported = FrameBindings::validateDeviceLimits(_physicalDevice, _maxLights); !supported)
    {
        return supported;
    }
    if (const auto attachments = initializeAttachments(info); !attachments)
    {
        return attachments;
    }
    return initializeBindingsAndPipeline();
}

auto MeshRenderer::initializeAttachments(const MeshRendererCreateInfo& info) -> std::expected<void, Error>
{
    return RenderAttachments::create({.physicalDevice = _physicalDevice,
                                      .device = _device,
                                      .pool = _pool,
                                      .extent = info.extent,
                                      .colorFormat = info.colorFormat,
                                      .frameCount = _frameCount,
                                      .requestedSampleCount = info.requestedSampleCount})
        .transform([this](std::unique_ptr<RenderAttachments> attachments) {
            _attachments = std::move(attachments);
        });
}

auto MeshRenderer::initializeBindingsAndPipeline() -> std::expected<void, Error>
{
    auto bindings = FrameBindings::create({.physicalDevice = _physicalDevice,
                                           .device = _device,
                                           .pool = _pool,
                                           .frameCount = _frameCount,
                                           .maxLights = _maxLights,
                                           .textureFilteringPreset = _textureFilteringPreset});
    if (!bindings)
    {
        return std::unexpected {bindings.error()};
    }
    _bindings = std::move(*bindings);
    _applicationBindings.emplace(_device, *_bindings, _pool, _frameCount);
    _pipelineCache.emplace(_device,
                           _bindings->getPipelineLayout(),
                           _bindings->getShaderDescriptorSchema(),
                           &*_applicationBindings);
    const auto& description = _attachments->getOutputDescription();
    pipelineCache().setOutputCompatibility({.colorFormat = description.colorFormat,
                                            .depthFormat = description.depthFormat,
                                            .sampleCount = description.sampleCount});
    return {};
}

auto MeshRenderer::resize(vk::Extent2D extent, vk::Format colorFormat, SampleCount requestedSampleCount)
    -> std::expected<void, Error>
{
    auto pending = _attachments->prepareResize(extent, colorFormat, requestedSampleCount);
    if (!pending)
    {
        return std::unexpected {pending.error()};
    }
    const auto& description = pending->description;
    pipelineCache().setOutputCompatibility({.colorFormat = description.colorFormat,
                                            .depthFormat = description.depthFormat,
                                            .sampleCount = description.sampleCount});
    _attachments->publishResize(std::move(*pending));
    return {};
}

auto MeshRenderer::updateFrame(std::size_t frameIndex, const RenderView& view) -> std::expected<void, Error>
{
    if (const auto updated = _bindings->updateFrame(frameIndex,
                                                    view,
                                                    _attachments->getShadowView(frameIndex),
                                                    _attachments->getShadowLayerView(frameIndex, 0));
        !updated)
    {
        return updated;
    }
    const auto shadowLayerCount = _bindings->getShadowAtlasPlan(frameIndex).atlasLayerCount;
    return _attachments->ensureShadowAtlasLayers(frameIndex, shadowLayerCount)
        .and_then([&](bool atlasGrown) {
            return atlasGrown ? _bindings->updateFrame(frameIndex,
                                                       view,
                                                       _attachments->getShadowView(frameIndex),
                                                       _attachments->getShadowLayerView(frameIndex, 0))
                              : std::expected<void, Error> {};
        })
        .and_then([&] {
            return _applicationBindings->prepare(frameIndex, view);
        })
        .and_then([&] {
            return pipelineCache().prepare(view);
        });
}

auto MeshRenderer::getSampleCount() const noexcept -> vk::SampleCountFlagBits
{
    return getOutputDescription().sampleCount;
}

auto MeshRenderer::getEffectiveAnisotropy(TextureFilteringPreset preset) const noexcept -> float
{
    return _bindings->getEffectiveAnisotropy(preset);
}

auto MeshRenderer::getOutputDescription() const noexcept -> const OutputDescription&
{
    return _attachments->getOutputDescription();
}

auto MeshRenderer::getResolvedColorView(std::size_t frameIndex) const -> vk::ImageView
{
    return _attachments->getResolvedColorView(frameIndex);
}

void MeshRenderer::record(vk::CommandBuffer command, std::size_t frameIndex, const RenderView& view)
{
    recordShadowPass(command, frameIndex, view);
    _attachments->transitionColor(command, frameIndex);
    _attachments->transitionDepth(command, frameIndex);
    _attachments->transitionResolvedColorToAttachment(command, frameIndex);
    recordAttachments(command, _attachments->getResolvedColorView(frameIndex), frameIndex);
    recordColorPass(command, frameIndex, view);
    _attachments->transitionResolvedColorToSampling(command, frameIndex);
}

void MeshRenderer::recordShadowPass(vk::CommandBuffer command, std::size_t frameIndex, const RenderView& view)
{
    _attachments->transitionShadowForAttachment(command, frameIndex);
    const auto& plan = _bindings->getShadowAtlasPlan(frameIndex);
    for (auto layer = std::uint32_t {}; layer < plan.atlasLayerCount; ++layer)
    {
        recordShadowLayer(command, frameIndex, view, layer);
    }
    _attachments->transitionShadowForSampling(command, frameIndex);
}

void MeshRenderer::recordShadowLayer(vk::CommandBuffer command,
                                     std::size_t frameIndex,
                                     const RenderView& view,
                                     std::uint32_t layer)
{
    recordShadowAttachment(command, _attachments->getShadowLayerView(frameIndex, layer));
    const auto& plan = _bindings->getShadowAtlasPlan(frameIndex);
    for (auto shadowViewIndex = std::size_t {}; shadowViewIndex < plan.views.size(); ++shadowViewIndex)
    {
        if (plan.views.at(shadowViewIndex).tile.layer == layer)
        {
            recordShadowView(command, frameIndex, view, shadowViewIndex);
        }
    }
    command.endRendering();
}

void MeshRenderer::recordShadowView(vk::CommandBuffer command,
                                    std::size_t frameIndex,
                                    const RenderView& view,
                                    std::size_t shadowViewIndex)
{
    const auto& shadowView = _bindings->getShadowAtlasPlan(frameIndex).views.at(shadowViewIndex);
    setViewportAndScissor(
        command,
        {.width = shadowView.tile.resolution, .height = shadowView.tile.resolution},
        {.x = static_cast<std::int32_t>(shadowView.tile.x), .y = static_cast<std::int32_t>(shadowView.tile.y)});
    bindFrameDescriptorSet(
        command,
        {.frameIndex = frameIndex,
         .shadowViewOffset = _bindings->getShadowViewOffset(frameIndex, static_cast<std::uint32_t>(shadowViewIndex))});
    auto materialIndex = std::uint32_t {};
    for (const auto& draw : view.meshes)
    {
        if (draw.material.alphaMode != AlphaMode::Blend)
        {
            recordDraw(command, frameIndex, draw, {.shadowPass = true, .materialIndex = materialIndex});
        }
        ++materialIndex;
    }
    for (const auto& [index, draw] : std::views::enumerate(view.proceduralDraws))
    {
        if (draw.vertexCount != 0 && draw.instanceCount != 0)
        {
            recordProceduralShadowDraw(command, frameIndex, draw, view.meshes.size() + static_cast<std::size_t>(index));
        }
    }
}

void MeshRenderer::recordColorPass(vk::CommandBuffer command, std::size_t frameIndex, const RenderView& view)
{
    setViewportAndScissor(command, _attachments->getOutputDescription().extent);
    bindFrameDescriptorSet(command, {.frameIndex = frameIndex, .shadowViewOffset = 0});
    if (view.skybox)
    {
        command.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelineCache().getSkyboxPipeline());
        command.draw(3, 1, 0, 0);
    }
    recordMeshColorDraws(command, frameIndex, view);
    recordProceduralColorDraws(command, frameIndex, view);
    recordBlendedDraws(command, frameIndex, view.environment.has_value());
    command.endRendering();
}

void MeshRenderer::recordMeshColorDraws(vk::CommandBuffer command, std::size_t frameIndex, const RenderView& view)
{
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
            recordDraw(
                command,
                frameIndex,
                draw,
                {.shadowPass = false, .materialIndex = materialIndex, .hasEnvironment = view.environment.has_value()});
        }
        ++materialIndex;
    }
}

void MeshRenderer::recordProceduralColorDraws(vk::CommandBuffer command, std::size_t frameIndex, const RenderView& view)
{
    for (const auto& [index, draw] : std::views::enumerate(view.proceduralDraws))
    {
        if (draw.vertexCount != 0 && draw.instanceCount != 0)
        {
            recordProceduralDraw(command, frameIndex, draw, view.meshes.size() + static_cast<std::size_t>(index));
        }
    }
}

void MeshRenderer::recordBlendedDraws(vk::CommandBuffer command, std::size_t frameIndex, bool hasEnvironment)
{
    std::ranges::stable_sort(_blendDraws, [](const BlendDraw& left, const BlendDraw& right) {
        return left.cameraDepth > right.cameraDepth;
    });
    for (const auto& blendDraw : _blendDraws)
    {
        recordDraw(command,
                   frameIndex,
                   *blendDraw.draw,
                   {.materialIndex = blendDraw.materialIndex, .hasEnvironment = hasEnvironment});
    }
    _blendDraws.clear();
}

void MeshRenderer::recordAttachments(vk::CommandBuffer command, vk::ImageView outputView, std::size_t frameIndex) const
{
    const auto& output = _attachments->getOutputDescription();
    const auto multisampled = output.sampleCount != vk::SampleCountFlagBits::e1;
    auto color = vk::RenderingAttachmentInfo {
        .imageView = multisampled ? _attachments->getColorView(frameIndex) : outputView,
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = multisampled ? vk::AttachmentStoreOp::eDontCare : vk::AttachmentStoreOp::eStore,
        .clearValue = vk::ClearValue {.color = makeClearColor(output.colorFormat)}};
    if (multisampled)
    {
        color.resolveMode = vk::ResolveModeFlagBits::eAverage;
        color.resolveImageView = outputView;
        color.resolveImageLayout = vk::ImageLayout::eColorAttachmentOptimal;
    }
    const auto depth = vk::RenderingAttachmentInfo {
        .imageView = _attachments->getDepthView(frameIndex),
        .imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eDontCare,
        .clearValue = vk::ClearValue {.depthStencil = vk::ClearDepthStencilValue {.depth = 1.0F, .stencil = 0}}};
    const auto info = vk::RenderingInfo {.renderArea = vk::Rect2D {.extent = output.extent},
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

void MeshRenderer::setViewportAndScissor(vk::CommandBuffer command, vk::Extent2D extent, vk::Offset2D offset)
{
    command.setViewport(0,
                        vk::Viewport {.x = static_cast<float>(offset.x),
                                      .y = static_cast<float>(offset.y),
                                      .width = static_cast<float>(extent.width),
                                      .height = static_cast<float>(extent.height),
                                      .minDepth = 0.0F,
                                      .maxDepth = 1.0F});
    command.setScissor(0, vk::Rect2D {.offset = offset, .extent = extent});
}

void MeshRenderer::bindFrameDescriptorSet(vk::CommandBuffer command, FrameDescriptorBinding binding) const
{
    const auto descriptorSet = _bindings->getFrameDescriptorSet(binding.frameIndex);
    command.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                               _bindings->getPipelineLayout(),
                               _bindings->getFrameDescriptorSetIndex(),
                               1,
                               &descriptorSet,
                               1,
                               &binding.shadowViewOffset);
}

void MeshRenderer::recordDraw(vk::CommandBuffer command,
                              std::size_t frameIndex,
                              const MeshDraw& draw,
                              DrawPass pass) const
{
    if (pass.shadowPass)
    {
        command.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelineCache().getShadowPipeline(draw));
    }
    else
    {
        command.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelineCache().getPipeline(draw, pass.hasEnvironment));
    }
    bindTextureDescriptors(command, frameIndex, draw, pass);
    bindApplicationDrawResources(command, frameIndex, draw, pass);
    const auto constants = makeDrawConstants(draw);
    command.pushConstants(_bindings->getPipelineLayout(),
                          vk::ShaderStageFlagBits::eVertex,
                          0,
                          sizeof(constants),
                          &constants);
    const auto& mesh = *::panda::detail::MeshResource::getResource(draw.mesh);
    const auto vertexBuffer = mesh.getVertexBuffer()->getNative();
    static constexpr auto vertexOffset = vk::DeviceSize {};
    command.bindVertexBuffers(0, 1, &vertexBuffer, &vertexOffset);
    recordMeshDraw(command, mesh, pass.materialIndex);
}

void MeshRenderer::bindApplicationDrawResources(vk::CommandBuffer command,
                                                std::size_t frameIndex,
                                                const MeshDraw& draw,
                                                DrawPass pass) const
{
    if (draw.material.shaderProgram.isValid())
    {
        const auto& bindings = panda::expect(_applicationBindings, "Custom shader bindings are unavailable");
        if (pass.shadowPass)
        {
            bindings.bindForShadow(command,
                                   {.frameIndex = frameIndex, .drawIndex = pass.materialIndex},
                                   draw.material.shaderProgram);
        }
        else
        {
            bindings.bind(command, {.frameIndex = frameIndex, .drawIndex = pass.materialIndex});
        }
    }
}

void MeshRenderer::recordProceduralDraw(vk::CommandBuffer command,
                                        std::size_t frameIndex,
                                        const ProceduralDraw& draw,
                                        std::size_t applicationDrawIndex) const
{
    command.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelineCache().getPipeline(draw));
    panda::expect(_applicationBindings, "Custom shader bindings are unavailable")
        .bind(command, {.frameIndex = frameIndex, .drawIndex = applicationDrawIndex});
    const auto constants = makeDrawConstants(draw.modelMatrix, draw.tint);
    command.pushConstants(_bindings->getPipelineLayout(),
                          vk::ShaderStageFlagBits::eVertex,
                          0,
                          sizeof(constants),
                          &constants);
    command.draw(draw.vertexCount, draw.instanceCount, 0, 0);
}

void MeshRenderer::recordProceduralShadowDraw(vk::CommandBuffer command,
                                              std::size_t frameIndex,
                                              const ProceduralDraw& draw,
                                              std::size_t applicationDrawIndex) const
{
    command.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelineCache().getShadowPipeline(draw));
    panda::expect(_applicationBindings, "Custom shader bindings are unavailable")
        .bindForShadow(command, {.frameIndex = frameIndex, .drawIndex = applicationDrawIndex}, draw.shaderProgram);
    const auto constants = makeDrawConstants(draw.modelMatrix, draw.tint);
    command.pushConstants(_bindings->getPipelineLayout(),
                          vk::ShaderStageFlagBits::eVertex,
                          0,
                          sizeof(constants),
                          &constants);
    command.draw(draw.vertexCount, draw.instanceCount, 0, 0);
}

void MeshRenderer::bindTextureDescriptors(vk::CommandBuffer command,
                                          std::size_t frameIndex,
                                          const MeshDraw& draw,
                                          DrawPass pass) const
{
    if (!pass.shadowPass || (draw.material.alphaMode == AlphaMode::Mask && draw.material.baseColorTexture.isValid()))
    {
        const auto textureSet = _bindings->getTextureDescriptorSet(frameIndex, pass.materialIndex);
        command.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                   _bindings->getPipelineLayout(),
                                   _bindings->getTextureDescriptorSetIndex(),
                                   1,
                                   &textureSet,
                                   0,
                                   nullptr);
    }
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
    _attachments->markSubmitted(frameIndex);
}
}
