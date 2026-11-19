// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
// clang-format on

#include "ScopedMock.hpp"
#include "SwapchainGenerationMock.hpp"
#include "presentation/SwapchainGeneration.hpp"

namespace panda::core::vulkan
{
auto createSwapchainGeneration(const SwapchainGenerationRequest& request) -> SwapchainGenerationResult
{
    return test::ScopedMock<test::SwapchainGenerationMock>::getActiveMock().createSwapchainGeneration(request);
}

}
