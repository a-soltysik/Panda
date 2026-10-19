#include "SmokeRun.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <expected>
#include <format>
#include <glm/ext/vector_uint2.hpp>
#include <iterator>
#include <optional>
#include <panda/CompletionPoint.hpp>
#include <panda/Context.hpp>
#include <panda/Error.hpp>
#include <panda/PerformanceManager.hpp>
#include <panda/WindowSurface.hpp>
#include <panda/tools/gui/Window.hpp>
#include <panda_bench/Arguments.hpp>
#include <panda_bench/Report.hpp>
#include <print>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

auto unsupported(std::string message) -> Failure
{
    return {.outcome = "unsupported", .message = std::move(message)};
}

auto failFromPanda(const panda::Error& error) -> Failure
{
    if (error.code == panda::ErrorCode::Unsupported)
    {
        return unsupported(error.message);
    }
    return failure(error.message);
}

auto statusName(panda::FrameResult::Status status) -> std::string_view
{
    switch (status)
    {
    case panda::FrameResult::Status::Presented:
        return "presented";
    case panda::FrameResult::Status::Skipped:
        return "skipped";
    case panda::FrameResult::Status::Suspended:
        return "suspended";
    case panda::FrameResult::Status::RecreatePending:
        return "recreate pending";
    }
    return "unknown";
}

}

SmokeRun::SmokeRun(const Options& options, RunReport& run, Report& report)
    : _options {options},
      _report {report},
      _run {run}
{
}

auto SmokeRun::execute() -> std::expected<void, Failure>
{
    auto execution = executeSafely();
    teardown();
    return execution;
}

void SmokeRun::teardown()
{
    const auto teardownStart = Clock::now();
    _context.reset();
    _window.reset();
    _run.teardownDuration = elapsed(teardownStart);
}

auto SmokeRun::checkedWindow() -> panda::tools::Window&
{
    if (!_window.has_value())
    {
        throw std::logic_error {"The benchmark window is not initialized"};
    }
    return *_window;
}

auto SmokeRun::checkedContext() -> panda::Context&
{
    if (!_context.has_value())
    {
        throw std::logic_error {"The benchmark context is not initialized"};
    }
    return *_context;
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
    if (const auto window = createWindow(); !window)
    {
        return window;
    }
    if (const auto context = createContext(); !context)
    {
        return context;
    }
    return captureInitialExtent();
}

auto SmokeRun::createWindow() -> std::expected<void, Failure>
{
    auto createdWindow =
        panda::tools::Window::create(glm::uvec2 {_options.windowWidth, _options.windowHeight}, "Panda benchmark smoke");
    if (!createdWindow)
    {
        return std::unexpected {failFromPanda(createdWindow.error())};
    }
    _window.emplace(std::move(*createdWindow));
    return {};
}

auto SmokeRun::createContext() -> std::expected<void, Failure>
{
    auto createdContext = panda::Context::createWithSurface(
        checkedWindow(),
        {.applicationName = "Panda benchmark", .enableValidation = false, .enableFramePerformanceSampling = true});
    if (!createdContext)
    {
        return std::unexpected {failFromPanda(createdContext.error())};
    }
    _context.emplace(std::move(*createdContext));
    recordDeviceInfo();
    return checkPerformanceCapabilities();
}

void SmokeRun::recordDeviceInfo()
{
    const auto deviceInfo = checkedContext().getDeviceInfo();
    _report.gpuModel = deviceInfo.name;
    if (!deviceInfo.driverName.empty())
    {
        _report.driver = DriverReport {.name = deviceInfo.driverName,
                                       .info = deviceInfo.driverInfo,
                                       .version = deviceInfo.driverVersion};
    }
    _report.vulkanApiVersion = deviceInfo.apiVersion;
}

auto SmokeRun::checkPerformanceCapabilities() -> std::expected<void, Failure>
{
    const auto capabilities = checkedContext().getPerformanceManager().getCapabilities();
    if (!capabilities.hasGpuTimestampQueries || !capabilities.isGpuTimestampSamplingEnabled)
    {
        return std::unexpected {unsupported("The selected graphics queue does not support GPU timestamp sampling")};
    }
    return {};
}

auto SmokeRun::captureInitialExtent() -> std::expected<void, Failure>
{
    const auto extent = checkedWindow().getFramebufferExtent();
    if (!extent)
    {
        return std::unexpected {failFromPanda(extent.error())};
    }
    if (extent->width == 0 || extent->height == 0)
    {
        return std::unexpected {invalidRun("The benchmark window is minimized")};
    }
    _initialExtent = *extent;
    _run.framebufferWidth = extent->width;
    _run.framebufferHeight = extent->height;
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
    if (checkedWindow().shouldClose())
    {
        return std::unexpected {invalidRun("The benchmark window was closed before the run completed")};
    }
    return checkExtent();
}

auto SmokeRun::checkExtent() -> std::expected<void, Failure>
{
    const auto extent = checkedWindow().getFramebufferExtent();
    if (!extent)
    {
        return std::unexpected {failFromPanda(extent.error())};
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
    const auto frame = checkedContext().presentClearFrame();
    if (!frame)
    {
        return std::unexpected {failFromPanda(frame.error())};
    }
    if (frame->status != panda::FrameResult::Status::Presented)
    {
        return std::unexpected {
            invalidRun(std::format("The frame was {} during the controlled run", statusName(frame->status)))};
    }
    if (!frame->completion.isValid())
    {
        return std::unexpected {failure("A presented frame had no completion token")};
    }
    recordSubmission(*frame, measuring);
    return {};
}

void SmokeRun::recordSubmission(const panda::FrameResult& frame, bool measuring)
{
    _lastCompletion = frame.completion;
    ++_submittedFrameCount;
    if (measuring)
    {
        ++_run.measurementSubmittedFrames;
    }
    else
    {
        ++_run.warmupSubmittedFrames;
    }
}

auto SmokeRun::collectAvailableSamples(bool measuring, std::uint64_t endFrame) -> std::expected<void, Failure>
{
    auto& performance = checkedContext().getPerformanceManager();
    const auto samples = performance.takeAvailableSamples();
    if (!samples)
    {
        return std::unexpected {failFromPanda(samples.error())};
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
    const auto input = checkedWindow().processInput();
    if (!input)
    {
        return std::unexpected {failFromPanda(input.error())};
    }
    return checkExtent();
}

auto SmokeRun::finishMeasurement() -> std::expected<void, Failure>
{
    if (!_lastCompletion.isValid())
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
    const auto waited = checkedContext().wait(_lastCompletion, std::chrono::seconds {30});
    _run.completionWaitDuration = elapsed(start);
    if (!waited)
    {
        return std::unexpected {failFromPanda(waited.error())};
    }
    if (*waited == panda::WaitStatus::Timeout)
    {
        return std::unexpected {failure("The last submitted frame did not finish within 30 seconds")};
    }
    return {};
}

auto SmokeRun::drainSamples() -> std::expected<void, Failure>
{
    const auto start = Clock::now();
    const auto remaining = checkedContext().getPerformanceManager().takeAvailableSamples();
    _run.sampleDrainDuration = elapsed(start);
    if (!remaining)
    {
        return std::unexpected {failFromPanda(remaining.error())};
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
