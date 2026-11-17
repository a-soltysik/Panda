#pragma once
#include <panda/base/Error.hpp>

// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include <cstdint>
#include <expected>
#include <panda/vulkan/WindowSurface.hpp>
#include <span>
#include <vector>

namespace panda::core::vulkan
{

struct SwapchainImage
{
    vk::Image image;
    vk::raii::ImageView view {nullptr};
    vk::raii::Semaphore renderFinished {nullptr};
    vk::raii::Fence presentFence {nullptr};
    bool presentPending {false};
    bool initialized {false};
};

struct SwapchainGeneration
{
    vk::raii::SwapchainKHR swapchain {nullptr};
    std::vector<SwapchainImage> images;
    vk::Extent2D extent;
    vk::Format format {vk::Format::eUndefined};
    bool supportsTransferDestination {false};
};

struct SurfaceConfig
{
    vk::SurfaceCapabilitiesKHR capabilities;
    vk::SurfaceFormatKHR format;
    vk::Extent2D extent;
    std::uint32_t imageCount {0};
};

struct SwapchainGenerationRequest
{
    vk::PhysicalDevice physicalDevice;
    const vk::raii::Device& device;
    vk::SurfaceKHR surface;
    glm::uvec2 requested;
    vk::SwapchainKHR oldSwapchain;
    std::uint32_t queueFamily;
    std::uint32_t presentQueueFamily;
};

[[nodiscard]] auto chooseSurfaceConfig(vk::SurfaceCapabilitiesKHR capabilities,
                                       std::span<const vk::SurfaceFormatKHR> formats,
                                       glm::uvec2 requested) -> std::expected<SurfaceConfig, Error>;

[[nodiscard]] auto createSwapchainGeneration(const SwapchainGenerationRequest& request)
    -> std::expected<SwapchainGeneration, Error>;

}
