#include "presentation/WindowPresentation.hpp"

#include <vulkan/vulkan_core.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <glm/ext/vector_uint2.hpp>
#include <limits>
#include <memory>
#include <optional>
#include <panda/EngineOptions.hpp>
#include <panda/base/Error.hpp>
#include <panda/diagnostics/Assert.hpp>
#include <panda/rendering/FrameResult.hpp>
#include <panda/rendering/RenderView.hpp>
#include <panda/rendering/SampleCount.hpp>
#include <panda/submission/CompletionPoint.hpp>
#include <panda/vulkan/Error.hpp>
#include <panda/vulkan/WindowSurface.hpp>
#include <string>
#include <utility>
#include <vector>
#include <vulkan/vulkan.hpp>

#include "DeviceSelection.hpp"
#include "SubmissionQueue.hpp"
#include "VulkanError.hpp"
#include "VulkanHpp.hpp"  // IWYU pragma: keep
#include "presentation/FrameExecution.hpp"
#include "presentation/OffscreenRenderBackend.hpp"
#include "presentation/SwapchainGeneration.hpp"
#include "renderer/RenderAttachments.hpp"

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

WindowPresentation::WindowPresentation(const WindowPresentationCreateInfo& info)
    : _physicalDevice {info.physicalDevice},
      _device {info.device},
      _presentQueue {info.device.getQueue(info.presentQueueFamily, 0)},
      _surface {info.surface},
      _window {info.window},
      _queueFamily {info.queueFamily},
      _presentQueueFamily {info.presentQueueFamily},
      _maintenance {info.maintenance},
      _requestedPresentationMode {info.presentationMode},
      _lastPresentationMode {info.presentationMode},
      _submissions {info.submissions},
      _textureFilteringPreset {info.textureFilteringPreset}
{
}

WindowPresentation::~WindowPresentation() = default;

auto WindowPresentation::create(const WindowPresentationCreateInfo& info)
    -> std::expected<std::unique_ptr<WindowPresentation>, Error>
{
    auto result = std::unique_ptr<WindowPresentation> {new WindowPresentation {info}};
    if (const auto initialized = result->initialize(info); !initialized)
    {
        return std::unexpected {initialized.error()};
    }
    return result;
}

auto WindowPresentation::getSupportedPresentationModes() const -> Result<std::vector<EngineOptions::PresentationMode>>
{
    return queryPresentationModes(_physicalDevice, _surface);
}

auto WindowPresentation::getRequestedPresentationMode() const noexcept -> EngineOptions::PresentationMode
{
    return _requestedPresentationMode;
}

auto WindowPresentation::getEffectivePresentationMode() const noexcept -> std::optional<EngineOptions::PresentationMode>
{
    return _generation ? std::optional {_lastPresentationMode} : std::nullopt;
}

auto WindowPresentation::setPresentationMode(EngineOptions::PresentationMode mode) -> Result<void>
{
    if (const auto selected = selectPresentMode(_physicalDevice, _surface, mode); !selected)
    {
        return std::unexpected {selected.error()};
    }
    _requestedPresentationMode = mode;
    return {};
}

auto WindowPresentation::getSupportedSampleCounts() const -> Result<std::vector<SampleCount>>
{
    if (!_generation)
    {
        return std::unexpected {makeError(ErrorCode::Unsupported, "Window has no active swapchain format")};
    }
    return renderer::RenderAttachments::getSupportedSampleCounts(_physicalDevice, _generation->format);
}

auto WindowPresentation::ensureWindowGeneration(glm::uvec2 requested) -> std::expected<void, Error>
{
    if (!_generation || _recreatePending || _requestedPresentationMode != _lastPresentationMode ||
        requested.x != _generation->extent.width || requested.y != _generation->extent.height)
    {
        return recreate(requested);
    }
    return {};
}

auto WindowPresentation::prepareWindowFrame() -> std::expected<bool, Error>
{
    if (_faulted)
    {
        return std::unexpected {makeError(ErrorCode::BackendFailure, "Windowed Engine is faulted")};
    }
    const auto requested = _window.getFramebufferExtent();
    if (!requested)
    {
        return std::unexpected {requested.error()};
    }
    if (requested->x == 0 || requested->y == 0)
    {
        return false;
    }
    if (const auto generation = ensureWindowGeneration(*requested); !generation)
    {
        return std::unexpected {generation.error()};
    }
    return _generation.has_value();
}

auto WindowPresentation::recreate(glm::uvec2 requested) -> std::expected<void, Error>
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
                                                 .queueFamily = _queueFamily,
                                                 .presentQueueFamily = _presentQueueFamily,
                                                 .presentationMode = _requestedPresentationMode});
    if (!generation)
    {
        handleGenerationFailure(generation.error());
        return std::unexpected {std::move(generation.error().cause)};
    }
    _generation = std::move(*generation);
    _lastPresentationMode = _requestedPresentationMode;
    _recreatePending = false;
    return {};
}

void WindowPresentation::handleGenerationFailure(const SwapchainGenerationError& failure)
{
    if (failure.oldSwapchainRetired)
    {
        _generation.reset();
        _recreatePending = true;
    }
    else if (failure.cause.code == ErrorCode::Unsupported)
    {
        _requestedPresentationMode = _lastPresentationMode;
    }
}

auto WindowPresentation::render(const RenderView& view) -> std::expected<FrameResult, Error>
{
    auto submittedPerformanceSlot = std::optional<std::size_t> {};
    return render(view, submittedPerformanceSlot);
}

auto WindowPresentation::render(const RenderView& view, std::optional<std::size_t>& submittedPerformanceSlot)
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
        return FrameResult {.status = FrameResult::Status::Suspended, .completion = {}};
    }
    const auto& output = activeGeneration();
    if (const auto prepared = _execution->prepareOutput(output.extent, output.format, view); !prepared)
    {
        return std::unexpected {prepared.error()};
    }
    const auto slotIndex = _nextSlot;
    if (const auto prepared = _execution->prepareSlot(slotIndex, view); !prepared)
    {
        return std::unexpected {prepared.error()};
    }
    return executeAcquiredFrame(slotIndex, view, submittedPerformanceSlot);
}

auto WindowPresentation::renderWithCapture(const CaptureFrameRequest& request) -> std::expected<FrameResult, Error>
{
    request.submittedPerformanceSlot.reset();
    const auto ready = prepareWindowFrame();
    if (!ready)
    {
        return std::unexpected {ready.error()};
    }
    if (!*ready)
    {
        return FrameResult {.status = FrameResult::Status::Suspended, .completion = {}};
    }
    if (!activeGeneration().supportsTransferDestination)
    {
        return std::unexpected {makeError(ErrorCode::Unsupported, "Surface does not support captured presentation")};
    }
    return executeAcquiredCaptureFrame(request);
}

auto WindowPresentation::executeAcquiredCaptureFrame(const CaptureFrameRequest& request)
    -> std::expected<FrameResult, Error>
{
    const auto slotIndex = _nextSlot;
    const auto acquired = acquireFrame(slotIndex);
    if (!acquired)
    {
        return std::unexpected {acquired.error()};
    }
    if (const auto nonReady = nonReadyFrameResult(acquired->status); nonReady)
    {
        return *nonReady;
    }
    return submitAcquiredCaptureFrame(*acquired, request);
}

auto WindowPresentation::submitAcquiredCaptureFrame(const AcquiredImage& acquired, const CaptureFrameRequest& request)
    -> std::expected<FrameResult, Error>
{
    const auto slotIndex = _nextSlot;
    auto& image = activeGeneration().images.at(acquired.index);
    if (const auto waited = waitImage(slotIndex, acquired.index); !waited)
    {
        return std::unexpected {waited.error()};
    }
    const auto output = presentation::WindowCaptureOutput {.image = image.image,
                                                           .extent = activeGeneration().extent,
                                                           .format = activeGeneration().format,
                                                           .initialized = image.initialized};
    auto submissionValue = std::uint64_t {};
    const auto capture =
        presentation::WindowCaptureRequest {.view = request.view,
                                            .target = request.target,
                                            .output = output,
                                            .acquire = *_acquireSemaphores.at(slotIndex),
                                            .finished = *image.renderFinished,
                                            .submittedValue = submissionValue,
                                            .submittedPerformanceSlot = request.submittedPerformanceSlot};
    const auto rendered = request.rendering.renderWithCapture(capture);
    if (!rendered)
    {
        const auto recovered = abortFrame(slotIndex, acquired.index, rendered.error());
        return std::unexpected {recovered.error()};
    }
    _execution->setLastCompletion(slotIndex, submissionValue);
    image.initialized = true;
    _nextSlot = (_nextSlot + 1) % _acquireSemaphores.size();
    return presentFrame(acquired, submissionValue, rendered->effectiveSampleCount, rendered->effectiveAnisotropy);
}

auto WindowPresentation::executeAcquiredFrame(std::size_t slotIndex,
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

auto WindowPresentation::submitAcquiredFrame(const AcquiredImage& acquired,
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
    return presentFrame(acquired,
                        *completion,
                        _execution->getSampleCount(view),
                        _execution->getEffectiveAnisotropy(view));
}

auto WindowPresentation::nonReadyFrameResult(AcquiredImage::Status status) -> std::optional<FrameResult>
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

auto WindowPresentation::recordFrame(std::size_t slotIndex, const SwapchainImage& image, const RenderView& view)
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

auto WindowPresentation::releaseImage(std::uint32_t index) const -> std::expected<void, Error>
{
    const auto info = vk::ReleaseSwapchainImagesInfoKHR {.swapchain = *activeGeneration().swapchain,
                                                         .imageIndexCount = 1,
                                                         .pImageIndices = &index};
    const auto result = _maintenance == SwapchainMaintenance::Khr ? _device.releaseSwapchainImagesKHR(info)
                                                                  : _device.releaseSwapchainImagesEXT(info);
    if (result != vk::Result::eSuccess)
    {
        return std::unexpected {makeVulkanError(result)};
    }
    return {};
}

auto WindowPresentation::abortAcquire(FrameSelection selection) -> std::expected<void, Error>
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

auto WindowPresentation::acquireFrame(std::size_t slotIndex) -> std::expected<AcquiredImage, Error>
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

auto WindowPresentation::interpretAcquiredImage(vk::Result result, std::uint32_t index)
    -> std::expected<AcquiredImage, Error>
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

auto WindowPresentation::waitImage(std::size_t slotIndex, std::uint32_t index) -> std::expected<void, Error>
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

auto WindowPresentation::submitRecordedFrame(std::size_t slotIndex, const SwapchainImage& image)
    -> Result<std::uint64_t>
{
    return _submissions.submit(_execution->command(slotIndex),
                               *_acquireSemaphores.at(slotIndex),
                               *image.renderFinished);
}

auto WindowPresentation::submitFrame(std::size_t slotIndex,
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

auto WindowPresentation::abortFrame(std::size_t slotIndex, std::uint32_t imageIndex, const Error& cause)
    -> std::expected<std::uint64_t, Error>
{
    const auto cleanup = abortAcquire({.slotIndex = slotIndex, .imageIndex = imageIndex});
    return std::unexpected {cleanup ? cause : cleanup.error()};
}

auto WindowPresentation::handleSubmitFailure(FrameSelection selection, const Error& cause)
    -> std::expected<std::uint64_t, Error>
{
    if (cause.code == ErrorCode::DeviceLost)
    {
        _faulted = true;
        return std::unexpected {cause};
    }
    return abortFrame(selection.slotIndex, selection.imageIndex, cause);
}

auto WindowPresentation::recoverPresentFailure(std::uint32_t index, vk::Result result, std::uint64_t completion)
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

auto WindowPresentation::finishPendingImageRecovery() -> std::expected<void, Error>
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

auto WindowPresentation::replacePendingRenderFinishedSemaphore() -> std::expected<void, Error>
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

auto WindowPresentation::queuePresent(const SwapchainImage& image, std::uint32_t index) -> vk::Result
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

auto WindowPresentation::presentFrame(const AcquiredImage& acquired,
                                      std::uint64_t completion,
                                      SampleCount sampleCount,
                                      float effectiveAnisotropy) -> std::expected<FrameResult, Error>
{
    const auto presented = queuePresent(activeGeneration().images.at(acquired.index), acquired.index);
    if (presented == vk::Result::eSuccess || presented == vk::Result::eSuboptimalKHR ||
        presented == vk::Result::eErrorOutOfDateKHR || presented == vk::Result::eErrorSurfaceLostKHR)
    {
        const auto frameResult = FrameResult {.completion = _submissions.getPoint(completion),
                                              .effectiveSampleCount = sampleCount,
                                              .effectiveAnisotropy = effectiveAnisotropy};
        return finishSuccessfulPresent(acquired, presented, frameResult);
    }
    if (presented == vk::Result::eErrorDeviceLost)
    {
        _faulted = true;
        return std::unexpected {makeVulkanError(presented)};
    }
    return recoverPresentFailure(acquired.index, presented, completion);
}

auto WindowPresentation::finishSuccessfulPresent(const AcquiredImage& acquired,
                                                 vk::Result result,
                                                 FrameResult frameResult) -> std::expected<FrameResult, Error>
{
    auto& image = activeGeneration().images.at(acquired.index);
    image.presentPending = true;
    if (result == vk::Result::eErrorSurfaceLostKHR)
    {
        _faulted = true;
        return std::unexpected {makeVulkanError(result)};
    }
    if (result == vk::Result::eSuboptimalKHR || result == vk::Result::eErrorOutOfDateKHR || acquired.suboptimal)
    {
        _recreatePending = true;
        frameResult.status = FrameResult::Status::RecreatePending;
        return frameResult;
    }
    frameResult.status = FrameResult::Status::Rendered;
    return frameResult;
}

auto WindowPresentation::verifyReleaseFunction() const -> std::expected<void, Error>
{
    const auto* const dispatcher = _device.getDispatcher();
    const auto khr = _maintenance == SwapchainMaintenance::Khr;
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

auto WindowPresentation::initializeAcquireSemaphore(vk::raii::Semaphore& semaphore) const -> std::expected<void, Error>
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

auto WindowPresentation::initialize(const WindowPresentationCreateInfo& info) -> std::expected<void, Error>
{
    if (const auto initialized = initializeFrameResources(info); !initialized)
    {
        return std::unexpected {initialized.error()};
    }
    return initializeWindowPresentation();
}

auto WindowPresentation::initializeFrameResources(const WindowPresentationCreateInfo& info)
    -> std::expected<void, Error>
{
    if (const auto resolved = verifyReleaseFunction(); !resolved)
    {
        return std::unexpected {resolved.error()};
    }
    auto execution = presentation::FrameExecution::create({.physicalDevice = _physicalDevice,
                                                           .device = _device,
                                                           .queueFamily = _queueFamily,
                                                           .submissions = _submissions,
                                                           .pool = info.pool,
                                                           .performance = info.performance,
                                                           .textureFilteringPreset = info.textureFilteringPreset,
                                                           .maxLights = info.maxLights});
    if (!execution)
    {
        return std::unexpected {execution.error()};
    }
    _execution = std::move(*execution);
    return initializeAcquireSemaphores();
}

auto WindowPresentation::initializeAcquireSemaphores() -> std::expected<void, Error>
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

auto WindowPresentation::initializeWindowPresentation() -> std::expected<void, Error>
{
    const auto extent = _window.getFramebufferExtent();
    if (!extent)
    {
        return std::unexpected {extent.error()};
    }
    if (extent->x == 0 || extent->y == 0)
    {
        _recreatePending = true;
        return {};
    }
    return recreate(*extent);
}

auto WindowPresentation::waitCompletion(std::uint64_t value) const -> std::expected<void, Error>
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

auto WindowPresentation::waitPresentation() -> std::expected<void, Error>
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

auto WindowPresentation::drain() -> std::expected<void, Error>
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

auto WindowPresentation::activeGeneration() -> SwapchainGeneration&
{
    if (!_generation)
    {
        panic("A window frame requires a live swapchain generation");
    }
    return *_generation;
}

auto WindowPresentation::activeGeneration() const -> const SwapchainGeneration&
{
    if (!_generation)
    {
        panic("A window frame requires a live swapchain generation");
    }
    return *_generation;
}

}
