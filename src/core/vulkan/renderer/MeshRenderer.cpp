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
#include <glm/matrix.hpp>
#include <limits>
#include <memory>
#include <optional>
#include <panda/Assert.hpp>
#include <panda/Error.hpp>
#include <panda/Mesh.hpp>
#include <panda/RenderView.hpp>
#include <utility>
#include <vector>

#include "Math.hpp"
#include "renderer/FrameBindings.hpp"
#include "renderer/MeshPipelineCache.hpp"
#include "renderer/RenderAttachments.hpp"
#include "renderer/ShaderData.hpp"
#include "renderer/ShaderInterface.inc"
#include "resources/MeshResource.hpp"

namespace panda::core::vulkan::renderer
{
namespace
{
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
    return vk::ClearColorValue {
        std::array {isSrgb ? clearRed : ::panda::detail::linearToSrgb(clearRed),
                    isSrgb ? clearGreen : ::panda::detail::linearToSrgb(clearGreen),
                    isSrgb ? clearBlue : ::panda::detail::linearToSrgb(clearBlue),
                    1.0F}
    };
}

MeshRenderer::MeshRenderer(const MeshRendererCreateInfo& info)
    : _physicalDevice {info.physicalDevice},
      _device {info.device},
      _pool {info.pool},
      _frameCount {info.frameCount},
      _maxLights {info.maxLights}
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
    if (auto initialized = renderer->initialize(info.extent, info.colorFormat); !initialized)
    {
        const auto& failure = initialized.error();
        return std::unexpected {makeError(failure.code, failure.message, failure.native, failure.source)};
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
    if (const auto supported = FrameBindings::validateDeviceLimits(_physicalDevice, _maxLights); !supported)
    {
        return supported;
    }
    if (const auto attachments = initializeAttachments(extent, colorFormat); !attachments)
    {
        return attachments;
    }
    return initializeBindingsAndPipeline();
}

auto MeshRenderer::initializeAttachments(vk::Extent2D extent, vk::Format colorFormat) -> std::expected<void, Error>
{
    return RenderAttachments::create({.physicalDevice = _physicalDevice,
                                      .device = _device,
                                      .pool = _pool,
                                      .extent = extent,
                                      .colorFormat = colorFormat,
                                      .frameCount = _frameCount})
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
                                           .maxLights = _maxLights});
    if (!bindings)
    {
        return std::unexpected {bindings.error()};
    }
    _bindings = std::move(*bindings);
    _pipelineCache.emplace(_device, _bindings->getPipelineLayout());
    const auto& description = _attachments->getOutputDescription();
    pipelineCache().setOutputCompatibility({.colorFormat = description.colorFormat,
                                            .depthFormat = description.depthFormat,
                                            .sampleCount = description.sampleCount});
    return {};
}

auto MeshRenderer::resize(vk::Extent2D extent, vk::Format colorFormat) -> std::expected<void, Error>
{
    auto pending = _attachments->prepareResize(extent, colorFormat);
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
    if (const auto updated = _bindings->updateFrame(frameIndex, view, _attachments->getShadowView(frameIndex));
        !updated)
    {
        return updated;
    }
    return pipelineCache().prepare(view);
}

auto MeshRenderer::getSampleCount() const noexcept -> vk::SampleCountFlagBits
{
    return _attachments->getOutputDescription().sampleCount;
}

void MeshRenderer::record(vk::CommandBuffer command,
                          vk::ImageView colorView,
                          std::size_t frameIndex,
                          const RenderView& view)
{
    recordShadowPass(command, frameIndex, view);
    _attachments->transitionColor(command, frameIndex);
    _attachments->transitionDepth(command, frameIndex);
    recordAttachments(command, colorView, frameIndex);
    recordColorPass(command, frameIndex, view);
}

void MeshRenderer::recordShadowPass(vk::CommandBuffer command, std::size_t frameIndex, const RenderView& view)
{
    _attachments->transitionShadowForAttachment(command, frameIndex);
    recordShadowAttachment(command, _attachments->getShadowView(frameIndex));
    setDrawState(command, frameIndex, shadowExtent);
    auto materialIndex = std::uint32_t {};
    for (const auto& draw : view.meshes)
    {
        if (draw.material.alphaMode != AlphaMode::Blend)
        {
            recordDraw(command, frameIndex, draw, {.shadowPass = true, .materialIndex = materialIndex});
        }
        ++materialIndex;
    }
    command.endRendering();
    _attachments->transitionShadowForSampling(command, frameIndex);
}

void MeshRenderer::recordColorPass(vk::CommandBuffer command, std::size_t frameIndex, const RenderView& view)
{
    setDrawState(command, frameIndex, _attachments->getOutputDescription().extent);
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
            recordDraw(command, frameIndex, draw, {.shadowPass = false, .materialIndex = materialIndex});
        }
        ++materialIndex;
    }
    recordBlendedDraws(command, frameIndex);
    command.endRendering();
}

void MeshRenderer::recordBlendedDraws(vk::CommandBuffer command, std::size_t frameIndex)
{
    std::ranges::stable_sort(_blendDraws, [](const BlendDraw& left, const BlendDraw& right) {
        return left.cameraDepth > right.cameraDepth;
    });
    for (const auto& blendDraw : _blendDraws)
    {
        recordDraw(command, frameIndex, *blendDraw.draw, {.materialIndex = blendDraw.materialIndex});
    }
    _blendDraws.clear();
}

void MeshRenderer::recordAttachments(vk::CommandBuffer command,
                                     vk::ImageView swapchainView,
                                     std::size_t frameIndex) const
{
    const auto& output = _attachments->getOutputDescription();
    const auto multisampled = output.sampleCount != vk::SampleCountFlagBits::e1;
    auto color = vk::RenderingAttachmentInfo {
        .imageView = multisampled ? _attachments->getColorView(frameIndex) : swapchainView,
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = multisampled ? vk::AttachmentStoreOp::eDontCare : vk::AttachmentStoreOp::eStore,
        .clearValue = vk::ClearValue {.color = makeClearColor(output.colorFormat)}};
    if (multisampled)
    {
        color.resolveMode = vk::ResolveModeFlagBits::eAverage;
        color.resolveImageView = swapchainView;
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

void MeshRenderer::setDrawState(vk::CommandBuffer command, std::size_t frameIndex, vk::Extent2D extent) const
{
    command.setViewport(0,
                        vk::Viewport {.width = static_cast<float>(extent.width),
                                      .height = static_cast<float>(extent.height),
                                      .minDepth = 0.0F,
                                      .maxDepth = 1.0F});
    command.setScissor(0, vk::Rect2D {.extent = extent});
    const auto descriptorSet = _bindings->getFrameDescriptorSet(frameIndex);
    command.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                               _bindings->getPipelineLayout(),
                               PANDA_FRAME_DESCRIPTOR_SET,
                               1,
                               &descriptorSet,
                               0,
                               nullptr);
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
        command.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelineCache().getPipeline(draw));
    }
    bindTextureDescriptors(command, frameIndex, draw, pass);
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
                                   PANDA_TEXTURE_DESCRIPTOR_SET,
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
