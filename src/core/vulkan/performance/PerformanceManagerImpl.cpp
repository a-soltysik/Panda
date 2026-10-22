// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include "PerformanceManagerImpl.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iterator>
#include <limits>
#include <optional>
#include <panda/Error.hpp>
#include <panda/PerformanceManager.hpp>
#include <vector>

#include "SubmissionQueue.hpp"

namespace panda::detail
{
PerformanceManagerImpl::PerformanceManagerImpl(const vk::raii::Device& device,
                                               SubmissionQueue* submissions,
                                               TimestampProperties timestamps,
                                               bool enableFramePerformanceSampling)
    : _device {device},
      _submissions {submissions},
      _capabilities {.hasGpuTimestampQueries = timestamps.validBits != 0,
                     .isGpuTimestampSamplingEnabled = enableFramePerformanceSampling && timestamps.validBits != 0},
      _timestampValidBits {timestamps.validBits},
      _timestampPeriodNanoseconds {timestamps.periodNanoseconds}
{
}

auto PerformanceManagerImpl::initialize() -> Result<void>
{
    if (!_capabilities.isGpuTimestampSamplingEnabled)
    {
        return {};
    }
    if (_timestampValidBits > std::numeric_limits<std::uint64_t>::digits ||
        !std::isfinite(_timestampPeriodNanoseconds) || _timestampPeriodNanoseconds <= 0.0F)
    {
        return std::unexpected {makeError(ErrorCode::BackendFailure, "Invalid Vulkan timestamp properties")};
    }
    static constexpr auto queryInfo = vk::QueryPoolCreateInfo {.queryType = vk::QueryType::eTimestamp, .queryCount = 2};
    for (auto& slot : _slots)
    {
        auto queryPool = vk::QueryPool {};
        const auto created = (*_device).createQueryPool(&queryInfo, nullptr, &queryPool, *_device.getDispatcher());
        if (created != vk::Result::eSuccess)
        {
            return std::unexpected {makeVulkanError(created)};
        }
        slot.pool = vk::raii::QueryPool {_device, queryPool};
    }
    return {};
}

auto PerformanceManagerImpl::getCapabilities() const noexcept -> PerformanceCapabilities
{
    return _capabilities;
}

auto PerformanceManagerImpl::duration(std::uint64_t start, std::uint64_t end) const -> Result<std::chrono::nanoseconds>
{
    const auto validBits = _timestampValidBits;
    const auto mask = validBits == std::numeric_limits<std::uint64_t>::digits
                          ? std::numeric_limits<std::uint64_t>::max()
                          : (std::uint64_t {1} << validBits) - 1;
    const auto ticks = static_cast<double>((end - start) & mask);
    const auto nanoseconds = std::round(ticks * static_cast<double>(_timestampPeriodNanoseconds));
    if (!std::isfinite(nanoseconds) || nanoseconds < 0.0 ||
        nanoseconds >= static_cast<double>(std::numeric_limits<std::chrono::nanoseconds::rep>::max()))
    {
        return std::unexpected {makeError(ErrorCode::BackendFailure, "GPU timestamp duration is out of range")};
    }
    return std::chrono::nanoseconds {static_cast<std::chrono::nanoseconds::rep>(nanoseconds)};
}

auto PerformanceManagerImpl::collect(std::size_t slotIndex) -> Result<bool>
{
    auto& slot = _slots.at(slotIndex);
    if (!slot.submitted)
    {
        return false;
    }
    const auto completed = isSubmissionComplete(slot);
    if (!completed)
    {
        return std::unexpected {completed.error()};
    }
    if (!*completed)
    {
        return false;
    }
    return collectCompleted(slot);
}

auto PerformanceManagerImpl::isSubmissionComplete(const QuerySlot& slot) -> Result<bool>
{
    if (_submissions == nullptr)
    {
        return std::unexpected {makeError(ErrorCode::BackendFailure, "Submitted timestamp sample has no queue")};
    }
    return _submissions->poll(slot.submissionPoint);
}

auto PerformanceManagerImpl::collectCompleted(QuerySlot& slot) -> Result<bool>
{
    const auto values = readReadyTimestamps(slot);
    if (!values)
    {
        return std::unexpected {values.error()};
    }
    if (!*values)
    {
        return false;
    }
    const auto measured = duration(values->value().at(0), values->value().at(2));
    if (!measured)
    {
        return std::unexpected {measured.error()};
    }
    const auto frameIndex = slot.frameIndex;
    _readySamples.push_back({
        .frameIndex = frameIndex,
        .sample = {.frameIndex = frameIndex,
                   .cpuFrameDuration = slot.cpuFrameDuration,
                   .gpuExecutionDuration = *measured}
    });
    slot.submitted = false;
    return true;
}

auto PerformanceManagerImpl::readReadyTimestamps(const QuerySlot& slot)
    -> Result<std::optional<std::array<std::uint64_t, 4>>>
{
    auto values = std::array<std::uint64_t, 4> {};
    static constexpr auto resultFlags = vk::QueryResultFlagBits::e64 | vk::QueryResultFlagBits::eWithAvailability;
    const auto result = (*_device).getQueryPoolResults(*slot.pool,
                                                       0,
                                                       2,
                                                       sizeof(values),
                                                       values.data(),
                                                       sizeof(std::uint64_t) * 2,
                                                       resultFlags);
    if (result != vk::Result::eSuccess && result != vk::Result::eNotReady)
    {
        return std::unexpected {makeVulkanError(result)};
    }
    if (values.at(1) == 0 || values.at(3) == 0)
    {
        return std::optional<std::array<std::uint64_t, 4>> {};
    }
    return std::optional {values};
}

auto PerformanceManagerImpl::recordStart(std::size_t slotIndex, vk::CommandBuffer command) -> Result<void>
{
    if (!_capabilities.isGpuTimestampSamplingEnabled)
    {
        return {};
    }
    auto& slot = _slots.at(slotIndex);
    const auto collected = collect(slotIndex);
    if (!collected)
    {
        return std::unexpected {collected.error()};
    }
    if (!*collected && slot.submitted)
    {
        return std::unexpected {
            makeError(ErrorCode::BackendFailure, "A completed frame's timestamp queries are not available")};
    }
    command.resetQueryPool(*slot.pool, 0, 2);
    command.writeTimestamp2(vk::PipelineStageFlagBits2::eColorAttachmentOutput, *slot.pool, 0);
    return {};
}

void PerformanceManagerImpl::recordEnd(std::size_t slotIndex, vk::CommandBuffer command)
{
    if (_capabilities.isGpuTimestampSamplingEnabled)
    {
        command.writeTimestamp2(vk::PipelineStageFlagBits2::eColorAttachmentOutput, *_slots.at(slotIndex).pool, 1);
    }
}

void PerformanceManagerImpl::markSubmitted(std::size_t slotIndex, CompletionPoint submissionPoint) noexcept
{
    if (_capabilities.isGpuTimestampSamplingEnabled)
    {
        auto& slot = _slots.at(slotIndex);
        slot.submitted = true;
        slot.submissionPoint = submissionPoint;
        slot.frameIndex = _nextFrameIndex++;
        slot.cpuFrameDuration = {};
    }
}

void PerformanceManagerImpl::recordCpuFrameDuration(std::size_t slotIndex, std::chrono::nanoseconds duration) noexcept
{
    if (_capabilities.isGpuTimestampSamplingEnabled)
    {
        auto& slot = _slots.at(slotIndex);
        if (slot.submitted)
        {
            slot.cpuFrameDuration = duration;
        }
    }
}

auto PerformanceManagerImpl::collectAvailableSamples() -> Result<void>
{
    for (auto index = std::size_t {}; index < _slots.size(); ++index)
    {
        const auto collected = collect(index);
        if (!collected)
        {
            return std::unexpected {collected.error()};
        }
    }
    std::ranges::sort(_readySamples, {}, &ReadySample::frameIndex);
    return {};
}

auto PerformanceManagerImpl::getLatestSample() -> Result<std::optional<FramePerformanceSample>>
{
    if (const auto collected = collectAvailableSamples(); !collected)
    {
        return std::unexpected {collected.error()};
    }
    if (_readySamples.empty())
    {
        return std::optional<FramePerformanceSample> {};
    }
    return _readySamples.back().sample;
}

auto PerformanceManagerImpl::takeNextSample() -> Result<std::optional<FramePerformanceSample>>
{
    if (const auto collected = collectAvailableSamples(); !collected)
    {
        return std::unexpected {collected.error()};
    }
    if (_readySamples.empty())
    {
        return std::optional<FramePerformanceSample> {};
    }
    const auto sample = _readySamples.front().sample;
    _readySamples.erase(_readySamples.begin());
    return std::optional<FramePerformanceSample> {sample};
}

auto PerformanceManagerImpl::takeAvailableSamples() -> Result<std::vector<FramePerformanceSample>>
{
    if (const auto collected = collectAvailableSamples(); !collected)
    {
        return std::unexpected {collected.error()};
    }
    auto samples = std::vector<FramePerformanceSample> {};
    samples.reserve(_readySamples.size());
    std::ranges::transform(_readySamples, std::back_inserter(samples), [](const ReadySample& ready) {
        return ready.sample;
    });
    _readySamples.clear();
    return samples;
}
}
