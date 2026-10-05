// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include <vulkan/vulkan_core.h>

#include <expected>
#include <panda/Context.hpp>
#include <panda/WindowSurface.hpp>

#include "ScopedMock.hpp"
#include "SwapchainGeneration.hpp"
#include "SwapchainGenerationMock.hpp"

namespace panda::detail
{
auto createSwapchainGeneration(vk::PhysicalDevice physicalDevice,
                               vk::Device device,
                               VkSurfaceKHR surface,
                               FramebufferExtent requested,
                               vk::SwapchainKHR oldSwapchain,
                               const ContextDeviceInfo& deviceInfo) -> std::expected<SwapchainGeneration, Error>
{
    return test::ScopedMock<test::SwapchainGenerationMock>::getActiveMock()
        .createSwapchainGeneration(physicalDevice, device, surface, requested, oldSwapchain, deviceInfo);
}

auto encodedClear(vk::Format format) -> vk::ClearColorValue
{
    return test::ScopedMock<test::SwapchainGenerationMock>::getActiveMock().encodedClear(format);
}
}
