#include "presentation/Presentation.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <numeric>
#include <optional>
#include <panda/Assert.hpp>
#include <panda/CompletionPoint.hpp>
#include <panda/Context.hpp>
#include <panda/Error.hpp>
#include <panda/WindowSurface.hpp>
#include <string>
#include <utility>
#include <vulkan/vulkan.hpp>

#include "SubmissionQueue.hpp"
#include "VulkanError.hpp"
#include "VulkanHpp.hpp"  // IWYU pragma: keep
#include "presentation/SwapchainGeneration.hpp"

namespace panda::core::vulkan
{
using detail::checkedCreation;

namespace
{
struct FrameCommands
{
    vk::raii::CommandPool pool;
    vk::CommandBuffer command;
};

struct ImageAccessScope
{
    vk::PipelineStageFlags2 stage;
    vk::AccessFlags2 access;
};

struct ImageTransition
{
    vk::ImageLayout oldLayout;
    vk::ImageLayout newLayout;
    ImageAccessScope source;
    ImageAccessScope destination;
};

auto createFrameCommands(const vk::raii::Device& device, std::uint32_t queueFamily)
    -> std::expected<FrameCommands, Error>
{
    const auto poolInfo = vk::CommandPoolCreateInfo {.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
                                                     .queueFamilyIndex = queueFamily};
    auto pool = checkedCreation(device.createCommandPool(poolInfo));
    if (!pool)
    {
        return std::unexpected {std::move(pool.error())};
    }
    const auto allocateInfo = vk::CommandBufferAllocateInfo {.commandPool = **pool,
                                                             .level = vk::CommandBufferLevel::ePrimary,
                                                             .commandBufferCount = 1};
    auto commands = (*device).allocateCommandBuffers(allocateInfo, *device.getDispatcher());
    if (commands.result != vk::Result::eSuccess)
    {
        return std::unexpected {makeVulkanError(commands.result)};
    }
    return FrameCommands {.pool = std::move(*pool), .command = commands.value.front()};
}

auto makeImageBarrier(const SwapchainImage& image, ImageTransition transition) -> vk::ImageMemoryBarrier2
{
    return vk::ImageMemoryBarrier2 {
        .srcStageMask = transition.source.stage,
        .srcAccessMask = transition.source.access,
        .dstStageMask = transition.destination.stage,
        .dstAccessMask = transition.destination.access,
        .oldLayout = transition.oldLayout,
        .newLayout = transition.newLayout,
        .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
        .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
        .image = image.image,
        .subresourceRange = vk::ImageSubresourceRange {.aspectMask = vk::ImageAspectFlagBits::eColor,
                                                       .levelCount = 1,
                                                       .layerCount = 1}
    };
}

void recordImageBarrier(vk::CommandBuffer command, const vk::ImageMemoryBarrier2& barrier)
{
    command.pipelineBarrier2(vk::DependencyInfo {.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier});
}

void transitionToColorAttachment(vk::CommandBuffer command, const SwapchainImage& image)
{
    const auto transition = ImageTransition {
        .oldLayout = image.initialized ? vk::ImageLayout::ePresentSrcKHR : vk::ImageLayout::eUndefined,
        .newLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .source = {.stage = vk::PipelineStageFlagBits2::eColorAttachmentOutput, .access = vk::AccessFlagBits2::eNone},
        .destination = {.stage = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                   .access = vk::AccessFlagBits2::eColorAttachmentWrite                                             }
    };
    const auto barrier = makeImageBarrier(image, transition);
    recordImageBarrier(command, barrier);
}

void recordClearRendering(vk::CommandBuffer command,
                          const SwapchainImage& image,
                          vk::Format format,
                          vk::Extent2D extent)
{
    const auto attachment = vk::RenderingAttachmentInfo {.imageView = *image.view,
                                                         .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
                                                         .loadOp = vk::AttachmentLoadOp::eClear,
                                                         .storeOp = vk::AttachmentStoreOp::eStore,
                                                         .clearValue = vk::ClearValue {.color = encodedClear(format)}};
    const auto rendering = vk::RenderingInfo {.renderArea = vk::Rect2D {.extent = extent},
                                              .layerCount = 1,
                                              .colorAttachmentCount = 1,
                                              .pColorAttachments = &attachment};
    command.beginRendering(rendering);
    command.endRendering();
}

void transitionToPresent(vk::CommandBuffer command, const SwapchainImage& image)
{
    static constexpr auto transition = ImageTransition {
        .oldLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .newLayout = vk::ImageLayout::ePresentSrcKHR,
        .source = {.stage = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                   .access = vk::AccessFlagBits2::eColorAttachmentWrite                                             },
        .destination = {.stage = vk::PipelineStageFlagBits2::eNone,                  .access = vk::AccessFlagBits2::eNone}
    };
    const auto barrier = makeImageBarrier(image, transition);
    recordImageBarrier(command, barrier);
}

}

Presentation::Presentation(const PresentationCreateInfo& info)
    : _physicalDevice {info.physicalDevice},
      _device {info.device},
      _presentQueue {info.device.getQueue(info.deviceInfo.presentQueueFamily, 0)},
      _surface {info.surface},
      _window {info.window},
      _deviceInfo {info.deviceInfo},
      _submissions {info.submissions},
      _performance {info.performance}
{
}

Presentation::~Presentation() = default;

auto Presentation::create(const PresentationCreateInfo& info) -> std::expected<std::unique_ptr<Presentation>, Error>
{
    auto result = std::unique_ptr<Presentation> {new Presentation {info}};
    if (const auto initialized = result->initialize(); !initialized)
    {
        return std::unexpected {initialized.error()};
    }
    return result;
}

auto Presentation::ensureWindowGeneration(FramebufferExtent requested) -> std::expected<void, Error>
{
    if (!_generation || _recreatePending || requested.width != _generation->extent.width ||
        requested.height != _generation->extent.height)
    {
        return recreate(requested);
    }
    return {};
}

auto Presentation::prepareWindowFrame() -> std::expected<bool, Error>
{
    if (_faulted)
    {
        return std::unexpected {makeError(ErrorCode::BackendFailure, "Windowed context is faulted")};
    }
    const auto requested = _window.getFramebufferExtent();
    if (!requested)
    {
        return std::unexpected {requested.error()};
    }
    if (requested->width == 0 || requested->height == 0)
    {
        return false;
    }
    if (const auto generation = ensureWindowGeneration(*requested); !generation)
    {
        return std::unexpected {generation.error()};
    }
    return _generation.has_value();
}

auto Presentation::recreate(FramebufferExtent requested) -> std::expected<void, Error>
{
    if (const auto drained = drain(); !drained)
    {
        return std::unexpected {drained.error()};
    }
    const auto oldSwapchain = _generation ? *_generation->swapchain : vk::SwapchainKHR {};
    auto generation = createSwapchainGeneration({.physicalDevice = _physicalDevice,
                                                 .device = _device,
                                                 .surface = _surface,
                                                 .requested = requested,
                                                 .oldSwapchain = oldSwapchain,
                                                 .deviceInfo = _deviceInfo});
    if (!generation)
    {
        // vkCreateSwapchainKHR may retire oldSwapchain even when replacement fails.
        _generation.reset();
        _recreatePending = true;
        return std::unexpected {std::move(generation.error())};
    }
    _generation = std::move(*generation);
    _recreatePending = false;
    return {};
}

auto Presentation::presentClearFrame() -> std::expected<FrameResult, Error>
{
    const auto ready = prepareWindowFrame();
    if (!ready)
    {
        return std::unexpected {ready.error()};
    }
    if (!*ready)
    {
        return FrameResult {.status = FrameResult::Status::Suspended, .completion = {}};
    }
    return executeAcquiredFrame();
}

auto Presentation::executeAcquiredFrame() -> std::expected<FrameResult, Error>
{
    const auto slotIndex = _nextSlot;
    auto& slot = _slots.at(slotIndex);
    const auto acquired = acquireFrame(slot);
    if (!acquired)
    {
        return std::unexpected {acquired.error()};
    }
    if (const auto nonReady = nonReadyFrameResult(acquired->status); nonReady)
    {
        return *nonReady;
    }
    return submitAcquiredFrame(slot, *acquired, slotIndex);
}

auto Presentation::submitAcquiredFrame(FrameSlot& slot, const AcquiredImage& acquired, std::size_t slotIndex)
    -> std::expected<FrameResult, Error>
{
    auto& image = activeGeneration().images.at(acquired.index);
    if (const auto waited = waitImage(slot, acquired.index); !waited)
    {
        return std::unexpected {waited.error()};
    }
    const auto selection = FrameSelection {.slotIndex = slotIndex, .imageIndex = acquired.index};
    const auto completion = submitFrame(slot, image, selection);
    if (!completion)
    {
        return std::unexpected {completion.error()};
    }
    return presentFrame(image, acquired.index, *completion, acquired.suboptimal);
}

auto Presentation::nonReadyFrameResult(AcquiredImage::Status status) -> std::optional<FrameResult>
{
    if (status == AcquiredImage::Status::Skipped)
    {
        return FrameResult {.status = FrameResult::Status::Skipped, .completion = {}};
    }
    if (status == AcquiredImage::Status::RecreatePending)
    {
        return FrameResult {.status = FrameResult::Status::RecreatePending, .completion = {}};
    }
    return std::nullopt;
}

auto Presentation::recordClear(FrameSlot& slot, const SwapchainImage& image, std::size_t slotIndex)
    -> std::expected<void, Error>
{
    if (const auto commands = recordFrameCommands(slot); !commands)
    {
        return std::unexpected {commands.error()};
    }
    return recordFrameWork(slot, image, slotIndex);
}

auto Presentation::recordFrameCommands(FrameSlot& slot) -> std::expected<void, Error>
{
    const auto reset = slot.pool.reset();
    if (reset != vk::Result::eSuccess)
    {
        return std::unexpected {makeVulkanError(reset)};
    }
    const auto begun = slot.command.begin(vk::CommandBufferBeginInfo {});
    if (begun != vk::Result::eSuccess)
    {
        return std::unexpected {makeVulkanError(begun)};
    }
    return {};
}

auto Presentation::recordFrameWork(FrameSlot& slot, const SwapchainImage& image, std::size_t slotIndex)
    -> std::expected<void, Error>
{
    transitionToColorAttachment(slot.command, image);
    if (_performance != nullptr)
    {
        if (const auto timestamp = _performance->recordStart(slotIndex, slot.command); !timestamp)
        {
            return std::unexpected {timestamp.error()};
        }
    }
    recordClearRendering(slot.command, image, activeGeneration().format, activeGeneration().extent);
    if (_performance != nullptr)
    {
        _performance->recordEnd(slotIndex, slot.command);
    }
    transitionToPresent(slot.command, image);
    const auto ended = slot.command.end();
    if (ended != vk::Result::eSuccess)
    {
        return std::unexpected {makeVulkanError(ended)};
    }
    return {};
}

auto Presentation::releaseImage(std::uint32_t index) const -> std::expected<void, Error>
{
    const auto info = vk::ReleaseSwapchainImagesInfoKHR {.swapchain = *activeGeneration().swapchain,
                                                         .imageIndexCount = 1,
                                                         .pImageIndices = &index};
    const auto result = _deviceInfo.swapchainMaintenance == ContextDeviceInfo::SwapchainMaintenance::Khr
                            ? _device.releaseSwapchainImagesKHR(info)
                            : _device.releaseSwapchainImagesEXT(info);
    if (result != vk::Result::eSuccess)
    {
        return std::unexpected {makeVulkanError(result)};
    }
    return {};
}

auto Presentation::abortAcquire(FrameSlot& slot, std::uint32_t index) -> std::expected<void, Error>
{
    const auto submitted = _submissions.submit({}, *slot.acquire, {});
    if (!submitted)
    {
        _faulted = true;
        _acquireRetirementFailed = true;
        return std::unexpected {submitted.error()};
    }
    const auto completion = *submitted;
    slot.lastCompletion = completion;
    if (const auto waited = waitCompletion(completion); !waited)
    {
        _faulted = true;
        return std::unexpected {waited.error()};
    }
    if (const auto released = releaseImage(index); !released)
    {
        _faulted = true;
        return std::unexpected {released.error()};
    }
    return {};
}

auto Presentation::acquireFrame(FrameSlot& slot) -> std::expected<AcquiredImage, Error>
{
    if (const auto waited = waitCompletion(slot.lastCompletion); !waited)
    {
        return std::unexpected {waited.error()};
    }
    if (const auto ready = _submissions.prepareSubmit(); !ready)
    {
        return std::unexpected {ready.error()};
    }
    static constexpr auto timeoutNanoseconds = std::uint64_t {1'000'000'000};
    const auto acquired = activeGeneration().swapchain.acquireNextImage(timeoutNanoseconds, *slot.acquire);
    return interpretAcquiredImage(acquired.result, acquired.value);
}

auto Presentation::interpretAcquiredImage(vk::Result result, std::uint32_t index) -> std::expected<AcquiredImage, Error>
{
    if (result == vk::Result::eTimeout || result == vk::Result::eNotReady)
    {
        return AcquiredImage {.status = AcquiredImage::Status::Skipped};
    }
    if (result == vk::Result::eErrorOutOfDateKHR)
    {
        _recreatePending = true;
        return AcquiredImage {.status = AcquiredImage::Status::RecreatePending};
    }
    if (result != vk::Result::eSuccess && result != vk::Result::eSuboptimalKHR)
    {
        _faulted = result == vk::Result::eErrorDeviceLost || result == vk::Result::eErrorSurfaceLostKHR;
        return std::unexpected {makeVulkanError(result)};
    }
    if (index >= activeGeneration().images.size())
    {
        _faulted = true;
        return std::unexpected {
            makeError(ErrorCode::BackendFailure, "Vulkan returned an invalid swapchain image index")};
    }
    return AcquiredImage {.status = AcquiredImage::Status::Ready,
                          .index = index,
                          .suboptimal = result == vk::Result::eSuboptimalKHR};
}

auto Presentation::waitImage(FrameSlot& slot, std::uint32_t index) -> std::expected<void, Error>
{
    auto& image = activeGeneration().images.at(index);
    if (!image.presentPending)
    {
        return {};
    }
    static constexpr auto timeoutNanoseconds = std::uint64_t {10'000'000'000};
    const auto fence = *image.presentFence;
    const auto waited = _device.waitForFences(fence, vk::True, timeoutNanoseconds);
    if (waited != vk::Result::eSuccess)
    {
        const auto cleanup = abortAcquire(slot, index);
        return std::unexpected {cleanup ? makeVulkanError(waited) : cleanup.error()};
    }
    const auto reset = _device.resetFences(fence);
    if (reset != vk::Result::eSuccess)
    {
        const auto cleanup = abortAcquire(slot, index);
        return std::unexpected {cleanup ? makeVulkanError(reset) : cleanup.error()};
    }
    image.presentPending = false;
    return {};
}

auto Presentation::submitRecordedFrame(const FrameSlot& slot, const SwapchainImage& image) -> Result<std::uint64_t>
{
    return _submissions.submit(slot.command, *slot.acquire, *image.renderFinished);
}

auto Presentation::submitFrame(FrameSlot& slot, SwapchainImage& image, FrameSelection selection)
    -> std::expected<std::uint64_t, Error>
{
    if (const auto recorded = recordClear(slot, image, selection.slotIndex); !recorded)
    {
        return abortFrame(slot, selection.imageIndex, recorded.error());
    }
    const auto submitted = submitRecordedFrame(slot, image);
    if (!submitted)
    {
        return handleSubmitFailure(slot, selection, submitted.error());
    }
    const auto completion = *submitted;
    slot.lastCompletion = completion;
    if (_performance != nullptr)
    {
        _performance->markSubmitted(selection.slotIndex);
    }
    image.initialized = true;
    _nextSlot = (_nextSlot + 1) % _slots.size();
    return completion;
}

auto Presentation::abortFrame(FrameSlot& slot, std::uint32_t imageIndex, const Error& cause)
    -> std::expected<std::uint64_t, Error>
{
    const auto cleanup = abortAcquire(slot, imageIndex);
    return std::unexpected {cleanup ? cause : cleanup.error()};
}

auto Presentation::handleSubmitFailure(FrameSlot& slot, FrameSelection selection, const Error& cause)
    -> std::expected<std::uint64_t, Error>
{
    if (cause.code == ErrorCode::DeviceLost)
    {
        _faulted = true;
        return std::unexpected {cause};
    }
    return abortFrame(slot, selection.imageIndex, cause);
}

auto Presentation::recoverPresentFailure(SwapchainImage& image,
                                         std::uint32_t index,
                                         vk::Result result,
                                         std::uint64_t completion) -> std::expected<FrameResult, Error>
{
    if (const auto waited = waitCompletion(completion); !waited)
    {
        _faulted = true;
        return std::unexpected {waited.error()};
    }
    const auto replacementInfo = vk::SemaphoreCreateInfo {};
    auto replacement = checkedCreation(_device.createSemaphore(replacementInfo));
    if (!replacement)
    {
        _faulted = true;
        return std::unexpected {std::move(replacement.error())};
    }
    image.renderFinished = std::move(*replacement);
    if (const auto released = releaseImage(index); !released)
    {
        _faulted = true;
        return std::unexpected {released.error()};
    }
    _recreatePending = true;
    return std::unexpected {makeVulkanError(result)};
}

auto Presentation::queuePresent(const SwapchainImage& image, std::uint32_t index) -> vk::Result
{
    const auto swapchain = *activeGeneration().swapchain;
    const auto finished = *image.renderFinished;
    const auto fence = *image.presentFence;
    const auto fenceInfo = vk::SwapchainPresentFenceInfoKHR {.swapchainCount = 1, .pFences = &fence};
    const auto presentInfo = vk::PresentInfoKHR {.pNext = &fenceInfo,
                                                 .waitSemaphoreCount = 1,
                                                 .pWaitSemaphores = &finished,
                                                 .swapchainCount = 1,
                                                 .pSwapchains = &swapchain,
                                                 .pImageIndices = &index};
    return _presentQueue.presentKHR(presentInfo);
}

auto Presentation::presentFrame(SwapchainImage& image,
                                std::uint32_t index,
                                std::uint64_t completion,
                                bool acquireSuboptimal) -> std::expected<FrameResult, Error>
{
    const auto presented = queuePresent(image, index);
    if (presented == vk::Result::eSuccess || presented == vk::Result::eSuboptimalKHR ||
        presented == vk::Result::eErrorOutOfDateKHR || presented == vk::Result::eErrorSurfaceLostKHR)
    {
        image.presentPending = true;
        if (presented == vk::Result::eErrorSurfaceLostKHR)
        {
            _faulted = true;
            return std::unexpected {makeVulkanError(presented)};
        }
        if (presented == vk::Result::eSuboptimalKHR || presented == vk::Result::eErrorOutOfDateKHR || acquireSuboptimal)
        {
            _recreatePending = true;
            return FrameResult {.status = FrameResult::Status::RecreatePending,
                                .completion = _submissions.getPoint(completion)};
        }
        return FrameResult {.status = FrameResult::Status::Presented, .completion = _submissions.getPoint(completion)};
    }
    if (presented == vk::Result::eErrorDeviceLost)
    {
        _faulted = true;
        return std::unexpected {makeVulkanError(presented)};
    }
    return recoverPresentFailure(image, index, presented, completion);
}

auto Presentation::verifyReleaseFunction() const -> std::expected<void, Error>
{
    const auto* const dispatcher = _device.getDispatcher();
    const auto khr = _deviceInfo.swapchainMaintenance == ContextDeviceInfo::SwapchainMaintenance::Khr;
    const auto available =
        khr ? dispatcher->vkReleaseSwapchainImagesKHR != nullptr : dispatcher->vkReleaseSwapchainImagesEXT != nullptr;
    if (!available)
    {
        return std::unexpected {makeError(ErrorCode::Unsupported,
                                          khr ? "VK_KHR_swapchain_maintenance1 release function is unavailable"
                                              : "VK_EXT_swapchain_maintenance1 release function is unavailable")};
    }
    return {};
}

auto Presentation::initializeSlot(FrameSlot& slot) const -> std::expected<void, Error>
{
    auto commands = createFrameCommands(_device, _deviceInfo.queueFamily);
    if (!commands)
    {
        return std::unexpected {commands.error()};
    }
    slot.pool = std::move(commands->pool);
    slot.command = commands->command;
    static constexpr auto acquireInfo = vk::SemaphoreCreateInfo {};
    auto acquire = checkedCreation(_device.createSemaphore(acquireInfo));
    if (!acquire)
    {
        return std::unexpected {acquire.error()};
    }
    slot.acquire = std::move(*acquire);
    return {};
}

auto Presentation::initialize() -> std::expected<void, Error>
{
    if (const auto initialized = initializeFrameResources(); !initialized)
    {
        return std::unexpected {initialized.error()};
    }
    return initializeWindowPresentation();
}

auto Presentation::initializeFrameResources() -> std::expected<void, Error>
{
    if (const auto resolved = verifyReleaseFunction(); !resolved)
    {
        return std::unexpected {resolved.error()};
    }
    if (const auto initialized = _submissions.initialize(_physicalDevice); !initialized)
    {
        return initialized;
    }
    for (auto& slot : _slots)
    {
        if (const auto initialized = initializeSlot(slot); !initialized)
        {
            return std::unexpected {initialized.error()};
        }
    }
    return {};
}

auto Presentation::initializeWindowPresentation() -> std::expected<void, Error>
{
    const auto extent = _window.getFramebufferExtent();
    if (!extent)
    {
        return std::unexpected {extent.error()};
    }
    if (extent->width == 0 || extent->height == 0)
    {
        _recreatePending = true;
        return {};
    }
    return recreate(*extent);
}

auto Presentation::waitCompletion(std::uint64_t value) const -> std::expected<void, Error>
{
    static constexpr auto timeoutNanoseconds = std::uint64_t {10'000'000'000};
    return _submissions.waitValue(value, timeoutNanoseconds).and_then([](WaitStatus status) -> Result<void> {
        if (status == WaitStatus::Timeout)
        {
            return std::unexpected {makeError(ErrorCode::Timeout, "GPU completion wait timed out")};
        }
        return {};
    });
}

auto Presentation::waitPresentation() -> std::expected<void, Error>
{
    if (!_generation)
    {
        return {};
    }
    static constexpr auto timeoutNanoseconds = std::uint64_t {10'000'000'000};
    for (auto& image : _generation->images)
    {
        if (!image.presentPending)
        {
            continue;
        }
        const auto fence = *image.presentFence;
        const auto result = _device.waitForFences(fence, vk::True, timeoutNanoseconds);
        if (result != vk::Result::eSuccess)
        {
            return std::unexpected {makeVulkanError(result)};
        }
        image.presentPending = false;
    }
    return {};
}

auto Presentation::drain() -> std::expected<void, Error>
{
    if (_acquireRetirementFailed)
    {
        return std::unexpected {makeError(ErrorCode::BackendFailure, "Acquired image has no committed retirement")};
    }
    const auto lastCompletion =
        std::accumulate(_slots.begin(), _slots.end(), std::uint64_t {0}, [](std::uint64_t last, const FrameSlot& slot) {
            return std::max(last, slot.lastCompletion);
        });
    if (const auto waited = waitCompletion(lastCompletion); !waited)
    {
        return std::unexpected {waited.error()};
    }
    return waitPresentation();
}

auto Presentation::activeGeneration() -> SwapchainGeneration&
{
    if (!_generation)
    {
        panic("A window frame requires a live swapchain generation");
    }
    return *_generation;
}

auto Presentation::activeGeneration() const -> const SwapchainGeneration&
{
    if (!_generation)
    {
        panic("A window frame requires a live swapchain generation");
    }
    return *_generation;
}

}
