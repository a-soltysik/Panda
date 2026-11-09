// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include "renderer/MeshRenderer.hpp"

#include <algorithm>
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

#include "presentation/SwapchainGeneration.hpp"
#include "renderer/FrameBindings.hpp"
#include "renderer/MeshPipelineCache.hpp"
#include "renderer/ShaderData.hpp"
#include "renderer/ShaderInterface.inc"
#include "resources/MeshResource.hpp"
#include "resources/TextureResource.hpp"

namespace panda::core::vulkan::renderer
{
namespace
{
constexpr auto shadowExtent = vk::Extent2D {.width = 2048, .height = 2048};

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

}

MeshRenderer::MeshRenderer(const MeshRendererCreateInfo& info)
    : _physicalDevice {info.physicalDevice},
      _device {info.device},
      _pool {info.pool},
      _frames(info.frameCount)
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
    if (const auto supported = FrameBindings::validateDeviceLimits(_physicalDevice); !supported)
    {
        return supported;
    }
    return initializeDepthAndPipeline(extent, colorFormat);
}

auto MeshRenderer::initializeDepthAndPipeline(vk::Extent2D extent, vk::Format colorFormat) -> std::expected<void, Error>
{
    if (const auto attachments = initializeAttachments(extent, colorFormat); !attachments)
    {
        return attachments;
    }
    return initializeBindingsAndPipeline();
}

auto MeshRenderer::initializeAttachments(vk::Extent2D extent, vk::Format colorFormat) -> std::expected<void, Error>
{
    const auto selectedDepthFormat = chooseDepthFormat();
    if (!selectedDepthFormat)
    {
        return std::unexpected {selectedDepthFormat.error()};
    }
    _depthFormat = *selectedDepthFormat;
    if (const auto shadowMaps = initializeShadowMaps(); !shadowMaps)
    {
        return shadowMaps;
    }
    if (const auto resized = resize(extent, colorFormat); !resized)
    {
        return resized;
    }
    return {};
}

auto MeshRenderer::initializeBindingsAndPipeline() -> std::expected<void, Error>
{
    auto bindings = FrameBindings::create(_physicalDevice, _device, _pool, _frames.size());
    if (!bindings)
    {
        return std::unexpected {bindings.error()};
    }
    _bindings = std::move(*bindings);
    _pipelineCache.emplace(_device, _bindings->getPipelineLayout());
    pipelineCache().setOutputCompatibility(
        {.colorFormat = _colorFormat, .depthFormat = _depthFormat, .sampleCount = _sampleCount});
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
    _colorFormat = request.colorFormat;
    _sampleCount = request.sampleCount;
    _extent = request.extent;
    if (_pipelineCache)
    {
        pipelineCache().setOutputCompatibility(
            {.colorFormat = _colorFormat, .depthFormat = _depthFormat, .sampleCount = _sampleCount});
    }
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

auto MeshRenderer::updateFrame(std::size_t frameIndex, const RenderView& view) -> std::expected<void, Error>
{
    auto& frame = _frames.at(frameIndex);
    if (const auto updated = _bindings->updateFrame(frameIndex, view, frame.shadowMap->getNativeView()); !updated)
    {
        return updated;
    }
    return pipelineCache().prepare(view);
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
    recordShadowPass(command, frame, frameIndex, view);
    transitionColor(command, frame);
    transitionDepth(command, frame);
    recordAttachments(command, colorView, frame, frame.depthAttachment->getNativeView());
    recordColorPass(command, frameIndex, view);
}

void MeshRenderer::recordShadowPass(vk::CommandBuffer command,
                                    FrameResources& frame,
                                    std::size_t frameIndex,
                                    const RenderView& view)
{
    transitionShadowForAttachment(command, frame);
    recordShadowAttachment(command, frame.shadowMap->getNativeView());
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
    transitionShadowForSampling(command, frame);
}

void MeshRenderer::recordColorPass(vk::CommandBuffer command, std::size_t frameIndex, const RenderView& view)
{
    setDrawState(command, frameIndex, _extent);
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
    _frames[frameIndex].colorInitialized = _sampleCount != vk::SampleCountFlagBits::e1;
    _frames[frameIndex].depthInitialized = true;
    _frames[frameIndex].shadowInitialized = true;
}
}
