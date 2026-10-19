#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <panda/PerformanceManager.hpp>
#include <span>

namespace panda_bench
{
struct SampleStatistics
{
    std::uint64_t sampleCount {0};
    double meanNanoseconds {0.0};
    std::chrono::nanoseconds p50 {};
    std::chrono::nanoseconds p95 {};
    std::chrono::nanoseconds p99 {};
    std::chrono::nanoseconds minimum {};
    std::chrono::nanoseconds maximum {};
};

[[nodiscard]] auto summarizeCpuFrameDuration(std::span<const panda::FramePerformanceSample> samples)
    -> std::optional<SampleStatistics>;
[[nodiscard]] auto summarizeGpuExecutionDuration(std::span<const panda::FramePerformanceSample> samples)
    -> std::optional<SampleStatistics>;
}
