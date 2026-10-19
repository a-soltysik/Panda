#include "SmokeRun.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <expected>
#include <format>
#include <iterator>
#include <panda/PerformanceManager.hpp>
#include <panda/WindowSurface.hpp>
#include <print>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "SmokeRunBackend.hpp"
#include "panda_bench/Arguments.hpp"
#include "panda_bench/Report.hpp"

namespace panda_bench
{
namespace
{
using Clock = std::chrono::steady_clock;

auto elapsed(Clock::time_point start) -> std::chrono::nanoseconds
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start);
}

auto failure(std::string message) -> Failure
{
    return {.message = std::move(message)};
}

auto invalidRun(std::string message) -> Failure
{
    return {.outcome = "invalid", .message = std::move(message)};
}

auto statusName(FrameStatus status) -> std::string_view
{
    switch (status)
    {
    case FrameStatus::Presented:
        return "presented";
    case FrameStatus::Skipped:
        return "skipped";
    case FrameStatus::Suspended:
        return "suspended";
    case FrameStatus::RecreatePending:
        return "recreate pending";
    }
    return "unknown";
}
}

SmokeRun::SmokeRun(const Options& options, RunReport& run, Report& report, SmokeRunBackend& backend)
    : _options {options},
      _report {report},
      _run {run},
      _backend {backend}
{
}

auto SmokeRun::execute() -> std::expected<void, Failure>
{
    auto execution = executeSafely();
    const auto teardownStart = Clock::now();
    _backend.teardown();
    _run.teardownDuration = elapsed(teardownStart);
    return execution;
}

auto SmokeRun::executeSafely() -> std::expected<void, Failure>
{
    try
    {
        return executeRun();
    }
    catch (const std::exception& error)
    {
        return std::unexpected {failure(error.what())};
    }
    catch (...)
    {
        return std::unexpected {failure("Unexpected benchmark failure")};
    }
}

auto SmokeRun::executeRun() -> std::expected<void, Failure>
{
    if (const auto initialized = initializeTimed(); !initialized)
    {
        return initialized;
    }
    return executePhases();
}

auto SmokeRun::initializeTimed() -> std::expected<void, Failure>
{
    const auto start = Clock::now();
    try
    {
        auto result = initialize();
        _run.startupDuration = elapsed(start);
        return result;
    }
    catch (...)
    {
        _run.startupDuration = elapsed(start);
        throw;
    }
}

auto SmokeRun::initialize() -> std::expected<void, Failure>
{
    const auto environment = _backend.initialize(_options);
    if (!environment)
    {
        return std::unexpected {environment.error()};
    }
    const auto extent = environment->framebufferExtent;
    if (extent.width == 0 || extent.height == 0)
    {
        return std::unexpected {invalidRun("The benchmark window is minimized")};
    }
    _initialExtent = extent;
    _run.framebufferWidth = extent.width;
    _run.framebufferHeight = extent.height;
    _report.gpuModel = environment->gpuModel;
    _report.driver = environment->driver;
    _report.vulkanApiVersion = environment->vulkanApiVersion;
    return {};
}

auto SmokeRun::executePhases() -> std::expected<void, Failure>
{
    std::println("Run {}: warmup ({} s)", _run.runIndex, _options.warmupDuration.count());
    if (const auto warmup = executePhase(_options.warmupDuration, false); !warmup)
    {
        return warmup;
    }
    std::println("Run {}: measuring ({} s)", _run.runIndex, _options.measurementDuration.count());
    if (const auto measurement = executePhase(_options.measurementDuration, true); !measurement)
    {
        return measurement;
    }
    return finishMeasurement();
}

auto SmokeRun::executePhase(std::chrono::seconds duration, bool measuring) -> std::expected<void, Failure>
{
    if (duration.count() == 0)
    {
        return {};
    }
    const auto start = Clock::now();
    if (measuring)
    {
        _firstMeasurementFrame = _submittedFrameCount;
    }
    while (Clock::now() - start < duration)
    {
        if (const auto iteration = executeIteration(measuring); !iteration)
        {
            return iteration;
        }
    }
    recordPhaseDuration(start, measuring);
    return {};
}

void SmokeRun::recordPhaseDuration(std::chrono::steady_clock::time_point start, bool measuring)
{
    const auto duration = elapsed(start);
    if (measuring)
    {
        _run.measurementWallDuration = duration;
        _measurementEndFrame = _submittedFrameCount;
    }
    else
    {
        _run.warmupDuration = duration;
    }
}

auto SmokeRun::executeIteration(bool measuring) -> std::expected<void, Failure>
{
    if (const auto ready = verifyWindowState(); !ready)
    {
        return ready;
    }
    if (const auto submitted = submitFrame(measuring); !submitted)
    {
        return submitted;
    }
    if (const auto samples = collectAvailableSamples(measuring, _submittedFrameCount); !samples)
    {
        return samples;
    }
    return processWindowEvents();
}

auto SmokeRun::verifyWindowState() -> std::expected<void, Failure>
{
    if (_backend.shouldClose())
    {
        return std::unexpected {invalidRun("The benchmark window was closed before the run completed")};
    }
    return checkExtent();
}

auto SmokeRun::checkExtent() -> std::expected<void, Failure>
{
    const auto extent = _backend.getFramebufferExtent();
    if (!extent)
    {
        return std::unexpected {extent.error()};
    }
    if (extent->width == 0 || extent->height == 0)
    {
        return std::unexpected {invalidRun("The benchmark window became minimized")};
    }
    if (extent->width != _initialExtent.width || extent->height != _initialExtent.height)
    {
        return std::unexpected {invalidRun("The framebuffer extent changed during the run")};
    }
    return {};
}

auto SmokeRun::submitFrame(bool measuring) -> std::expected<void, Failure>
{
    const auto frame = _backend.submitFrame();
    if (!frame)
    {
        return std::unexpected {frame.error()};
    }
    if (*frame != FrameStatus::Presented)
    {
        return std::unexpected {
            invalidRun(std::format("The frame was {} during the controlled run", statusName(*frame)))};
    }
    ++_submittedFrameCount;
    _hasSubmittedFrame = true;
    if (measuring)
    {
        ++_run.measurementSubmittedFrames;
    }
    else
    {
        ++_run.warmupSubmittedFrames;
    }
    return {};
}

auto SmokeRun::collectAvailableSamples(bool measuring, std::uint64_t endFrame) -> std::expected<void, Failure>
{
    const auto samples = _backend.takeAvailableSamples();
    if (!samples)
    {
        return std::unexpected {samples.error()};
    }
    if (measuring)
    {
        appendMeasurementSamples(*samples, endFrame);
    }
    return {};
}

void SmokeRun::appendMeasurementSamples(const std::vector<panda::FramePerformanceSample>& samples,
                                        std::uint64_t endFrame)
{
    std::ranges::copy_if(samples, std::back_inserter(_run.frameSamples), [this, endFrame](const auto& sample) {
        return sample.frameIndex >= _firstMeasurementFrame && sample.frameIndex < endFrame;
    });
}

auto SmokeRun::processWindowEvents() -> std::expected<void, Failure>
{
    const auto input = _backend.processInput();
    if (!input)
    {
        return std::unexpected {input.error()};
    }
    return checkExtent();
}

auto SmokeRun::finishMeasurement() -> std::expected<void, Failure>
{
    if (!_hasSubmittedFrame)
    {
        return std::unexpected {invalidRun("No frame was submitted during the run")};
    }
    if (const auto waited = waitForLastFrame(); !waited)
    {
        return waited;
    }
    return drainSamples();
}

auto SmokeRun::waitForLastFrame() -> std::expected<void, Failure>
{
    const auto start = Clock::now();
    const auto waited = _backend.waitForLastFrame(std::chrono::seconds {30});
    _run.completionWaitDuration = elapsed(start);
    return waited;
}

auto SmokeRun::drainSamples() -> std::expected<void, Failure>
{
    const auto start = Clock::now();
    const auto remaining = _backend.takeAvailableSamples();
    _run.sampleDrainDuration = elapsed(start);
    if (!remaining)
    {
        return std::unexpected {remaining.error()};
    }
    appendMeasurementSamples(*remaining, _measurementEndFrame);
    std::ranges::sort(_run.frameSamples, {}, &panda::FramePerformanceSample::frameIndex);
    return verifySampleCount();
}

auto SmokeRun::verifySampleCount() const -> std::expected<void, Failure>
{
    if (_run.frameSamples.size() != _run.measurementSubmittedFrames)
    {
        return std::unexpected {invalidRun(std::format("Expected {} frame samples but got {}",
                                                       _run.measurementSubmittedFrames,
                                                       _run.frameSamples.size()))};
    }
    return {};
}
}
