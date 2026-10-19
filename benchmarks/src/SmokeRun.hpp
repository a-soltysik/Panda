#pragma once

#include <chrono>
#include <cstdint>
#include <expected>
#include <vector>

#include "SmokeRunBackend.hpp"

namespace panda_bench
{
class SmokeRun final
{
public:
    SmokeRun(const Options& options, RunReport& run, Report& report, SmokeRunBackend& backend);

    auto execute() -> std::expected<void, Failure>;

private:
    auto executeSafely() -> std::expected<void, Failure>;
    auto executeRun() -> std::expected<void, Failure>;
    auto initializeTimed() -> std::expected<void, Failure>;
    auto initialize() -> std::expected<void, Failure>;
    auto executePhases() -> std::expected<void, Failure>;
    auto executePhase(std::chrono::seconds duration, bool measuring) -> std::expected<void, Failure>;
    void recordPhaseDuration(std::chrono::steady_clock::time_point start, bool measuring);
    auto executeIteration(bool measuring) -> std::expected<void, Failure>;
    auto verifyWindowState() -> std::expected<void, Failure>;
    auto checkExtent() -> std::expected<void, Failure>;
    auto submitFrame(bool measuring) -> std::expected<void, Failure>;
    auto collectAvailableSamples(bool measuring, std::uint64_t endFrame) -> std::expected<void, Failure>;
    void appendMeasurementSamples(const std::vector<panda::FramePerformanceSample>& samples, std::uint64_t endFrame);
    auto processWindowEvents() -> std::expected<void, Failure>;
    auto finishMeasurement() -> std::expected<void, Failure>;
    auto waitForLastFrame() -> std::expected<void, Failure>;
    auto drainSamples() -> std::expected<void, Failure>;
    auto verifySampleCount() const -> std::expected<void, Failure>;

    const Options& _options;
    Report& _report;
    RunReport& _run;
    SmokeRunBackend& _backend;
    panda::FramebufferExtent _initialExtent {};
    std::uint64_t _submittedFrameCount {0};
    std::uint64_t _firstMeasurementFrame {0};
    std::uint64_t _measurementEndFrame {0};
    bool _hasSubmittedFrame {false};
};
}
