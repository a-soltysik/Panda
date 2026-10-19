#pragma once

#include <chrono>
#include <cstdint>
#include <expected>
#include <optional>
#include <panda/CompletionPoint.hpp>
#include <panda/Context.hpp>
#include <panda/PerformanceManager.hpp>
#include <panda/WindowSurface.hpp>
#include <panda/tools/gui/Window.hpp>
#include <panda_bench/Arguments.hpp>
#include <panda_bench/Report.hpp>
#include <string>
#include <vector>

namespace panda_bench
{
struct Failure
{
    std::string outcome {"failed"};
    std::string message;
};

class SmokeRun final
{
public:
    SmokeRun(const Options& options, RunReport& run, Report& report);

    auto execute() -> std::expected<void, Failure>;

private:
    auto checkedWindow() -> panda::tools::Window&;
    auto checkedContext() -> panda::Context&;
    auto executeSafely() -> std::expected<void, Failure>;
    auto executeRun() -> std::expected<void, Failure>;
    auto initializeTimed() -> std::expected<void, Failure>;
    auto initialize() -> std::expected<void, Failure>;
    auto createWindow() -> std::expected<void, Failure>;
    auto createContext() -> std::expected<void, Failure>;
    void recordDeviceInfo();
    auto checkPerformanceCapabilities() -> std::expected<void, Failure>;
    auto captureInitialExtent() -> std::expected<void, Failure>;
    auto executePhases() -> std::expected<void, Failure>;
    auto executePhase(std::chrono::seconds duration, bool measuring) -> std::expected<void, Failure>;
    void recordPhaseDuration(std::chrono::steady_clock::time_point start, bool measuring);
    auto executeIteration(bool measuring) -> std::expected<void, Failure>;
    auto verifyWindowState() -> std::expected<void, Failure>;
    auto checkExtent() -> std::expected<void, Failure>;
    auto submitFrame(bool measuring) -> std::expected<void, Failure>;
    void recordSubmission(const panda::FrameResult& frame, bool measuring);
    auto collectAvailableSamples(bool measuring, std::uint64_t endFrame) -> std::expected<void, Failure>;
    void appendMeasurementSamples(const std::vector<panda::FramePerformanceSample>& samples, std::uint64_t endFrame);
    auto processWindowEvents() -> std::expected<void, Failure>;
    auto finishMeasurement() -> std::expected<void, Failure>;
    auto waitForLastFrame() -> std::expected<void, Failure>;
    auto drainSamples() -> std::expected<void, Failure>;
    auto verifySampleCount() const -> std::expected<void, Failure>;
    void teardown();

    const Options& _options;
    Report& _report;
    RunReport& _run;
    std::optional<panda::tools::Window> _window;
    std::optional<panda::Context> _context;
    panda::FramebufferExtent _initialExtent {};
    panda::CompletionPoint _lastCompletion;
    std::uint64_t _submittedFrameCount {0};
    std::uint64_t _firstMeasurementFrame {0};
    std::uint64_t _measurementEndFrame {0};
};
}
