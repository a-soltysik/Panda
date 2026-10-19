#pragma once

#include <chrono>
#include <cstdint>
#include <expected>
#include <optional>
#include <panda/PerformanceManager.hpp>
#include <panda/WindowSurface.hpp>
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
    panda::FramebufferExtent framebufferExtent {};
    std::string gpuModel;
    std::optional<DriverReport> driver;
    std::optional<std::uint32_t> vulkanApiVersion;
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
    [[nodiscard]] virtual auto getFramebufferExtent() const -> std::expected<panda::FramebufferExtent, Failure> = 0;
    [[nodiscard]] virtual auto submitFrame() -> std::expected<FrameStatus, Failure> = 0;
    [[nodiscard]] virtual auto takeAvailableSamples()
        -> std::expected<std::vector<panda::FramePerformanceSample>, Failure> = 0;
    [[nodiscard]] virtual auto processInput() -> std::expected<void, Failure> = 0;
    [[nodiscard]] virtual auto waitForLastFrame(std::chrono::nanoseconds timeout) -> std::expected<void, Failure> = 0;
    virtual void teardown() = 0;
};
}
