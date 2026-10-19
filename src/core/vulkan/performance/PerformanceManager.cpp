// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
// clang-format on

#include <optional>
#include <panda/Error.hpp>
#include <panda/PerformanceManager.hpp>
#include <vector>

#include "PerformanceManagerImpl.hpp"

namespace panda
{
PerformanceManager::PerformanceManager(detail::PerformanceManagerImpl& implementation) noexcept
    : _implementation {&implementation}
{
}

auto PerformanceManager::getCapabilities() const noexcept -> PerformanceCapabilities
{
    return _implementation->getCapabilities();
}

auto PerformanceManager::getLatestSample() -> Result<std::optional<FramePerformanceSample>>
{
    return _implementation->getLatestSample();
}

auto PerformanceManager::takeNextSample() -> Result<std::optional<FramePerformanceSample>>
{
    return _implementation->takeNextSample();
}

auto PerformanceManager::takeAvailableSamples() -> Result<std::vector<FramePerformanceSample>>
{
    return _implementation->takeAvailableSamples();
}
}
