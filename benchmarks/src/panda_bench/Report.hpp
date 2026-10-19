#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <panda/PerformanceManager.hpp>
#include <string>
#include <vector>

namespace panda_bench
{
struct RunReport
{
    std::uint32_t runIndex {0};
    std::uint32_t framebufferWidth {0};
    std::uint32_t framebufferHeight {0};
    std::chrono::nanoseconds startupDuration {};
    std::chrono::nanoseconds warmupDuration {};
    std::chrono::nanoseconds measurementWallDuration {};
    std::chrono::nanoseconds completionWaitDuration {};
    std::chrono::nanoseconds sampleDrainDuration {};
    std::chrono::nanoseconds teardownDuration {};
    std::uint64_t warmupSubmittedFrames {0};
    std::uint64_t measurementSubmittedFrames {0};
    std::vector<panda::FramePerformanceSample> frameSamples;
};

struct DriverReport
{
    std::string name;
    std::string info;
    std::uint32_t version {0};
};

struct Report
{
    std::string revision;
    std::string operatingSystem;
    std::string compiler;
    std::string buildMode;
    std::optional<std::string> cpuModel;
    std::optional<std::string> gpuModel;
    std::optional<DriverReport> driver;
    std::optional<std::uint32_t> vulkanApiVersion;
    std::uint32_t requestedWindowWidth {0};
    std::uint32_t requestedWindowHeight {0};
    std::chrono::seconds requestedWarmup {0};
    std::chrono::seconds requestedMeasurement {0};
    std::uint64_t requestedRuns {0};
    std::vector<RunReport> runs;
    std::string outcome {"complete"};
    std::optional<std::string> outcomeMessage;
};

[[nodiscard]] auto serialize(const Report& report) -> std::string;
}
