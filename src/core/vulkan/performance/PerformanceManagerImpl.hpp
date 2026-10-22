#pragma once

// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <panda/CompletionPoint.hpp>
#include <panda/PerformanceManager.hpp>
#include <vector>

#include "SubmissionQueue.hpp"

namespace panda::detail
{
struct TimestampProperties
{
    std::uint32_t validBits {0};
    float periodNanoseconds {0.0F};
};

class PerformanceManagerImpl final
{
public:
    PerformanceManagerImpl(const vk::raii::Device& device,
                           SubmissionQueue* submissions,
                           TimestampProperties timestamps,
                           bool enableFramePerformanceSampling);

    [[nodiscard]] auto initialize() -> Result<void>;
    [[nodiscard]] auto getCapabilities() const noexcept -> PerformanceCapabilities;
    [[nodiscard]] auto getLatestSample() -> Result<std::optional<FramePerformanceSample>>;
    [[nodiscard]] auto takeNextSample() -> Result<std::optional<FramePerformanceSample>>;
    [[nodiscard]] auto takeAvailableSamples() -> Result<std::vector<FramePerformanceSample>>;
    [[nodiscard]] auto recordStart(std::size_t slotIndex, vk::CommandBuffer command) -> Result<void>;
    void recordEnd(std::size_t slotIndex, vk::CommandBuffer command);
    void markSubmitted(std::size_t slotIndex, CompletionPoint submissionPoint) noexcept;
    void recordCpuFrameDuration(std::size_t slotIndex, std::chrono::nanoseconds duration) noexcept;

private:
    struct QuerySlot
    {
        vk::raii::QueryPool pool {nullptr};
        bool submitted {false};
        CompletionPoint submissionPoint;
        std::uint64_t frameIndex {0};
        std::chrono::nanoseconds cpuFrameDuration {};
    };

    struct ReadySample
    {
        std::uint64_t frameIndex {0};
        FramePerformanceSample sample;
    };

    [[nodiscard]] auto collect(std::size_t slotIndex) -> Result<bool>;
    [[nodiscard]] auto isSubmissionComplete(const QuerySlot& slot) -> Result<bool>;
    [[nodiscard]] auto collectCompleted(QuerySlot& slot) -> Result<bool>;
    [[nodiscard]] auto readReadyTimestamps(const QuerySlot& slot)
        -> Result<std::optional<std::array<std::uint64_t, 4>>>;
    [[nodiscard]] auto collectAvailableSamples() -> Result<void>;
    [[nodiscard]] auto duration(std::uint64_t start, std::uint64_t end) const -> Result<std::chrono::nanoseconds>;

    const vk::raii::Device& _device;
    SubmissionQueue* _submissions;
    PerformanceCapabilities _capabilities;
    std::uint32_t _timestampValidBits;
    float _timestampPeriodNanoseconds;
    std::array<QuerySlot, 2> _slots;
    std::vector<ReadySample> _readySamples;
    std::uint64_t _nextFrameIndex {0};
};
}
