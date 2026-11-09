#include "presentation/Presentation.hpp"

#include <vulkan/vulkan_core.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <memory>
#include <optional>
#include <panda/Assert.hpp>
#include <panda/CompletionPoint.hpp>
#include <panda/Context.hpp>
#include <panda/Error.hpp>
#include <panda/RenderView.hpp>
#include <panda/WindowSurface.hpp>
#include <string>
#include <utility>
#include <vulkan/vulkan.hpp>

#include "SubmissionQueue.hpp"
#include "VulkanError.hpp"
#include "VulkanHpp.hpp"  // IWYU pragma: keep
#include "presentation/FrameExecution.hpp"
#include "presentation/SwapchainGeneration.hpp"

namespace panda::core::vulkan
{
using detail::checkedCreation;

namespace
{
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
      _submissions {info.submissions}
{
}

Presentation::~Presentation() = default;

auto Presentation::create(const PresentationCreateInfo& info) -> std::expected<std::unique_ptr<Presentation>, Error>
{
    auto result = std::unique_ptr<Presentation> {new Presentation {info}};
    if (const auto initialized = result->initialize(info); !initialized)
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

auto Presentation::render(const RenderView& view) -> std::expected<FrameResult, Error>
{
    auto submittedPerformanceSlot = std::optional<std::size_t> {};
    return render(view, submittedPerformanceSlot);
}

auto Presentation::render(const RenderView& view, std::optional<std::size_t>& submittedPerformanceSlot)
    -> std::expected<FrameResult, Error>
{
    submittedPerformanceSlot.reset();
    const auto ready = prepareWindowFrame();
    if (!ready)
    {
        return std::unexpected {ready.error()};
    }
    if (!*ready)
    {
        return FrameResult {.status = FrameResult::Status::Suspended,
                            .completion = {},
                            .effectiveSampleCount = effectiveSampleCount()};
    }
    const auto slotIndex = _nextSlot;
    if (const auto prepared = _execution->prepareSlot(slotIndex, view); !prepared)
    {
        return std::unexpected {prepared.error()};
    }
    return executeAcquiredFrame(slotIndex, view, submittedPerformanceSlot);
}

auto Presentation::executeAcquiredFrame(std::size_t slotIndex,
                                        const RenderView& view,
                                        std::optional<std::size_t>& submittedPerformanceSlot)
    -> std::expected<FrameResult, Error>
{
    const auto acquired = acquireFrame(slotIndex);
    if (!acquired)
    {
        _execution->discardUnsubmittedInputs(slotIndex);
        return std::unexpected {acquired.error()};
    }
    if (const auto nonReady = nonReadyFrameResult(acquired->status); nonReady)
    {
        _execution->discardUnsubmittedInputs(slotIndex);
        return *nonReady;
    }
    return submitAcquiredFrame(*acquired, slotIndex, view, submittedPerformanceSlot);
}

auto Presentation::submitAcquiredFrame(const AcquiredImage& acquired,
                                       std::size_t slotIndex,
                                       const RenderView& view,
                                       std::optional<std::size_t>& submittedPerformanceSlot)
    -> std::expected<FrameResult, Error>
{
    auto& image = activeGeneration().images.at(acquired.index);
    if (const auto waited = waitImage(slotIndex, acquired.index); !waited)
    {
        _execution->discardUnsubmittedInputs(slotIndex);
        return std::unexpected {waited.error()};
    }
    const auto selection = FrameSelection {.slotIndex = slotIndex, .imageIndex = acquired.index};
    const auto completion = submitFrame(slotIndex, image, selection, view);
    if (!completion)
    {
        _execution->discardUnsubmittedInputs(slotIndex);
        return std::unexpected {completion.error()};
    }
    if (_execution->isGpuTimestampSamplingEnabled())
    {
        submittedPerformanceSlot = slotIndex;
    }
    return presentFrame(image, acquired.index, *completion, acquired.suboptimal);
}

auto Presentation::nonReadyFrameResult(AcquiredImage::Status status) const -> std::optional<FrameResult>
{
    if (status == AcquiredImage::Status::Skipped)
    {
        return FrameResult {.status = FrameResult::Status::Skipped,
                            .completion = {},
                            .effectiveSampleCount = effectiveSampleCount()};
    }
    if (status == AcquiredImage::Status::RecreatePending)
    {
        return FrameResult {.status = FrameResult::Status::RecreatePending,
                            .completion = {},
                            .effectiveSampleCount = effectiveSampleCount()};
    }
    return std::nullopt;
}

auto Presentation::recordFrame(std::size_t slotIndex, const SwapchainImage& image, const RenderView& view)
    -> std::expected<void, Error>
{
    if (const auto begun = _execution->beginCommands(slotIndex); !begun)
    {
        return std::unexpected {begun.error()};
    }
    const auto commandBuffer = _execution->command(slotIndex);
    transitionToColorAttachment(commandBuffer, image);
    const auto& generation = activeGeneration();
    const auto output = presentation::FrameOutput {.imageView = *image.view,
                                                   .extent = generation.extent,
                                                   .colorFormat = generation.format};
    if (const auto content = _execution->recordContent(slotIndex, output, view); !content)
    {
        return std::unexpected {content.error()};
    }
    transitionToPresent(commandBuffer, image);
    return _execution->endCommands(slotIndex);
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

auto Presentation::abortAcquire(FrameSelection selection) -> std::expected<void, Error>
{
    const auto submitted = _submissions.submit({}, *_acquireSemaphores.at(selection.slotIndex), {});
    if (!submitted)
    {
        _faulted = true;
        _acquireRetirementFailed = true;
        return std::unexpected {submitted.error()};
    }
    const auto completion = *submitted;
    _execution->setLastCompletion(selection.slotIndex, completion);
    _pendingImageRecovery = PendingImageRecovery {.index = selection.imageIndex, .replaceRenderFinished = false};
    if (const auto waited = waitCompletion(completion); !waited)
    {
        _faulted = true;
        return std::unexpected {waited.error()};
    }
    if (const auto recovered = finishPendingImageRecovery(); !recovered)
    {
        _faulted = true;
        return std::unexpected {recovered.error()};
    }
    return {};
}

auto Presentation::acquireFrame(std::size_t slotIndex) -> std::expected<AcquiredImage, Error>
{
    if (const auto ready = _submissions.prepareSubmit(); !ready)
    {
        return std::unexpected {ready.error()};
    }
    static constexpr auto timeoutNanoseconds = std::uint64_t {1'000'000'000};
    auto index = std::numeric_limits<std::uint32_t>::max();
    const auto result =
        _device.getDispatcher()->vkAcquireNextImageKHR(static_cast<VkDevice>(*_device),
                                                       static_cast<VkSwapchainKHR>(*activeGeneration().swapchain),
                                                       timeoutNanoseconds,
                                                       static_cast<VkSemaphore>(*_acquireSemaphores.at(slotIndex)),
                                                       VkFence {},
                                                       &index);
    return interpretAcquiredImage(static_cast<vk::Result>(result), index);
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

auto Presentation::waitImage(std::size_t slotIndex, std::uint32_t index) -> std::expected<void, Error>
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
        const auto cleanup = abortAcquire({.slotIndex = slotIndex, .imageIndex = index});
        return std::unexpected {cleanup ? makeVulkanError(waited) : cleanup.error()};
    }
    const auto reset = _device.resetFences(fence);
    if (reset != vk::Result::eSuccess)
    {
        const auto cleanup = abortAcquire({.slotIndex = slotIndex, .imageIndex = index});
        return std::unexpected {cleanup ? makeVulkanError(reset) : cleanup.error()};
    }
    image.presentPending = false;
    return {};
}

auto Presentation::submitRecordedFrame(std::size_t slotIndex, const SwapchainImage& image) -> Result<std::uint64_t>
{
    return _submissions.submit(_execution->command(slotIndex),
                               *_acquireSemaphores.at(slotIndex),
                               *image.renderFinished);
}

auto Presentation::submitFrame(std::size_t slotIndex,
                               SwapchainImage& image,
                               FrameSelection selection,
                               const RenderView& view) -> std::expected<std::uint64_t, Error>
{
    if (const auto recorded = recordFrame(slotIndex, image, view); !recorded)
    {
        return abortFrame(slotIndex, selection.imageIndex, recorded.error());
    }
    const auto submitted = submitRecordedFrame(slotIndex, image);
    if (!submitted)
    {
        return handleSubmitFailure(selection, submitted.error());
    }
    const auto completion = *submitted;
    _execution->markSubmitted(slotIndex, completion, !view.meshes.empty());
    image.initialized = true;
    _nextSlot = (_nextSlot + 1) % _acquireSemaphores.size();
    return completion;
}

auto Presentation::abortFrame(std::size_t slotIndex, std::uint32_t imageIndex, const Error& cause)
    -> std::expected<std::uint64_t, Error>
{
    const auto cleanup = abortAcquire({.slotIndex = slotIndex, .imageIndex = imageIndex});
    return std::unexpected {cleanup ? cause : cleanup.error()};
}

auto Presentation::handleSubmitFailure(FrameSelection selection, const Error& cause)
    -> std::expected<std::uint64_t, Error>
{
    if (cause.code == ErrorCode::DeviceLost)
    {
        _faulted = true;
        return std::unexpected {cause};
    }
    return abortFrame(selection.slotIndex, selection.imageIndex, cause);
}

auto Presentation::recoverPresentFailure(std::uint32_t index, vk::Result result, std::uint64_t completion)
    -> std::expected<FrameResult, Error>
{
    _pendingImageRecovery = PendingImageRecovery {.index = index, .replaceRenderFinished = true};
    if (const auto waited = waitCompletion(completion); !waited)
    {
        _faulted = true;
        return std::unexpected {waited.error()};
    }
    if (const auto recovered = finishPendingImageRecovery(); !recovered)
    {
        _faulted = true;
        return std::unexpected {recovered.error()};
    }
    _recreatePending = true;
    return std::unexpected {makeVulkanError(result)};
}

auto Presentation::finishPendingImageRecovery() -> std::expected<void, Error>
{
    if (!_pendingImageRecovery)
    {
        return {};
    }
    auto& pending = *_pendingImageRecovery;
    if (pending.releaseFailed)
    {
        return std::unexpected {
            makeError(ErrorCode::BackendFailure, "Swapchain image release left its acquisition state unconfirmed")};
    }
    if (const auto replaced = replacePendingRenderFinishedSemaphore(); !replaced)
    {
        return std::unexpected {replaced.error()};
    }
    if (const auto released = releaseImage(pending.index); !released)
    {
        pending.releaseFailed = true;
        return std::unexpected {released.error()};
    }
    _pendingImageRecovery.reset();
    return {};
}

auto Presentation::replacePendingRenderFinishedSemaphore() -> std::expected<void, Error>
{
    if (!_pendingImageRecovery || !_pendingImageRecovery->replaceRenderFinished)
    {
        return {};
    }
    auto& pending = *_pendingImageRecovery;
    auto replacement = checkedCreation(_device.createSemaphore(vk::SemaphoreCreateInfo {}));
    if (!replacement)
    {
        return std::unexpected {std::move(replacement.error())};
    }
    activeGeneration().images.at(pending.index).renderFinished = std::move(*replacement);
    pending.replaceRenderFinished = false;
    return {};
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
                                .completion = _submissions.getPoint(completion),
                                .effectiveSampleCount = effectiveSampleCount()};
        }
        return FrameResult {.status = FrameResult::Status::Presented,
                            .completion = _submissions.getPoint(completion),
                            .effectiveSampleCount = effectiveSampleCount()};
    }
    if (presented == vk::Result::eErrorDeviceLost)
    {
        _faulted = true;
        return std::unexpected {makeVulkanError(presented)};
    }
    return recoverPresentFailure(index, presented, completion);
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

auto Presentation::initializeAcquireSemaphore(vk::raii::Semaphore& semaphore) const -> std::expected<void, Error>
{
    static constexpr auto acquireInfo = vk::SemaphoreCreateInfo {};
    auto acquire = checkedCreation(_device.createSemaphore(acquireInfo));
    if (!acquire)
    {
        return std::unexpected {acquire.error()};
    }
    semaphore = std::move(*acquire);
    return {};
}

auto Presentation::initialize(const PresentationCreateInfo& info) -> std::expected<void, Error>
{
    if (const auto initialized = initializeFrameResources(info); !initialized)
    {
        return std::unexpected {initialized.error()};
    }
    return initializeWindowPresentation();
}

auto Presentation::initializeFrameResources(const PresentationCreateInfo& info) -> std::expected<void, Error>
{
    if (const auto resolved = verifyReleaseFunction(); !resolved)
    {
        return std::unexpected {resolved.error()};
    }
    if (const auto initialized = _submissions.initialize(_physicalDevice); !initialized)
    {
        return initialized;
    }
    auto execution = presentation::FrameExecution::create({.physicalDevice = _physicalDevice,
                                                           .device = _device,
                                                           .queueFamily = _deviceInfo.queueFamily,
                                                           .submissions = _submissions,
                                                           .pool = info.pool,
                                                           .performance = info.performance});
    if (!execution)
    {
        return std::unexpected {execution.error()};
    }
    _execution = std::move(*execution);
    return initializeAcquireSemaphores();
}

auto Presentation::initializeAcquireSemaphores() -> std::expected<void, Error>
{
    for (auto& semaphore : _acquireSemaphores)
    {
        if (const auto initialized = initializeAcquireSemaphore(semaphore); !initialized)
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
    if (const auto drained = _execution->drain(); !drained)
    {
        return std::unexpected {drained.error()};
    }
    if (const auto recovered = finishPendingImageRecovery(); !recovered)
    {
        return std::unexpected {recovered.error()};
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

auto Presentation::effectiveSampleCount() const noexcept -> vk::SampleCountFlagBits
{
    return _execution->getSampleCount();
}

}
