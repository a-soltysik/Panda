#pragma once

/// @file
/// Public access to asynchronous performance measurements.

#include <chrono>
#include <cstdint>
#include <optional>
#include <panda/Error.hpp>
#include <vector>

namespace panda
{
class Context;

namespace detail
{
class PerformanceManagerImpl;
}

/// @brief Performance measurement capabilities of the selected graphics queue.
struct PerformanceCapabilities
{
    /// Whether the graphics queue can record GPU timestamp queries.
    bool hasGpuTimestampQueries {false};
    /// Whether GPU timestamp sampling is enabled for this Context.
    bool isGpuTimestampSamplingEnabled {false};
};

/// @brief Completed CPU and GPU timings for a submitted frame.
struct FramePerformanceSample
{
    /// Zero-based index of the submitted frame within this Context.
    std::uint64_t frameIndex {0};
    /// CPU wall duration of Panda's frame call, including waits and queuing presentation; excludes caller work and display latency.
    std::chrono::nanoseconds cpuFrameDuration {};
    /// GPU time from the start to the end of this frame's rendering work, in nanoseconds.
    std::chrono::nanoseconds gpuExecutionDuration {};
};

/// @brief Context-owned access to available performance samples.
/// Calls are serialized with other operations on the owning Context. This object
/// is borrowed from Context and must not outlive it.
class PerformanceManager final
{
public:
    PerformanceManager(const PerformanceManager&) = delete;
    auto operator=(const PerformanceManager&) -> PerformanceManager& = delete;
    PerformanceManager(PerformanceManager&&) = delete;
    auto operator=(PerformanceManager&&) -> PerformanceManager& = delete;
    ~PerformanceManager() = default;

    /// @brief Copies timestamp support and sampling state for this Context.
    /// @return GPU support and whether sampling is enabled; does not query or wait for GPU work.
    [[nodiscard]] auto getCapabilities() const noexcept -> PerformanceCapabilities;

    /// @brief Peeks at the newest frame sample whose GPU timestamps are ready.
    /// @return A copy of the sample with the greatest frame index, or empty if sampling is disabled,
    /// unsupported, or no sample is ready; this call does not remove samples or wait for GPU work.
    /// Drain samples with a take method to keep the manager's pending sample storage bounded.
    [[nodiscard]] auto getLatestSample() -> Result<std::optional<FramePerformanceSample>>;

    /// @brief Takes the oldest frame sample whose GPU timestamps are ready.
    /// @return The sample with the smallest frame index, removed from the manager, or empty if sampling is
    /// disabled, unsupported, or no sample is ready; this call does not wait for GPU work.
    [[nodiscard]] auto takeNextSample() -> Result<std::optional<FramePerformanceSample>>;

    /// @brief Drains frame samples whose GPU timestamp queries are already available.
    /// @return Ready samples, or a backend error. This call does not wait for GPU work;
    /// samples remain buffered until returned. The result may allocate. The vector is empty
    /// if sampling is disabled, unsupported, or no sample is ready.
    [[nodiscard]] auto takeAvailableSamples() -> Result<std::vector<FramePerformanceSample>>;

private:
    friend class Context;

    explicit PerformanceManager(detail::PerformanceManagerImpl& implementation) noexcept;

    detail::PerformanceManagerImpl* _implementation;
};

}
