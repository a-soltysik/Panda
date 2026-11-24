#pragma once

#include <chrono>
#include <expected>
#include <optional>
#include <panda/Engine.hpp>
#include <panda/rendering/RenderView.hpp>
#include <panda/submission/CompletionPoint.hpp>
#include <panda/tools/gui/Window.hpp>

#include "SmokeRunBackend.hpp"

namespace panda_bench
{
class VulkanSmokeRunBackend final : public SmokeRunBackend
{
public:
    [[nodiscard]] auto initialize(const Options& options) -> std::expected<SmokeRunEnvironment, Failure> override;
    [[nodiscard]] auto shouldClose() const -> bool override;
    [[nodiscard]] auto getFramebufferExtent() const -> std::expected<glm::uvec2, Failure> override;
    [[nodiscard]] auto effectiveSampleCount() const noexcept -> std::uint32_t override;
    [[nodiscard]] auto effectiveAnisotropy() const noexcept -> float override;
    [[nodiscard]] auto submitFrame() -> std::expected<FrameStatus, Failure> override;
    [[nodiscard]] auto takeAvailableSamples()
        -> std::expected<std::vector<panda::FramePerformanceSample>, Failure> override;
    [[nodiscard]] auto processInput() -> std::expected<void, Failure> override;
    [[nodiscard]] auto waitForLastFrame(std::chrono::nanoseconds timeout) -> std::expected<void, Failure> override;
    void teardown() override;

private:
    [[nodiscard]] auto checkedWindow() const -> const panda::tools::Window&;
    [[nodiscard]] auto checkedEngine() -> panda::Engine&;
    [[nodiscard]] auto createWindow(const Options& options) -> std::expected<void, Failure>;
    [[nodiscard]] auto createEngine(const Options& options) -> std::expected<void, Failure>;
    [[nodiscard]] auto checkPerformanceCapabilities() -> std::expected<void, Failure>;
    [[nodiscard]] auto captureEnvironment() -> std::expected<SmokeRunEnvironment, Failure>;
    [[nodiscard]] auto createStaticScene(glm::uvec2 extent, const Options& options) -> std::expected<void, Failure>;
    [[nodiscard]] auto createParticleView(glm::uvec2 extent, const Options& options) -> std::expected<void, Failure>;
    [[nodiscard]] auto createSmokeScene(glm::uvec2 extent, const Options& options) -> std::expected<void, Failure>;

    std::optional<panda::tools::Window> _window;
    std::optional<panda::Engine> _engine;
    panda::RenderView _view;
    panda::CompletionPoint _lastCompletion;
    std::uint32_t _effectiveSampleCount {1};
    float _effectiveAnisotropy {1.0F};
};
}
