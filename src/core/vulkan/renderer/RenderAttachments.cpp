// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include "renderer/RenderAttachments.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <memory>
#include <panda/Error.hpp>
#include <utility>
#include <vector>

#include "resources/TextureResource.hpp"
#include "resources/memory/MemoryPool.hpp"

namespace panda::core::vulkan::renderer
{
namespace
{
auto depthFormatSupported(vk::PhysicalDevice physicalDevice, vk::Format format) -> std::expected<bool, Error>
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

auto depthAttachmentsChanged(const OutputDescription& current, const OutputDescription& next) -> bool
{
    return current.extent.width != next.extent.width || current.extent.height != next.extent.height ||
           current.depthFormat != next.depthFormat || current.sampleCount != next.sampleCount;
}

auto colorAttachmentsChanged(const OutputDescription& current, const OutputDescription& next) -> bool
{
    return current.extent.width != next.extent.width || current.extent.height != next.extent.height ||
           current.colorFormat != next.colorFormat || current.sampleCount != next.sampleCount;
}
}

RenderAttachments::RenderAttachments(vk::PhysicalDevice physicalDevice,
                                     const vk::raii::Device& device,
                                     resources::MemoryPool& pool,
                                     std::size_t frameCount)
    : _physicalDevice {physicalDevice},
      _device {device},
      _pool {pool},
      _frames(frameCount)
{
}

RenderAttachments::~RenderAttachments() = default;

auto RenderAttachments::create(const RenderAttachmentsCreateInfo& info)
    -> std::expected<std::unique_ptr<RenderAttachments>, Error>
{
    if (info.frameCount == 0 || info.frameCount > std::numeric_limits<std::uint32_t>::max() || info.extent.width == 0 ||
        info.extent.height == 0 || info.colorFormat == vk::Format::eUndefined)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Invalid render attachment creation parameters")};
    }
    auto attachments = std::unique_ptr<RenderAttachments> {
        new RenderAttachments {info.physicalDevice, info.device, info.pool, info.frameCount}
    };
    if (const auto initialized = attachments->initialize(info.extent, info.colorFormat); !initialized)
    {
        return std::unexpected {initialized.error()};
    }
    return attachments;
}

auto RenderAttachments::initialize(vk::Extent2D extent, vk::Format colorFormat) -> std::expected<void, Error>
{
    const auto depthFormat = chooseDepthFormat();
    if (!depthFormat)
    {
        return std::unexpected {depthFormat.error()};
    }
    _description.depthFormat = *depthFormat;
    if (const auto shadows = initializeShadowMaps(); !shadows)
    {
        return shadows;
    }
    auto resize = prepareResize(extent, colorFormat);
    if (!resize)
    {
        return std::unexpected {resize.error()};
    }
    publishResize(std::move(*resize));
    return {};
}

auto RenderAttachments::initializeShadowMaps() -> std::expected<void, Error>
{
    auto maps = createShadowMaps();
    if (!maps)
    {
        return std::unexpected {maps.error()};
    }
    for (auto index = std::size_t {}; index < _frames.size(); ++index)
    {
        _frames.at(index).shadowMap = std::move(maps->at(index));
    }
    return {};
}

auto RenderAttachments::createDepthAttachments(vk::Extent2D extent, vk::SampleCountFlagBits samples)
    -> std::expected<std::vector<std::shared_ptr<::panda::detail::TextureResource>>, Error>
{
    auto attachments = std::vector<std::shared_ptr<::panda::detail::TextureResource>> {};
    attachments.reserve(_frames.size());
    for (auto index = std::size_t {}; index < _frames.size(); ++index)
    {
        auto attachment =
            ::panda::detail::TextureResource::create(_device,
                                                     _pool,
                                                     {.extent = extent,
                                                      .format = _description.depthFormat,
                                                      .usage = vk::ImageUsageFlagBits::eDepthStencilAttachment},
                                                     samples);
        if (!attachment)
        {
            return std::unexpected {attachment.error()};
        }
        attachments.push_back(std::move(*attachment));
    }
    return attachments;
}

auto RenderAttachments::createColorAttachments(vk::Extent2D extent, vk::Format format, vk::SampleCountFlagBits samples)
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

auto RenderAttachments::createShadowMaps()
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
             .format = _description.depthFormat,
             .usage = vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled});
        if (!map)
        {
            return std::unexpected {map.error()};
        }
        maps.push_back(std::move(*map));
    }
    return maps;
}

auto RenderAttachments::chooseDepthFormat() const -> std::expected<vk::Format, Error>
{
    for (const auto format : {vk::Format::eD32Sfloat, vk::Format::eD16Unorm})
    {
        const auto supported = depthFormatSupported(_physicalDevice, format);
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

auto RenderAttachments::chooseSampleCount(vk::Format colorFormat) const -> std::expected<vk::SampleCountFlagBits, Error>
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
                                                _description.depthFormat,
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

auto RenderAttachments::getOutputDescription() const noexcept -> const OutputDescription&
{
    return _description;
}

auto RenderAttachments::getShadowView(std::size_t frameIndex) const -> vk::ImageView
{
    return _frames.at(frameIndex).shadowMap->getNativeView();
}

auto RenderAttachments::getDepthView(std::size_t frameIndex) const -> vk::ImageView
{
    return _frames.at(frameIndex).depthAttachment->getNativeView();
}

auto RenderAttachments::getColorView(std::size_t frameIndex) const -> vk::ImageView
{
    const auto& attachment = _frames.at(frameIndex).colorAttachment;
    return attachment ? attachment->getNativeView() : vk::ImageView {};
}

auto RenderAttachments::prepareResize(vk::Extent2D extent, vk::Format colorFormat)
    -> std::expected<PendingResize, Error>
{
    if (extent.width == 0 || extent.height == 0 || colorFormat == vk::Format::eUndefined)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Invalid renderer output extent or format")};
    }
    const auto extentChanged = extent.width != _description.extent.width || extent.height != _description.extent.height;
    const auto formatChanged = colorFormat != _description.colorFormat;
    if (!extentChanged && !formatChanged)
    {
        return PendingResize {.description = _description, .colorAttachments = {}, .depthAttachments = {}};
    }
    const auto sampleCount = chooseSampleCount(colorFormat);
    if (!sampleCount)
    {
        return std::unexpected {sampleCount.error()};
    }
    const auto description = OutputDescription {.extent = extent,
                                                .colorFormat = colorFormat,
                                                .depthFormat = _description.depthFormat,
                                                .sampleCount = *sampleCount};
    return createResizeAttachments(description, extentChanged, formatChanged);
}

auto RenderAttachments::createResizeAttachments(const OutputDescription& description,
                                                bool extentChanged,
                                                bool formatChanged) -> std::expected<PendingResize, Error>
{
    auto depthAttachments = std::vector<std::shared_ptr<::panda::detail::TextureResource>> {};
    if (extentChanged || description.sampleCount != _description.sampleCount)
    {
        auto created = createDepthAttachments(description.extent, description.sampleCount);
        if (!created)
        {
            return std::unexpected {created.error()};
        }
        depthAttachments = std::move(*created);
    }
    auto colorAttachments = std::vector<std::shared_ptr<::panda::detail::TextureResource>> {};
    if (extentChanged || formatChanged || description.sampleCount != _description.sampleCount)
    {
        auto created = createColorAttachments(description.extent, description.colorFormat, description.sampleCount);
        if (!created)
        {
            return std::unexpected {created.error()};
        }
        colorAttachments = std::move(*created);
    }
    return PendingResize {.description = description,
                          .colorAttachments = std::move(colorAttachments),
                          .depthAttachments = std::move(depthAttachments)};
}

void RenderAttachments::publishResize(PendingResize resize) noexcept
{
    if (depthAttachmentsChanged(_description, resize.description))
    {
        replaceDepthAttachments(std::move(resize.depthAttachments));
    }
    if (colorAttachmentsChanged(_description, resize.description))
    {
        replaceColorAttachments(std::move(resize.colorAttachments));
    }
    _description = resize.description;
}

void RenderAttachments::replaceDepthAttachments(
    std::vector<std::shared_ptr<::panda::detail::TextureResource>> replacements) noexcept
{
    for (auto index = std::size_t {}; index < _frames.size(); ++index)
    {
        auto& frame = _frames.at(index);
        frame.depthAttachment = std::move(replacements.at(index));
        frame.depthInitialized = false;
    }
}

void RenderAttachments::replaceColorAttachments(
    std::vector<std::shared_ptr<::panda::detail::TextureResource>> replacements) noexcept
{
    for (auto index = std::size_t {}; index < _frames.size(); ++index)
    {
        auto& frame = _frames.at(index);
        frame.colorAttachment = replacements.empty() ? nullptr : std::move(replacements.at(index));
        frame.colorInitialized = false;
    }
}

void RenderAttachments::transitionColor(vk::CommandBuffer command, std::size_t frameIndex) const
{
    if (_description.sampleCount == vk::SampleCountFlagBits::e1)
    {
        return;
    }
    const auto& frame = _frames.at(frameIndex);
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

void RenderAttachments::transitionDepth(vk::CommandBuffer command, std::size_t frameIndex) const
{
    const auto& frame = _frames.at(frameIndex);
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

void RenderAttachments::transitionShadowForAttachment(vk::CommandBuffer command, std::size_t frameIndex) const
{
    const auto& frame = _frames.at(frameIndex);
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

void RenderAttachments::transitionShadowForSampling(vk::CommandBuffer command, std::size_t frameIndex) const
{
    const auto& frame = _frames.at(frameIndex);
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

void RenderAttachments::markSubmitted(std::size_t frameIndex) noexcept
{
    auto& frame = _frames.at(frameIndex);
    frame.colorInitialized = _description.sampleCount != vk::SampleCountFlagBits::e1;
    frame.depthInitialized = true;
    frame.shadowInitialized = true;
}
}
