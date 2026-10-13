// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include <expected>
#include <panda/Error.hpp>

#include "ScopedMock.hpp"
#include "SwapchainGeneration.hpp"
#include "SwapchainGenerationMock.hpp"

namespace panda::detail
{
auto createSwapchainGeneration(const SwapchainGenerationRequest& request) -> std::expected<SwapchainGeneration, Error>
{
    return test::ScopedMock<test::SwapchainGenerationMock>::getActiveMock().createSwapchainGeneration(request);
}

auto encodedClear(vk::Format format) -> vk::ClearColorValue
{
    return test::ScopedMock<test::SwapchainGenerationMock>::getActiveMock().encodedClear(format);
}
}
