// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include "SubmissionQueue.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <expected>
#include <limits>
#include <panda/Assert.hpp>
#include <panda/CompletionPoint.hpp>
#include <panda/Error.hpp>

namespace panda::detail
{
namespace
{
auto nextSubmissionId() -> std::uint64_t
{
    static auto nextId = std::atomic<std::uint64_t> {1};
    const auto id = nextId.fetch_add(1, std::memory_order_relaxed);
    expect(id != 0 && id != std::numeric_limits<std::uint64_t>::max(), "Submission context ids exhausted");
    return id;
}
}

SubmissionQueue::SubmissionQueue(const vk::raii::Device& device, std::uint32_t family)
    : _device {device},
      _queue {device.getQueue(family, 0)},
      _id {nextSubmissionId()}
{
}

SubmissionQueue::~SubmissionQueue() = default;

auto SubmissionQueue::initialize(vk::PhysicalDevice physicalDevice) -> Result<void>
{
    if (*_timeline != vk::Semaphore {})
    {
        return {};
    }
    const auto properties =
        physicalDevice.getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceTimelineSemaphoreProperties>();
    _maximumDifference =
        properties.get<vk::PhysicalDeviceTimelineSemaphoreProperties>().maxTimelineSemaphoreValueDifference;
    if (_maximumDifference == 0)
    {
        return std::unexpected {makeError(ErrorCode::Unsupported, "No outstanding timeline values are supported")};
    }
    const auto type = vk::SemaphoreTypeCreateInfo {.semaphoreType = vk::SemaphoreType::eTimeline};
    const auto info = vk::SemaphoreCreateInfo {.pNext = &type};
    auto semaphore = vk::Semaphore {};
    const auto result = (*_device).createSemaphore(&info, nullptr, &semaphore, *_device.getDispatcher());
    if (result != vk::Result::eSuccess)
    {
        return std::unexpected {makeVulkanError(result)};
    }
    _timeline = vk::raii::Semaphore {_device, semaphore};
    return {};
}

auto SubmissionQueue::refreshCompleted() -> Result<void>
{
    const auto read = _timeline.getCounterValue();
    if (read.result != vk::Result::eSuccess)
    {
        _deviceLost = read.result == vk::Result::eErrorDeviceLost;
        return std::unexpected {makeVulkanError(read.result)};
    }
    if (read.value < _completed || read.value > _committed)
    {
        return std::unexpected {makeError(ErrorCode::BackendFailure, "Timeline counter is outside committed work")};
    }
    _completed = read.value;
    return {};
}

auto SubmissionQueue::prepareSubmit() -> Result<void>
{
    expect(*_timeline != vk::Semaphore {}, "Submission queue has no timeline");
    if (_deviceLost)
    {
        return std::unexpected {makeVulkanError(vk::Result::eErrorDeviceLost)};
    }
    if (_committed == std::numeric_limits<std::uint64_t>::max())
    {
        return std::unexpected {makeError(ErrorCode::ResourceExhausted, "Timeline values exhausted")};
    }
    return ensureOutstandingCapacity();
}

auto SubmissionQueue::ensureOutstandingCapacity() -> Result<void>
{
    if (_committed - _completed >= _maximumDifference)
    {
        const auto refreshed = refreshCompleted();
        if (!refreshed)
        {
            return refreshed;
        }
        if (_committed - _completed >= _maximumDifference)
        {
            return std::unexpected {
                makeError(ErrorCode::ResourceExhausted, "Outstanding timeline value limit reached")};
        }
    }
    return {};
}

auto SubmissionQueue::makeWaitInfos(const SubmissionSync& sync) -> WaitInfos
{
    auto waits = std::array<vk::SemaphoreSubmitInfo, 2> {};
    auto waitCount = std::uint32_t {};
    if (sync.acquire)
    {
        waits[waitCount++] =
            vk::SemaphoreSubmitInfo {.semaphore = sync.acquire, .stageMask = vk::PipelineStageFlagBits2::eAllCommands};
    }
    if (sync.externalWait.semaphore)
    {
        waits[waitCount++] = sync.externalWait;
    }
    return {waits, waitCount};
}

auto SubmissionQueue::makeSignalInfos(const SubmissionSync& sync, std::uint64_t value) const -> SignalInfos
{
    auto signals = std::array<vk::SemaphoreSubmitInfo, 3> {};
    auto signalCount = std::uint32_t {};
    if (sync.finished)
    {
        signals[signalCount++] =
            vk::SemaphoreSubmitInfo {.semaphore = sync.finished, .stageMask = vk::PipelineStageFlagBits2::eAllCommands};
    }
    if (sync.externalSignal.semaphore)
    {
        signals[signalCount++] = sync.externalSignal;
    }
    signals[signalCount++] = vk::SemaphoreSubmitInfo {.semaphore = *_timeline,
                                                      .value = value,
                                                      .stageMask = vk::PipelineStageFlagBits2::eAllCommands};
    return {signals, signalCount};
}

auto SubmissionQueue::submitValue(vk::CommandBuffer command, const SubmissionSync& sync, std::uint64_t value) const
    -> vk::Result
{
    const auto [waits, waitCount] = makeWaitInfos(sync);
    const auto [signals, signalCount] = makeSignalInfos(sync, value);
    const auto buffer = vk::CommandBufferSubmitInfo {.commandBuffer = command};
    const auto info = vk::SubmitInfo2 {.waitSemaphoreInfoCount = waitCount,
                                       .pWaitSemaphoreInfos = waitCount > 0 ? waits.data() : nullptr,
                                       .commandBufferInfoCount = command ? 1U : 0U,
                                       .pCommandBufferInfos = command ? &buffer : nullptr,
                                       .signalSemaphoreInfoCount = signalCount,
                                       .pSignalSemaphoreInfos = signals.data()};
    return _queue.submit2(info);
}

auto SubmissionQueue::submit(vk::CommandBuffer command, vk::Semaphore acquire, vk::Semaphore finished)
    -> Result<std::uint64_t>
{
    return submit(command, SubmissionSync {.acquire = acquire, .finished = finished});
}

auto SubmissionQueue::submitExternalWait(vk::CommandBuffer command, vk::Semaphore semaphore, std::uint64_t value)
    -> Result<std::uint64_t>
{
    if (!semaphore || value == 0)
    {
        return std::unexpected {
            makeError(ErrorCode::InvalidArgument, "External timeline wait needs a semaphore and value")};
    }
    return submit(command,
                  SubmissionSync {
                      .externalWait = vk::SemaphoreSubmitInfo {.semaphore = semaphore,
                                                               .value = value,
                                                               .stageMask = vk::PipelineStageFlagBits2::eAllCommands}
    });
}

auto SubmissionQueue::submitExternalSignal(vk::CommandBuffer command, vk::Semaphore semaphore, std::uint64_t value)
    -> Result<std::uint64_t>
{
    if (!semaphore || value == 0)
    {
        return std::unexpected {
            makeError(ErrorCode::InvalidArgument, "External timeline signal needs a semaphore and value")};
    }
    return submit(
        command,
        SubmissionSync {
            .externalSignal = vk::SemaphoreSubmitInfo {.semaphore = semaphore,
                                                       .value = value,
                                                       .stageMask = vk::PipelineStageFlagBits2::eAllCommands}
    });
}

auto SubmissionQueue::submit(vk::CommandBuffer command, const SubmissionSync& sync) -> Result<std::uint64_t>
{
    const auto ready = prepareSubmit();
    if (!ready)
    {
        return std::unexpected {ready.error()};
    }
    const auto value = _committed + 1;
    const auto result = submitValue(command, sync, value);
    if (result != vk::Result::eSuccess)
    {
        _deviceLost = result == vk::Result::eErrorDeviceLost;
        return std::unexpected {makeVulkanError(result)};
    }
    _committed = value;
    return value;
}

auto SubmissionQueue::getCompletedValue() const -> std::uint64_t
{
    return _completed;
}

auto SubmissionQueue::isDeviceLost() const -> bool
{
    return _deviceLost;
}

auto SubmissionQueue::getPoint(std::uint64_t value) const -> CompletionPoint
{
    expect(value != 0 && value <= _committed, "Completion point must identify committed work");
    auto point = CompletionPoint {};
    point._contextId = _id;
    point._value = value;
    return point;
}

auto SubmissionQueue::validatePoint(CompletionPoint point) const -> Result<void>
{
    if (!point.isValid() || point._contextId != _id || point._value > _committed)
    {
        return std::unexpected {
            makeError(ErrorCode::InvalidArgument, "Completion point belongs to no work in this Context")};
    }
    return {};
}

auto SubmissionQueue::poll(CompletionPoint point) -> Result<bool>
{
    const auto valid = validatePoint(point);
    if (!valid)
    {
        return std::unexpected {valid.error()};
    }
    if (_deviceLost)
    {
        return std::unexpected {makeVulkanError(vk::Result::eErrorDeviceLost)};
    }
    return refreshCompleted().transform([&] {
        return _completed >= point._value;
    });
}

auto SubmissionQueue::waitValue(std::uint64_t timelineValue, std::uint64_t timeoutNanoseconds) -> Result<WaitStatus>
{
    expect(timelineValue <= _committed, "Cannot wait for uncommitted work");
    if (_deviceLost)
    {
        return std::unexpected {makeVulkanError(vk::Result::eErrorDeviceLost)};
    }
    if (timelineValue <= _completed)
    {
        return WaitStatus::Completed;
    }
    const auto semaphore = *_timeline;
    const auto info = vk::SemaphoreWaitInfo {.semaphoreCount = 1, .pSemaphores = &semaphore, .pValues = &timelineValue};
    const auto result = _device.waitSemaphores(info, timeoutNanoseconds);
    if (result == vk::Result::eTimeout)
    {
        return WaitStatus::Timeout;
    }
    if (result != vk::Result::eSuccess)
    {
        _deviceLost = result == vk::Result::eErrorDeviceLost;
        return std::unexpected {makeVulkanError(result)};
    }
    _completed = std::max(_completed, timelineValue);
    return WaitStatus::Completed;
}

auto SubmissionQueue::wait(CompletionPoint point, std::uint64_t timeout) -> Result<WaitStatus>
{
    return validatePoint(point).and_then([&] {
        return waitValue(point._value, timeout);
    });
}
}
