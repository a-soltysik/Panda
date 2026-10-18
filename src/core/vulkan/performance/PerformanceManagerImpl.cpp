// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include "PerformanceManagerImpl.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <panda/PerformanceManager.hpp>
#include <ranges>
#include <utility>
#include <vector>

#include "VulkanError.hpp"

namespace panda::detail
{
PerformanceManagerImpl::PerformanceManagerImpl(const vk::raii::Device& device,
                                               std::uint32_t timestampValidBits,
                                               double timestampPeriodNanoseconds,
                                               bool enableGpuTimestampSampling)
    : _device {device},
      _capabilities {.hasGpuTimestampQueries = timestampValidBits != 0,
                     .isGpuTimestampSamplingEnabled = enableGpuTimestampSampling && timestampValidBits != 0},
      _timestampValidBits {timestampValidBits},
      _timestampPeriodNanoseconds {timestampPeriodNanoseconds}
{
}

auto PerformanceManagerImpl::initialize() -> Result<void>
{
    if (!_capabilities.isGpuTimestampSamplingEnabled)
    {
        return {};
    }
    if (_timestampValidBits > 64 || !std::isfinite(_timestampPeriodNanoseconds) || _timestampPeriodNanoseconds <= 0.0)
    {
        return std::unexpected {makeError(ErrorCode::BackendFailure, "Invalid Vulkan timestamp properties")};
    }
    static constexpr auto queryInfo = vk::QueryPoolCreateInfo {.queryType = vk::QueryType::eTimestamp, .queryCount = 2};
    for (auto& slot : _slots)
    {
        auto pool = core::vulkan::detail::checkedCreation(_device.createQueryPool(queryInfo));
        if (!pool)
        {
            return std::unexpected {pool.error()};
        }
        slot.pool = std::move(*pool);
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
    const auto mask =
        validBits == 64 ? std::numeric_limits<std::uint64_t>::max() : (std::uint64_t {1} << validBits) - 1;
    const auto ticks = static_cast<double>((end - start) & mask);
    const auto nanoseconds = std::round(ticks * _timestampPeriodNanoseconds);
    if (!std::isfinite(nanoseconds) || nanoseconds < 0.0 ||
        nanoseconds > static_cast<double>(std::numeric_limits<std::chrono::nanoseconds::rep>::max()))
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
    if (values[1] == 0 || values[3] == 0)
    {
        return false;
    }
    const auto measured = duration(values[0], values[2]);
    if (!measured)
    {
        return std::unexpected {measured.error()};
    }
    const auto frameIndex = slot.frameIndex;
    _readySamples.push_back({
        .frameIndex = frameIndex,
        .sample = {.frameIndex = frameIndex, .gpuExecutionDuration = *measured}
    });
    slot.submitted = false;
    return true;
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

void PerformanceManagerImpl::markSubmitted(std::size_t slotIndex) noexcept
{
    if (_capabilities.isGpuTimestampSamplingEnabled)
    {
        auto& slot = _slots.at(slotIndex);
        slot.submitted = true;
        slot.frameIndex = _nextFrameIndex++;
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
    auto sample = std::move(_readySamples.front().sample);
    _readySamples.erase(_readySamples.begin());
    return std::optional<FramePerformanceSample> {std::move(sample)};
}

auto PerformanceManagerImpl::takeAvailableSamples() -> Result<std::vector<FramePerformanceSample>>
{
    if (const auto collected = collectAvailableSamples(); !collected)
    {
        return std::unexpected {collected.error()};
    }
    auto samples = std::vector<FramePerformanceSample> {};
    samples.reserve(_readySamples.size());
    for (auto& ready : _readySamples)
    {
        samples.push_back(std::move(ready.sample));
    }
    _readySamples.clear();
    return samples;
}
}
