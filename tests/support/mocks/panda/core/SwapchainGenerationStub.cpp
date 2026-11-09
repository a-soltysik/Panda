// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
// clang-format on

#include <expected>
#include <panda/Error.hpp>

#include "ScopedMock.hpp"
#include "SwapchainGenerationMock.hpp"
#include "presentation/SwapchainGeneration.hpp"

namespace panda::core::vulkan
{
auto createSwapchainGeneration(const SwapchainGenerationRequest& request) -> std::expected<SwapchainGeneration, Error>
{
    return test::ScopedMock<test::SwapchainGenerationMock>::getActiveMock().createSwapchainGeneration(request);
}

}
