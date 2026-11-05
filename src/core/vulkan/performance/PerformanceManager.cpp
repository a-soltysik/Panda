// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
// clang-format on

#include <functional>
#include <optional>
#include <panda/Assert.hpp>
#include <panda/Error.hpp>
#include <panda/PerformanceManager.hpp>
#include <vector>

#include "PerformanceManagerImpl.hpp"

namespace panda
{
PerformanceManager::PerformanceManager(
    std::optional<std::reference_wrapper<detail::PerformanceManagerImpl>> implementation) noexcept
    : _implementation {implementation}
{
}

auto PerformanceManager::getCapabilities() const noexcept -> PerformanceCapabilities
{
    if (!_implementation)
    {
        panda::panic("Cannot use a moved-from PerformanceManager");
    }
    return (*_implementation).get().getCapabilities();
}

auto PerformanceManager::getLatestSample() -> Result<std::optional<FramePerformanceSample>>
{
    if (!_implementation)
    {
        panda::panic("Cannot use a moved-from PerformanceManager");
    }
    return (*_implementation).get().getLatestSample();
}

auto PerformanceManager::takeNextSample() -> Result<std::optional<FramePerformanceSample>>
{
    if (!_implementation)
    {
        panda::panic("Cannot use a moved-from PerformanceManager");
    }
    return (*_implementation).get().takeNextSample();
}

auto PerformanceManager::takeAvailableSamples() -> Result<std::vector<FramePerformanceSample>>
{
    if (!_implementation)
    {
        panda::panic("Cannot use a moved-from PerformanceManager");
    }
    return (*_implementation).get().takeAvailableSamples();
}
}
