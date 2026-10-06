#pragma once

// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include <cstdint>
#include <expected>
#include <panda/Context.hpp>
#include <panda/WindowSurface.hpp>
#include <span>
#include <vector>

namespace panda::detail
{

struct SwapchainImage
{
    vk::Image image;
    vk::UniqueImageView view;
    vk::UniqueSemaphore renderFinished;
    vk::UniqueFence presentFence;
    bool presentPending {false};
    bool initialized {false};
};

struct SwapchainGeneration
{
    vk::UniqueSwapchainKHR swapchain;
    std::vector<SwapchainImage> images;
    vk::Extent2D extent;
    vk::Format format {vk::Format::eUndefined};
};

struct SurfaceConfig
{
    vk::SurfaceCapabilitiesKHR capabilities;
    vk::SurfaceFormatKHR format;
    vk::Extent2D extent;
    std::uint32_t imageCount {0};
};

[[nodiscard]] auto chooseSurfaceConfig(vk::SurfaceCapabilitiesKHR capabilities,
                                       std::span<const vk::SurfaceFormatKHR> formats,
                                       FramebufferExtent requested) -> std::expected<SurfaceConfig, Error>;

[[nodiscard]] auto createSwapchainGeneration(vk::PhysicalDevice physicalDevice,
                                             vk::Device device,
                                             VkSurfaceKHR surface,
                                             FramebufferExtent requested,
                                             vk::SwapchainKHR oldSwapchain,
                                             const ContextDeviceInfo& deviceInfo)
    -> std::expected<SwapchainGeneration, Error>;

[[nodiscard]] auto encodedClear(vk::Format format) -> vk::ClearColorValue;

}
