#pragma once

#include <chrono>
#include <cstdint>
#include <expected>
#include <glm/vec2.hpp>
#include <optional>
#include <panda/EngineOptions.hpp>
#include <panda/rendering/PerformanceManager.hpp>
#include <panda/vulkan/WindowSurface.hpp>
#include <string>
#include <vector>

#include "panda_bench/Arguments.hpp"
#include "panda_bench/Report.hpp"

namespace panda_bench
{
struct Failure
{
    std::string outcome {"failed"};
    std::string message;
};

struct SmokeRunEnvironment
{
    glm::uvec2 framebufferExtent {};
    std::string gpuModel;
    std::optional<DriverReport> driver;
    std::optional<std::uint32_t> vulkanApiVersion;
    panda::EngineOptions::PresentationMode presentationMode {panda::EngineOptions::PresentationMode::Fifo};
};

enum class FrameStatus : std::uint8_t
{
    Presented,
    Skipped,
    Suspended,
    RecreatePending
};

class SmokeRunBackend
{
public:
    SmokeRunBackend() = default;
    SmokeRunBackend(const SmokeRunBackend&) = delete;
    auto operator=(const SmokeRunBackend&) -> SmokeRunBackend& = delete;
    SmokeRunBackend(SmokeRunBackend&&) = delete;
    auto operator=(SmokeRunBackend&&) -> SmokeRunBackend& = delete;
    virtual ~SmokeRunBackend() = default;

    [[nodiscard]] virtual auto initialize(const Options& options) -> std::expected<SmokeRunEnvironment, Failure> = 0;
    [[nodiscard]] virtual auto shouldClose() const -> bool = 0;
    [[nodiscard]] virtual auto getFramebufferExtent() const -> std::expected<glm::uvec2, Failure> = 0;

    [[nodiscard]] virtual auto effectiveSampleCount() const noexcept -> std::uint32_t
    {
        return 1;
    }

    [[nodiscard]] virtual auto effectiveAnisotropy() const noexcept -> float
    {
        return 1.0F;
    }

    [[nodiscard]] virtual auto submitFrame() -> std::expected<FrameStatus, Failure> = 0;
    [[nodiscard]] virtual auto takeAvailableSamples()
        -> std::expected<std::vector<panda::FramePerformanceSample>, Failure> = 0;
    [[nodiscard]] virtual auto processInput() -> std::expected<void, Failure> = 0;
    [[nodiscard]] virtual auto waitForLastFrame(std::chrono::nanoseconds timeout) -> std::expected<void, Failure> = 0;
    virtual void teardown() = 0;
};
}
