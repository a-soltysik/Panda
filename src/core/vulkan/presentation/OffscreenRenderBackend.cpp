// clang-format off
#include <panda/base/Error.hpp>
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan.hpp>
// clang-format on

#include "presentation/OffscreenRenderBackend.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <memory>
#include <optional>
#include <panda/rendering/FrameResult.hpp>
#include <panda/rendering/RenderView.hpp>
#include <panda/rendering/SampleCount.hpp>
#include <panda/resources/Buffer.hpp>
#include <panda/resources/OffscreenTarget.hpp>
#include <panda/submission/CompletionPoint.hpp>
#include <panda/vulkan/Error.hpp>
#include <utility>
#include <vector>

#include "presentation/FrameExecution.hpp"
#include "renderer/RenderAttachments.hpp"
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

void transitionCaptureImageToColorAttachment(vk::CommandBuffer command, const WindowCaptureOutput& output)
{
    const auto barrier = vk::ImageMemoryBarrier2 {
        .srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        .dstStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        .dstAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
        .oldLayout = output.initialized ? vk::ImageLayout::ePresentSrcKHR : vk::ImageLayout::eUndefined,
        .newLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
        .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
        .image = output.image,
        .subresourceRange = vk::ImageSubresourceRange {.aspectMask = vk::ImageAspectFlagBits::eColor,
                                                       .levelCount = 1,
                                                       .layerCount = 1}
    };
    command.pipelineBarrier2(vk::DependencyInfo {.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier});
}

void transitionTargetBackToColor(vk::CommandBuffer command, const ::panda::detail::OffscreenTargetResource& target)
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
        .srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        .srcAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eNone,
        .oldLayout = vk::ImageLayout::eColorAttachmentOptimal,
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
    transitionTargetBackToColor(command, target);
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

OffscreenRenderBackend::OffscreenRenderBackend(const OffscreenRenderBackendCreateInfo& info)
    : _physicalDevice {info.physicalDevice},
      _device {info.device},
      _queueFamily {info.queueFamily},
      _submissions {info.submissions},
      _pool {info.pool},
      _performance {info.performance},
      _textureFilteringPreset {info.textureFilteringPreset},
      _maxLights {info.maxLights}
{
}

OffscreenRenderBackend::~OffscreenRenderBackend() = default;

auto OffscreenRenderBackend::initialize() -> std::expected<void, Error>
{
    if (_execution)
    {
        return {};
    }
    auto execution = FrameExecution::create({.physicalDevice = _physicalDevice,
                                             .device = _device,
                                             .queueFamily = _queueFamily,
                                             .submissions = _submissions,
                                             .pool = &_pool,
                                             .performance = _performance,
                                             .textureFilteringPreset = _textureFilteringPreset,
                                             .maxLights = _maxLights,
                                             .performanceSlotBase = FrameExecution::slotCount});
    if (!execution)
    {
        return std::unexpected {execution.error()};
    }
    _execution = std::move(*execution);
    return {};
}

auto OffscreenRenderBackend::getSupportedSampleCounts() const -> Result<std::vector<SampleCount>>
{
    return renderer::RenderAttachments::getSupportedSampleCounts(_physicalDevice, renderer::hdrColorFormat);
}

auto OffscreenRenderBackend::recordFrame(std::size_t slotIndex,
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
    auto windowOutput = std::optional<FrameOutput> {};
    if (request.windowOutput != nullptr)
    {
        transitionCaptureImageToColorAttachment(command, *request.windowOutput);
        windowOutput = FrameOutput {.imageView = request.windowOutput->imageView,
                                    .extent = request.windowOutput->extent,
                                    .colorFormat = request.windowOutput->format};
    }
    return _execution->recordContent(slotIndex, output, request.view, windowOutput ? &*windowOutput : nullptr)
        .and_then([&] {
            if (request.windowOutput != nullptr)
            {
                transitionCaptureImageToPresent(command, *request.windowOutput);
            }
            return _execution->endCommands(slotIndex);
        });
}

auto OffscreenRenderBackend::render(const RenderView& view,
                                    const OffscreenTarget& target,
                                    std::optional<std::size_t>& submittedPerformanceSlot)
    -> std::expected<FrameResult, Error>
{
    submittedPerformanceSlot.reset();
    const auto request =
        FrameRequest {.view = view, .target = target, .submittedPerformanceSlot = submittedPerformanceSlot};
    return renderInternal(request);
}

auto OffscreenRenderBackend::renderWithCapture(const WindowCaptureRequest& request) -> std::expected<FrameResult, Error>
{
    if (!request.output.image || !request.output.imageView || !request.acquire || !request.finished)
    {
        return std::unexpected {
            makeError(ErrorCode::InvalidArgument, "Capture needs a swapchain image and semaphores")};
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

auto OffscreenRenderBackend::renderInternal(const FrameRequest& request) -> std::expected<FrameResult, Error>
{
    if (const auto pending = waitPendingReadback(); !pending)
    {
        return std::unexpected {pending.error()};
    }
    const auto targetResource = ::panda::detail::OffscreenTargetResource::getResource(request.target);
    const auto slotIndex = _nextSlot;
    if (const auto ready = _execution->prepareOutput(
            targetResource->getExtent(),
            targetResource->getFormat(),
            request.view,
            request.windowOutput != nullptr ? std::optional {request.windowOutput->format} : std::nullopt);
        !ready)
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

auto OffscreenRenderBackend::submitFrame(std::size_t slotIndex,
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
    _execution->markSubmitted(slotIndex, *submitted, !request.view.meshes.empty() || request.view.skybox.has_value());
    if (_execution->isGpuTimestampSamplingEnabled())
    {
        request.submittedPerformanceSlot = FrameExecution::slotCount + slotIndex;
    }
    _nextSlot = (_nextSlot + 1) % FrameExecution::slotCount;
    return FrameResult {.status = FrameResult::Status::Rendered,
                        .completion = _submissions.getPoint(*submitted),
                        .effectiveSampleCount = _execution->getSampleCount(request.view),
                        .effectiveAnisotropy = _execution->getEffectiveAnisotropy(request.view)};
}

auto OffscreenRenderBackend::readPixels(const OffscreenTarget& target, CompletionPoint completion)
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

auto OffscreenRenderBackend::submitReadback(const std::shared_ptr<::panda::detail::OffscreenTargetResource>& target)
    -> std::expected<ImageReadback, Error>
{
    const auto extent = target->getExtent();
    const auto size = pixelByteCount(extent);
    if (!size)
    {
        return std::unexpected {size.error()};
    }
    auto buffer = ::panda::detail::BufferResource::createNative(
        _device,
        _pool,
        ::panda::detail::BufferResourceCreateInfo {.size = *size,
                                                   .usage = vk::BufferUsageFlagBits::eTransferDst,
                                                   .memory = BufferMemory::Readback});
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

auto OffscreenRenderBackend::submitReadbackCommands(
    const std::shared_ptr<::panda::detail::OffscreenTargetResource>& target,
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

auto OffscreenRenderBackend::collectPendingReadback() -> std::expected<ImageReadback, Error>
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

auto OffscreenRenderBackend::copyReadbackBytes(const ::panda::detail::BufferResource& buffer, vk::Extent2D extent)
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
    auto pixels = ImageReadback {
        .extent = {extent.width, extent.height},
        .rgba = std::vector<std::uint8_t>(static_cast<std::size_t>(*size))
    };
    std::ranges::transform(*mapped, pixels.rgba.begin(), [](std::byte value) {
        return std::to_integer<std::uint8_t>(value);
    });
    return pixels;
}

auto OffscreenRenderBackend::waitPendingReadback() -> std::expected<void, Error>
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

auto OffscreenRenderBackend::drain() -> std::expected<void, Error>
{
    if (const auto pending = waitPendingReadback(); !pending)
    {
        return pending;
    }
    return _execution ? _execution->drain() : std::expected<void, Error> {};
}
}
