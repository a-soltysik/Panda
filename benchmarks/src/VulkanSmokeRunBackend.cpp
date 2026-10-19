#include "VulkanSmokeRunBackend.hpp"

#include <chrono>
#include <expected>
#include <glm/ext/vector_uint2.hpp>
#include <panda/CompletionPoint.hpp>
#include <panda/Context.hpp>
#include <panda/Error.hpp>
#include <panda/PerformanceManager.hpp>
#include <panda/WindowSurface.hpp>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "SmokeRunBackend.hpp"
#include "panda_bench/Arguments.hpp"
#include "panda_bench/Report.hpp"

namespace panda_bench
{
namespace
{
auto failure(std::string message) -> Failure
{
    return {.message = std::move(message)};
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

auto statusFromPanda(panda::FrameResult::Status status) -> FrameStatus
{
    switch (status)
    {
    case panda::FrameResult::Status::Presented:
        return FrameStatus::Presented;
    case panda::FrameResult::Status::Skipped:
        return FrameStatus::Skipped;
    case panda::FrameResult::Status::Suspended:
        return FrameStatus::Suspended;
    case panda::FrameResult::Status::RecreatePending:
        return FrameStatus::RecreatePending;
    }
    return FrameStatus::Skipped;
}
}

auto VulkanSmokeRunBackend::initialize(const Options& options) -> std::expected<SmokeRunEnvironment, Failure>
{
    if (const auto window = createWindow(options); !window)
    {
        return std::unexpected {window.error()};
    }
    if (const auto context = createContext(); !context)
    {
        return std::unexpected {context.error()};
    }
    return captureEnvironment();
}

auto VulkanSmokeRunBackend::shouldClose() const -> bool
{
    return checkedWindow().shouldClose();
}

auto VulkanSmokeRunBackend::getFramebufferExtent() const -> std::expected<panda::FramebufferExtent, Failure>
{
    const auto extent = checkedWindow().getFramebufferExtent();
    if (!extent)
    {
        return std::unexpected {failFromPanda(extent.error())};
    }
    return *extent;
}

auto VulkanSmokeRunBackend::submitFrame() -> std::expected<FrameStatus, Failure>
{
    const auto frame = checkedContext().presentClearFrame();
    if (!frame)
    {
        return std::unexpected {failFromPanda(frame.error())};
    }
    if (frame->status == panda::FrameResult::Status::Presented && !frame->completion.isValid())
    {
        return std::unexpected {failure("A presented frame had no completion token")};
    }
    if (frame->completion.isValid())
    {
        _lastCompletion = frame->completion;
    }
    return statusFromPanda(frame->status);
}

auto VulkanSmokeRunBackend::takeAvailableSamples() -> std::expected<std::vector<panda::FramePerformanceSample>, Failure>
{
    const auto samples = checkedContext().getPerformanceManager().takeAvailableSamples();
    if (!samples)
    {
        return std::unexpected {failFromPanda(samples.error())};
    }
    return *samples;
}

auto VulkanSmokeRunBackend::processInput() -> std::expected<void, Failure>
{
    const auto input = checkedWindow().processInput();
    if (!input)
    {
        return std::unexpected {failFromPanda(input.error())};
    }
    return {};
}

auto VulkanSmokeRunBackend::waitForLastFrame(std::chrono::nanoseconds timeout) -> std::expected<void, Failure>
{
    const auto waited = checkedContext().wait(_lastCompletion, timeout);
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

void VulkanSmokeRunBackend::teardown()
{
    _context.reset();
    _window.reset();
    _lastCompletion = {};
}

auto VulkanSmokeRunBackend::checkedWindow() const -> const panda::tools::Window&
{
    if (!_window.has_value())
    {
        throw std::logic_error {"The benchmark window is not initialized"};
    }
    return *_window;
}

auto VulkanSmokeRunBackend::checkedContext() -> panda::Context&
{
    if (!_context.has_value())
    {
        throw std::logic_error {"The benchmark context is not initialized"};
    }
    return *_context;
}

auto VulkanSmokeRunBackend::createWindow(const Options& options) -> std::expected<void, Failure>
{
    auto createdWindow =
        panda::tools::Window::create(glm::uvec2 {options.windowWidth, options.windowHeight}, "Panda benchmark smoke");
    if (!createdWindow)
    {
        return std::unexpected {failFromPanda(createdWindow.error())};
    }
    _window.emplace(std::move(*createdWindow));
    return {};
}

auto VulkanSmokeRunBackend::createContext() -> std::expected<void, Failure>
{
    auto createdContext = panda::Context::createWithSurface(
        checkedWindow(),
        {.applicationName = "Panda benchmark", .enableValidation = false, .enableFramePerformanceSampling = true});
    if (!createdContext)
    {
        return std::unexpected {failFromPanda(createdContext.error())};
    }
    _context.emplace(std::move(*createdContext));
    return checkPerformanceCapabilities();
}

auto VulkanSmokeRunBackend::checkPerformanceCapabilities() -> std::expected<void, Failure>
{
    const auto capabilities = checkedContext().getPerformanceManager().getCapabilities();
    if (!capabilities.hasGpuTimestampQueries || !capabilities.isGpuTimestampSamplingEnabled)
    {
        return std::unexpected {unsupported("The selected graphics queue does not support GPU timestamp sampling")};
    }
    return {};
}

auto VulkanSmokeRunBackend::captureEnvironment() -> std::expected<SmokeRunEnvironment, Failure>
{
    const auto extent = getFramebufferExtent();
    if (!extent)
    {
        return std::unexpected {extent.error()};
    }
    const auto deviceInfo = checkedContext().getDeviceInfo();
    auto environment = SmokeRunEnvironment {.framebufferExtent = *extent,
                                            .gpuModel = deviceInfo.name,
                                            .driver = {},
                                            .vulkanApiVersion = deviceInfo.apiVersion};
    if (!deviceInfo.driverName.empty())
    {
        environment.driver = DriverReport {.name = deviceInfo.driverName,
                                           .info = deviceInfo.driverInfo,
                                           .version = deviceInfo.driverVersion};
    }
    return environment;
}
}
