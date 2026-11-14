// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan.hpp>
// clang-format on

#include "presentation/OffscreenRendering.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <memory>
#include <optional>
#include <panda/Buffer.hpp>
#include <panda/CompletionPoint.hpp>
#include <panda/Error.hpp>
#include <panda/FrameResult.hpp>
#include <panda/OffscreenTarget.hpp>
#include <panda/RenderView.hpp>
#include <utility>
#include <vector>

#include "presentation/FrameExecution.hpp"
#include "resources/BufferResource.hpp"
#include "resources/OffscreenTargetResource.hpp"

namespace panda::core::vulkan::presentation
{
namespace
{
constexpr auto readbackTimeoutNanoseconds = std::uint64_t {10'000'000'000};

auto pixelByteCount(vk::Extent2D extent) -> std::expected<std::uint64_t, Error>
{
    if (extent.width == 0 || extent.height == 0)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Offscreen readback extent must be nonzero")};
    }
    const auto rowBytes = std::uint64_t {extent.width} * 4;
    if (extent.height > std::numeric_limits<std::uint64_t>::max() / rowBytes ||
        rowBytes * extent.height > std::numeric_limits<std::size_t>::max())
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Offscreen readback extent is too large")};
    }
    return rowBytes * extent.height;
}

auto createReadbackCommands(const vk::raii::Device& device,
                            std::uint32_t queueFamily,
                            vk::raii::CommandPool& pool,
                            vk::CommandBuffer& command) -> Result<void>
{
    const auto poolInfo = VkCommandPoolCreateInfo {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                                                   .pNext = nullptr,
                                                   .flags = 0,
                                                   .queueFamilyIndex = queueFamily};
    auto* rawPool = VkCommandPool {};
    const auto created =
        device.getDispatcher()->vkCreateCommandPool(static_cast<VkDevice>(*device), &poolInfo, nullptr, &rawPool);
    if (created != VK_SUCCESS)
    {
        return std::unexpected {makeVulkanError(static_cast<vk::Result>(created))};
    }
    pool = vk::raii::CommandPool {device, rawPool};
    const auto allocation = vk::CommandBufferAllocateInfo {.commandPool = *pool,
                                                           .level = vk::CommandBufferLevel::ePrimary,
                                                           .commandBufferCount = 1};
    auto commands = (*device).allocateCommandBuffers(allocation, *device.getDispatcher());
    if (commands.result != vk::Result::eSuccess)
    {
        return std::unexpected {makeVulkanError(commands.result)};
    }
    command = commands.value.front();
    return {};
}

void transitionTargetToColor(vk::CommandBuffer command, const ::panda::detail::OffscreenTargetResource& target)
{
    const auto initialized = target.isInitialized();
    const auto barrier = vk::ImageMemoryBarrier2 {
        .srcStageMask =
            initialized ? vk::PipelineStageFlagBits2::eColorAttachmentOutput : vk::PipelineStageFlagBits2::eNone,
        .srcAccessMask = initialized ? vk::AccessFlagBits2::eColorAttachmentWrite : vk::AccessFlags2 {},
        .dstStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        .dstAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
        .oldLayout = initialized ? vk::ImageLayout::eColorAttachmentOptimal : vk::ImageLayout::eUndefined,
        .newLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
        .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
        .image = target.getImage(),
        .subresourceRange = vk::ImageSubresourceRange {.aspectMask = vk::ImageAspectFlagBits::eColor,
                                                                                                      .levelCount = 1,
                                                                                                      .layerCount = 1}
    };
    command.pipelineBarrier2(vk::DependencyInfo {.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier});
}

void transitionTargetToTransferSource(vk::CommandBuffer command, const ::panda::detail::OffscreenTargetResource& target)
{
    const auto barrier = vk::ImageMemoryBarrier2 {
        .srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        .srcAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eTransfer,
        .dstAccessMask = vk::AccessFlagBits2::eTransferRead,
        .oldLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .newLayout = vk::ImageLayout::eTransferSrcOptimal,
        .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
        .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
        .image = target.getImage(),
        .subresourceRange = vk::ImageSubresourceRange {.aspectMask = vk::ImageAspectFlagBits::eColor,
                                                       .levelCount = 1,
                                                       .layerCount = 1}
    };
    command.pipelineBarrier2(vk::DependencyInfo {.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier});
}

void transitionCaptureImageToTransferDestination(vk::CommandBuffer command, const WindowCaptureOutput& output)
{
    const auto barrier = vk::ImageMemoryBarrier2 {
        .srcStageMask = vk::PipelineStageFlagBits2::eNone,
        .dstStageMask = vk::PipelineStageFlagBits2::eTransfer,
        .dstAccessMask = vk::AccessFlagBits2::eTransferWrite,
        .oldLayout = output.initialized ? vk::ImageLayout::ePresentSrcKHR : vk::ImageLayout::eUndefined,
        .newLayout = vk::ImageLayout::eTransferDstOptimal,
        .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
        .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
        .image = output.image,
        .subresourceRange = vk::ImageSubresourceRange {.aspectMask = vk::ImageAspectFlagBits::eColor,
                                                       .levelCount = 1,
                                                       .layerCount = 1}
    };
    command.pipelineBarrier2(vk::DependencyInfo {.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier});
}

void transitionCapturedTargetToColor(vk::CommandBuffer command, const ::panda::detail::OffscreenTargetResource& target)
{
    const auto barrier = vk::ImageMemoryBarrier2 {
        .srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
        .srcAccessMask = vk::AccessFlagBits2::eTransferRead,
        .dstStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        .dstAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
        .oldLayout = vk::ImageLayout::eTransferSrcOptimal,
        .newLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
        .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
        .image = target.getImage(),
        .subresourceRange = vk::ImageSubresourceRange {.aspectMask = vk::ImageAspectFlagBits::eColor,
                                                       .levelCount = 1,
                                                       .layerCount = 1}
    };
    command.pipelineBarrier2(vk::DependencyInfo {.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier});
}

void transitionCaptureImageToPresent(vk::CommandBuffer command, const WindowCaptureOutput& output)
{
    const auto barrier = vk::ImageMemoryBarrier2 {
        .srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
        .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eNone,
        .oldLayout = vk::ImageLayout::eTransferDstOptimal,
        .newLayout = vk::ImageLayout::ePresentSrcKHR,
        .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
        .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
        .image = output.image,
        .subresourceRange = vk::ImageSubresourceRange {.aspectMask = vk::ImageAspectFlagBits::eColor,
                                                       .levelCount = 1,
                                                       .layerCount = 1}
    };
    command.pipelineBarrier2(vk::DependencyInfo {.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier});
}

void blitToWindow(vk::CommandBuffer command,
                  const ::panda::detail::OffscreenTargetResource& target,
                  const WindowCaptureOutput& output)
{
    const auto sourceOffsets = std::array {
        vk::Offset3D {},
        vk::Offset3D {.x = static_cast<std::int32_t>(target.getExtent().width),
                      .y = static_cast<std::int32_t>(target.getExtent().height),
                      .z = 1}
    };
    const auto destinationOffsets = std::array {
        vk::Offset3D {},
        vk::Offset3D {.x = static_cast<std::int32_t>(output.extent.width),
                      .y = static_cast<std::int32_t>(output.extent.height),
                      .z = 1}
    };
    const auto region = vk::ImageBlit {
        .srcSubresource = vk::ImageSubresourceLayers {.aspectMask = vk::ImageAspectFlagBits::eColor, .layerCount = 1},
        .srcOffsets = sourceOffsets,
        .dstSubresource = vk::ImageSubresourceLayers {.aspectMask = vk::ImageAspectFlagBits::eColor, .layerCount = 1},
        .dstOffsets = destinationOffsets
    };
    command.blitImage(target.getImage(),
                      vk::ImageLayout::eTransferSrcOptimal,
                      output.image,
                      vk::ImageLayout::eTransferDstOptimal,
                      region,
                      vk::Filter::eNearest);
}

void recordCaptureCopy(vk::CommandBuffer command,
                       const ::panda::detail::OffscreenTargetResource& target,
                       const WindowCaptureOutput& output)
{
    transitionTargetToTransferSource(command, target);
    transitionCaptureImageToTransferDestination(command, output);
    blitToWindow(command, target, output);
    transitionCapturedTargetToColor(command, target);
    transitionCaptureImageToPresent(command, output);
}

void recordPixelCopy(vk::CommandBuffer command,
                     const ::panda::detail::OffscreenTargetResource& target,
                     vk::Buffer buffer,
                     vk::Extent2D extent)
{
    transitionTargetToTransferSource(command, target);
    const auto region = vk::BufferImageCopy {
        .imageSubresource = vk::ImageSubresourceLayers {.aspectMask = vk::ImageAspectFlagBits::eColor, .layerCount = 1},
        .imageExtent = vk::Extent3D {.width = extent.width, .height = extent.height, .depth = 1}
    };
    command.copyImageToBuffer(target.getImage(), vk::ImageLayout::eTransferSrcOptimal, buffer, region);
    transitionCapturedTargetToColor(command, target);
    const auto bufferBarrier = vk::BufferMemoryBarrier2 {.srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
                                                         .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
                                                         .dstStageMask = vk::PipelineStageFlagBits2::eHost,
                                                         .dstAccessMask = vk::AccessFlagBits2::eHostRead,
                                                         .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
                                                         .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
                                                         .buffer = buffer,
                                                         .size = vk::WholeSize};
    command.pipelineBarrier2(
        vk::DependencyInfo {.bufferMemoryBarrierCount = 1, .pBufferMemoryBarriers = &bufferBarrier});
}

auto recordReadbackCommands(vk::CommandBuffer command,
                            const ::panda::detail::OffscreenTargetResource& target,
                            vk::Buffer buffer,
                            vk::Extent2D extent) -> Result<void>
{
    if (const auto begun = command.begin(vk::CommandBufferBeginInfo {}); begun != vk::Result::eSuccess)
    {
        return std::unexpected {makeVulkanError(begun)};
    }
    recordPixelCopy(command, target, buffer, extent);
    if (const auto ended = command.end(); ended != vk::Result::eSuccess)
    {
        return std::unexpected {makeVulkanError(ended)};
    }
    return {};
}

auto waitStatusResult(WaitStatus status) -> Result<void>
{
    if (status == WaitStatus::Timeout)
    {
        return std::unexpected {makeError(ErrorCode::Timeout, "Offscreen pixel readback timed out")};
    }
    return {};
}
}

OffscreenRendering::OffscreenRendering(const OffscreenRenderingCreateInfo& info)
    : _physicalDevice {info.physicalDevice},
      _device {info.device},
      _queueFamily {info.queueFamily},
      _submissions {info.submissions},
      _pool {info.pool},
      _performance {info.performance},
      _maxLights {info.maxLights}
{
}

OffscreenRendering::~OffscreenRendering() = default;

auto OffscreenRendering::create(const OffscreenRenderingCreateInfo& info)
    -> std::expected<std::unique_ptr<OffscreenRendering>, Error>
{
    auto rendering = std::unique_ptr<OffscreenRendering> {new OffscreenRendering {info}};
    if (const auto initialized = rendering->initialize(); !initialized)
    {
        return std::unexpected {initialized.error()};
    }
    return rendering;
}

auto OffscreenRendering::initialize() -> std::expected<void, Error>
{
    auto execution = FrameExecution::create({.physicalDevice = _physicalDevice,
                                             .device = _device,
                                             .queueFamily = _queueFamily,
                                             .submissions = _submissions,
                                             .pool = &_pool,
                                             .performance = _performance,
                                             .maxLights = _maxLights,
                                             .performanceSlotBase = FrameExecution::slotCount});
    if (!execution)
    {
        return std::unexpected {execution.error()};
    }
    _execution = std::move(*execution);
    return {};
}

auto OffscreenRendering::prepareRendererOutput(const ::panda::detail::OffscreenTargetResource& target,
                                               const RenderView& view) -> std::expected<void, Error>
{
    if (view.meshes.empty())
    {
        return {};
    }
    const auto extent = target.getExtent();
    const auto format = target.getFormat();
    if (_rendererExtent && (_rendererExtent->width != extent.width || _rendererExtent->height != extent.height ||
                            _rendererFormat != format))
    {
        return _execution->drain();
    }
    return {};
}

auto OffscreenRendering::recordFrame(std::size_t slotIndex,
                                     const std::shared_ptr<::panda::detail::OffscreenTargetResource>& target,
                                     const FrameRequest& request) -> std::expected<void, Error>
{
    if (const auto begun = _execution->beginCommands(slotIndex); !begun)
    {
        return std::unexpected {begun.error()};
    }
    const auto command = _execution->command(slotIndex);
    const auto output = FrameOutput {.imageView = target->getImageView(),
                                     .extent = target->getExtent(),
                                     .colorFormat = target->getFormat()};
    transitionTargetToColor(command, *target);
    if (const auto recorded = _execution->recordContent(slotIndex, output, request.view); !recorded)
    {
        return std::unexpected {recorded.error()};
    }
    if (request.windowOutput != nullptr)
    {
        recordCaptureCopy(command, *target, *request.windowOutput);
    }
    return _execution->endCommands(slotIndex);
}

auto OffscreenRendering::render(const RenderView& view,
                                const OffscreenTarget& target,
                                std::optional<std::size_t>& submittedPerformanceSlot)
    -> std::expected<FrameResult, Error>
{
    submittedPerformanceSlot.reset();
    const auto request =
        FrameRequest {.view = view, .target = target, .submittedPerformanceSlot = submittedPerformanceSlot};
    return renderInternal(request);
}

auto OffscreenRendering::renderWithCapture(const WindowCaptureRequest& request) -> std::expected<FrameResult, Error>
{
    if (!request.output.image || !request.acquire || !request.finished)
    {
        return std::unexpected {
            makeError(ErrorCode::InvalidArgument, "Capture needs a swapchain image and semaphores")};
    }
    const auto sourceFeatures = _physicalDevice.getFormatProperties(request.target.getFormat()).optimalTilingFeatures;
    const auto destinationFeatures = _physicalDevice.getFormatProperties(request.output.format).optimalTilingFeatures;
    if (!(sourceFeatures & vk::FormatFeatureFlagBits::eBlitSrc) ||
        !(destinationFeatures & vk::FormatFeatureFlagBits::eBlitDst))
    {
        return std::unexpected {
            makeError(ErrorCode::Unsupported, "Device formats do not support window capture blits")};
    }
    request.submittedPerformanceSlot.reset();
    const auto frameRequest = FrameRequest {.view = request.view,
                                            .target = request.target,
                                            .submittedPerformanceSlot = request.submittedPerformanceSlot,
                                            .windowOutput = &request.output,
                                            .acquire = request.acquire,
                                            .finished = request.finished,
                                            .submittedValue = &request.submittedValue};
    return renderInternal(frameRequest);
}

auto OffscreenRendering::renderInternal(const FrameRequest& request) -> std::expected<FrameResult, Error>
{
    if (const auto pending = waitPendingReadback(); !pending)
    {
        return std::unexpected {pending.error()};
    }
    const auto targetResource = ::panda::detail::OffscreenTargetResource::getResource(request.target);
    const auto slotIndex = _nextSlot;
    if (const auto ready = prepareRendererOutput(*targetResource, request.view); !ready)
    {
        return std::unexpected {ready.error()};
    }
    if (const auto prepared = _execution->prepareSlot(slotIndex, request.view, targetResource); !prepared)
    {
        return std::unexpected {prepared.error()};
    }
    if (const auto recorded = recordFrame(slotIndex, targetResource, request); !recorded)
    {
        _execution->discardUnsubmittedInputs(slotIndex);
        return std::unexpected {recorded.error()};
    }
    return submitFrame(slotIndex, targetResource, request);
}

auto OffscreenRendering::submitFrame(std::size_t slotIndex,
                                     const std::shared_ptr<::panda::detail::OffscreenTargetResource>& target,
                                     const FrameRequest& request) -> std::expected<FrameResult, Error>
{
    const auto submitted = _submissions.submit(_execution->command(slotIndex), request.acquire, request.finished);
    if (!submitted)
    {
        _execution->discardUnsubmittedInputs(slotIndex);
        return std::unexpected {submitted.error()};
    }
    target->markRendered(*submitted);
    if (request.submittedValue != nullptr)
    {
        *request.submittedValue = *submitted;
    }
    _execution->markSubmitted(slotIndex, *submitted, !request.view.meshes.empty());
    if (_execution->isGpuTimestampSamplingEnabled())
    {
        request.submittedPerformanceSlot = FrameExecution::slotCount + slotIndex;
    }
    if (!request.view.meshes.empty())
    {
        _rendererExtent = target->getExtent();
        _rendererFormat = target->getFormat();
    }
    _nextSlot = (_nextSlot + 1) % FrameExecution::slotCount;
    return FrameResult {.status = FrameResult::Status::Rendered,
                        .completion = _submissions.getPoint(*submitted),
                        .effectiveSampleCount = _execution->getSampleCount()};
}

auto OffscreenRendering::readPixels(const OffscreenTarget& target, CompletionPoint completion)
    -> std::expected<ImageReadback, Error>
{
    if (const auto pending = waitPendingReadback(); !pending)
    {
        return std::unexpected {pending.error()};
    }
    const auto targetResource = ::panda::detail::OffscreenTargetResource::getResource(target);
    if (!targetResource->isInitialized())
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Offscreen target has not been rendered")};
    }
    if (const auto covered =
            _submissions.validateCompletionCoversRender(completion, targetResource->getLastRenderCompletion());
        !covered)
    {
        return std::unexpected {covered.error()};
    }
    return submitReadback(targetResource);
}

auto OffscreenRendering::submitReadback(const std::shared_ptr<::panda::detail::OffscreenTargetResource>& target)
    -> std::expected<ImageReadback, Error>
{
    const auto extent = target->getExtent();
    const auto size = pixelByteCount(extent);
    if (!size)
    {
        return std::unexpected {size.error()};
    }
    auto buffer = ::panda::detail::BufferResource::create(
        _device,
        _pool,
        {.size = *size, .usage = vk::BufferUsageFlagBits::eTransferDst, .memory = BufferMemory::Readback});
    if (!buffer)
    {
        return std::unexpected {buffer.error()};
    }
    if (const auto submitted = submitReadbackCommands(target, std::move(*buffer), extent); !submitted)
    {
        return std::unexpected {submitted.error()};
    }
    return collectPendingReadback();
}

auto OffscreenRendering::submitReadbackCommands(const std::shared_ptr<::panda::detail::OffscreenTargetResource>& target,
                                                std::shared_ptr<::panda::detail::BufferResource> buffer,
                                                vk::Extent2D extent) -> std::expected<void, Error>
{
    auto pool = vk::raii::CommandPool {nullptr};
    auto command = vk::CommandBuffer {};
    if (const auto created = createReadbackCommands(_device, _queueFamily, pool, command); !created)
    {
        return std::unexpected {created.error()};
    }
    if (const auto recorded = recordReadbackCommands(command, *target, buffer->getNative(), extent); !recorded)
    {
        return std::unexpected {recorded.error()};
    }
    const auto submitted = _submissions.submit(command, {}, {});
    if (!submitted)
    {
        return std::unexpected {submitted.error()};
    }
    _pendingReadback.emplace(PendingReadback {.commandPool = std::move(pool),
                                              .command = command,
                                              .buffer = std::move(buffer),
                                              .target = target,
                                              .extent = extent,
                                              .completion = *submitted});
    return {};
}

auto OffscreenRendering::collectPendingReadback() -> std::expected<ImageReadback, Error>
{
    if (!_pendingReadback)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "No offscreen readback is pending")};
    }
    const auto waited = _submissions.waitValue(_pendingReadback->completion, readbackTimeoutNanoseconds);
    if (!waited)
    {
        return std::unexpected {waited.error()};
    }
    if (const auto completed = waitStatusResult(*waited); !completed)
    {
        return std::unexpected {completed.error()};
    }
    auto pending = std::move(*_pendingReadback);
    _pendingReadback.reset();
    return copyReadbackBytes(*pending.buffer, pending.extent);
}

auto OffscreenRendering::copyReadbackBytes(const ::panda::detail::BufferResource& buffer, vk::Extent2D extent)
    -> std::expected<ImageReadback, Error>
{
    const auto size = pixelByteCount(extent);
    if (!size)
    {
        return std::unexpected {size.error()};
    }
    if (const auto invalidated = buffer.invalidate(0, *size); !invalidated)
    {
        return std::unexpected {invalidated.error()};
    }
    const auto mapped = buffer.getMappedBytes();
    if (!mapped)
    {
        return std::unexpected {mapped.error()};
    }
    auto pixels = ImageReadback {.extent = extent, .rgba = std::vector<std::uint8_t>(static_cast<std::size_t>(*size))};
    std::ranges::transform(*mapped, pixels.rgba.begin(), [](std::byte value) {
        return std::to_integer<std::uint8_t>(value);
    });
    return pixels;
}

auto OffscreenRendering::waitPendingReadback() -> std::expected<void, Error>
{
    if (!_pendingReadback)
    {
        return {};
    }
    const auto waited = _submissions.waitValue(_pendingReadback->completion, readbackTimeoutNanoseconds);
    if (!waited)
    {
        return std::unexpected {waited.error()};
    }
    if (const auto completed = waitStatusResult(*waited); !completed)
    {
        return std::unexpected {completed.error()};
    }
    _pendingReadback.reset();
    return {};
}

auto OffscreenRendering::drain() -> std::expected<void, Error>
{
    if (const auto pending = waitPendingReadback(); !pending)
    {
        return pending;
    }
    return _execution->drain();
}
}
