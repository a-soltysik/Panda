#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <panda/PerformanceManager.hpp>
#include <panda_bench/Statistics.hpp>
#include <span>
#include <vector>

namespace panda_bench
{
namespace
{
constexpr auto medianPercentile = 0.50;
constexpr auto ninetyFifthPercentile = 0.95;
constexpr auto ninetyNinthPercentile = 0.99;

auto nearestRank(const std::vector<std::chrono::nanoseconds>& sorted, double quantile) -> std::chrono::nanoseconds
{
    const auto rank = static_cast<std::size_t>(std::ceil(quantile * static_cast<double>(sorted.size())));
    return sorted.at(std::max(std::size_t {1}, rank) - 1);
}

auto summarizeDurations(std::span<const panda::FramePerformanceSample> samples,
                        std::chrono::nanoseconds panda::FramePerformanceSample::* duration)
    -> std::optional<SampleStatistics>
{
    if (samples.empty())
    {
        return std::nullopt;
    }
    auto durations = std::vector<std::chrono::nanoseconds> {};
    durations.reserve(samples.size());
    auto totalNanoseconds = std::uint64_t {};
    for (const auto& sample : samples)
    {
        const auto value = sample.*duration;
        durations.push_back(value);
        totalNanoseconds += static_cast<std::uint64_t>(value.count());
    }
    std::ranges::sort(durations);
    const std::uint64_t sampleCount {durations.size()};
    const auto meanWholeNanoseconds = totalNanoseconds / sampleCount;
    const auto meanRemainderNanoseconds = totalNanoseconds % sampleCount;
    const auto meanNanoseconds = static_cast<double>(meanWholeNanoseconds) +
                                 (static_cast<double>(meanRemainderNanoseconds) / static_cast<double>(sampleCount));
    return SampleStatistics {.sampleCount = durations.size(),
                             .meanNanoseconds = meanNanoseconds,
                             .p50 = nearestRank(durations, medianPercentile),
                             .p95 = nearestRank(durations, ninetyFifthPercentile),
                             .p99 = nearestRank(durations, ninetyNinthPercentile),
                             .minimum = durations.front(),
                             .maximum = durations.back()};
}
}

auto summarizeCpuFrameDuration(std::span<const panda::FramePerformanceSample> samples)
    -> std::optional<SampleStatistics>
{
    return summarizeDurations(samples, &panda::FramePerformanceSample::cpuFrameDuration);
}

auto summarizeGpuExecutionDuration(std::span<const panda::FramePerformanceSample> samples)
    -> std::optional<SampleStatistics>
{
    return summarizeDurations(samples, &panda::FramePerformanceSample::gpuExecutionDuration);
}
}
