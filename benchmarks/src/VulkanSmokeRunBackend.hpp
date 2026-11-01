#pragma once

#include <chrono>
#include <expected>
#include <optional>
#include <panda/CompletionPoint.hpp>
#include <panda/Context.hpp>
#include <panda/RenderView.hpp>
#include <panda/tools/gui/Window.hpp>

#include "SmokeRunBackend.hpp"

namespace panda_bench
{
class VulkanSmokeRunBackend final : public SmokeRunBackend
{
public:
    [[nodiscard]] auto initialize(const Options& options) -> std::expected<SmokeRunEnvironment, Failure> override;
    [[nodiscard]] auto shouldClose() const -> bool override;
    [[nodiscard]] auto getFramebufferExtent() const -> std::expected<panda::FramebufferExtent, Failure> override;
    [[nodiscard]] auto submitFrame() -> std::expected<FrameStatus, Failure> override;
    [[nodiscard]] auto takeAvailableSamples()
        -> std::expected<std::vector<panda::FramePerformanceSample>, Failure> override;
    [[nodiscard]] auto processInput() -> std::expected<void, Failure> override;
    [[nodiscard]] auto waitForLastFrame(std::chrono::nanoseconds timeout) -> std::expected<void, Failure> override;
    void teardown() override;

private:
    [[nodiscard]] auto checkedWindow() const -> const panda::tools::Window&;
    [[nodiscard]] auto checkedContext() -> panda::Context&;
    [[nodiscard]] auto createWindow(const Options& options) -> std::expected<void, Failure>;
    [[nodiscard]] auto createContext() -> std::expected<void, Failure>;
    [[nodiscard]] auto checkPerformanceCapabilities() -> std::expected<void, Failure>;
    [[nodiscard]] auto captureEnvironment() -> std::expected<SmokeRunEnvironment, Failure>;
    [[nodiscard]] auto createStaticCube(panda::FramebufferExtent extent) -> std::expected<void, Failure>;

    std::optional<panda::tools::Window> _window;
    std::optional<panda::Context> _context;
    panda::RenderView _view;
    panda::CompletionPoint _lastCompletion;
};
}
