#include "SwapchainGeneration.hpp"

#include <vulkan/vulkan_core.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <expected>
#include <limits>
#include <panda/Context.hpp>
#include <panda/WindowSurface.hpp>
#include <span>
#include <string>
#include <utility>
#include <vector>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_to_string.hpp>

#include "VulkanError.hpp"
#include "VulkanHpp.hpp"  // IWYU pragma: keep

namespace panda::detail
{
namespace
{
auto chooseFormat(std::span<const vk::SurfaceFormatKHR> formats) -> vk::SurfaceFormatKHR
{
    for (const auto candidate :
         {vk::Format::eB8G8R8A8Srgb, vk::Format::eR8G8B8A8Srgb, vk::Format::eB8G8R8A8Unorm, vk::Format::eR8G8B8A8Unorm})
    {
        const auto found = std::ranges::find_if(formats, [candidate](const auto& entry) {
            return entry.format == candidate && entry.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear;
        });
        if (found != formats.end())
        {
            return *found;
        }
    }
    return {};
}

auto chooseCompositeAlpha(vk::CompositeAlphaFlagsKHR supported) -> vk::CompositeAlphaFlagBitsKHR
{
    for (const auto candidate : {vk::CompositeAlphaFlagBitsKHR::eOpaque,
                                 vk::CompositeAlphaFlagBitsKHR::ePreMultiplied,
                                 vk::CompositeAlphaFlagBitsKHR::ePostMultiplied,
                                 vk::CompositeAlphaFlagBitsKHR::eInherit})
    {
        if (supported & candidate)
        {
            return candidate;
        }
    }
    return vk::CompositeAlphaFlagBitsKHR::eOpaque;
}

auto selectSurfaceConfig(vk::PhysicalDevice physicalDevice, VkSurfaceKHR surface, FramebufferExtent requested)
    -> std::expected<SurfaceConfig, Error>
{
    const auto capabilities = physicalDevice.getSurfaceCapabilitiesKHR(vk::SurfaceKHR {surface});
    if (capabilities.result != vk::Result::eSuccess)
    {
        return std::unexpected {makeVulkanError(capabilities.result)};
    }
    const auto listedFormats = physicalDevice.getSurfaceFormatsKHR(vk::SurfaceKHR {surface});
    if (listedFormats.result != vk::Result::eSuccess)
    {
        return std::unexpected {makeVulkanError(listedFormats.result)};
    }
    return chooseSurfaceConfig(capabilities.value, listedFormats.value, requested);
}

}

auto chooseSurfaceConfig(vk::SurfaceCapabilitiesKHR capabilities,
                         std::span<const vk::SurfaceFormatKHR> formats,
                         FramebufferExtent requested) -> std::expected<SurfaceConfig, Error>
{
    if (!(capabilities.supportedUsageFlags & vk::ImageUsageFlagBits::eColorAttachment))
    {
        return std::unexpected {makeError(ErrorCode::Unsupported, "Surface does not support color attachments")};
    }
    const auto chosenFormat = chooseFormat(formats);
    if (chosenFormat.format == vk::Format::eUndefined)
    {
        return std::unexpected {makeError(ErrorCode::Unsupported, "Surface has no supported SRGB or UNORM SDR format")};
    }
    const auto extent = capabilities.currentExtent.width != std::numeric_limits<std::uint32_t>::max()
                            ? capabilities.currentExtent
                            : vk::Extent2D {.width = std::clamp(requested.width,
                                                                capabilities.minImageExtent.width,
                                                                capabilities.maxImageExtent.width),
                                            .height = std::clamp(requested.height,
                                                                 capabilities.minImageExtent.height,
                                                                 capabilities.maxImageExtent.height)};
    if (extent.width == 0 || extent.height == 0)
    {
        return std::unexpected {makeError(ErrorCode::Unsupported, "Surface has zero framebuffer extent")};
    }
    auto imageCount = std::max(capabilities.minImageCount, 2U);
    if (capabilities.maxImageCount != 0)
    {
        imageCount = std::min(imageCount, capabilities.maxImageCount);
    }
    return SurfaceConfig {.capabilities = capabilities,
                          .format = chosenFormat,
                          .extent = extent,
                          .imageCount = imageCount};
}

namespace
{
auto createSwapchain(vk::Device device,
                     VkSurfaceKHR surface,
                     vk::SwapchainKHR oldSwapchain,
                     const SurfaceConfig& config,
                     const ContextDeviceInfo& deviceInfo) -> std::expected<vk::UniqueSwapchainKHR, Error>
{
    const auto familyIndices = std::array {deviceInfo.queueFamily, deviceInfo.presentQueueFamily};
    const auto separateFamilies = familyIndices.front() != familyIndices.back();
    const auto info = vk::SwapchainCreateInfoKHR {
        .surface = vk::SurfaceKHR {surface},
        .minImageCount = config.imageCount,
        .imageFormat = config.format.format,
        .imageColorSpace = config.format.colorSpace,
        .imageExtent = config.extent,
        .imageArrayLayers = 1,
        .imageUsage = vk::ImageUsageFlagBits::eColorAttachment,
        .imageSharingMode = separateFamilies ? vk::SharingMode::eConcurrent : vk::SharingMode::eExclusive,
        .queueFamilyIndexCount = separateFamilies ? 2U : 0U,
        .pQueueFamilyIndices = separateFamilies ? familyIndices.data() : nullptr,
        .preTransform = config.capabilities.currentTransform,
        .compositeAlpha = chooseCompositeAlpha(config.capabilities.supportedCompositeAlpha),
        .presentMode = vk::PresentModeKHR::eFifo,
        .clipped = vk::True,
        .oldSwapchain = oldSwapchain};
    auto created = createOwned<vk::SwapchainKHR>(
        [&](vk::SwapchainKHR* output) {
            return device.createSwapchainKHR(&info, nullptr, output);
        },
        deviceDeleter(device));
    if (created.result != vk::Result::eSuccess)
    {
        return std::unexpected {makeVulkanError(created.result)};
    }
    return std::move(created.value);
}

auto createSwapchainImage(vk::Device device, vk::Image image, vk::Format format) -> std::expected<SwapchainImage, Error>
{
    const auto viewInfo = vk::ImageViewCreateInfo {
        .image = image,
        .viewType = vk::ImageViewType::e2D,
        .format = format,
        .subresourceRange = vk::ImageSubresourceRange {.aspectMask = vk::ImageAspectFlagBits::eColor,
                                                       .levelCount = 1,
                                                       .layerCount = 1}
    };
    auto view = createOwned<vk::ImageView>(
        [&](vk::ImageView* output) {
            return device.createImageView(&viewInfo, nullptr, output);
        },
        deviceDeleter(device));
    if (view.result != vk::Result::eSuccess)
    {
        return std::unexpected {makeVulkanError(view.result)};
    }
    static constexpr auto semaphoreInfo = vk::SemaphoreCreateInfo {};
    auto finished = createOwned<vk::Semaphore>(
        [&](vk::Semaphore* output) {
            return device.createSemaphore(&semaphoreInfo, nullptr, output);
        },
        deviceDeleter(device));
    if (finished.result != vk::Result::eSuccess)
    {
        return std::unexpected {makeVulkanError(finished.result)};
    }
    static constexpr auto fenceInfo = vk::FenceCreateInfo {};
    auto fence = createOwned<vk::Fence>(
        [&](vk::Fence* output) {
            return device.createFence(&fenceInfo, nullptr, output);
        },
        deviceDeleter(device));
    if (fence.result != vk::Result::eSuccess)
    {
        return std::unexpected {makeVulkanError(fence.result)};
    }
    return SwapchainImage {.image = image,
                           .view = std::move(view.value),
                           .renderFinished = std::move(finished.value),
                           .presentFence = std::move(fence.value)};
}

auto populateImages(vk::Device device, SwapchainGeneration& generation) -> std::expected<void, Error>
{
    const auto images = device.getSwapchainImagesKHR(*generation.swapchain);
    if (images.result != vk::Result::eSuccess)
    {
        return std::unexpected {makeVulkanError(images.result)};
    }
    generation.images.reserve(images.value.size());
    for (const auto image : images.value)
    {
        auto created = createSwapchainImage(device, image, generation.format);
        if (!created)
        {
            return std::unexpected {std::move(created.error())};
        }
        generation.images.push_back(std::move(*created));
    }
    return {};
}
}

auto encodedClear(vk::Format format) -> vk::ClearColorValue
{
    static constexpr auto transferThreshold = 0.0031308F;
    static constexpr auto linearScale = 12.92F;
    static constexpr auto gammaScale = 1.055F;
    static constexpr auto inverseGamma = 1.0F / 2.4F;
    static constexpr auto gammaOffset = 0.055F;
    static constexpr auto clearRed = 0.025F;
    static constexpr auto clearGreen = 0.035F;
    static constexpr auto clearBlue = 0.05F;
    const auto encode = [](float linear) {
        return linear <= transferThreshold ? linear * linearScale
                                           : (gammaScale * std::pow(linear, inverseGamma)) - gammaOffset;
    };
    const auto srgb = format == vk::Format::eB8G8R8A8Srgb || format == vk::Format::eR8G8B8A8Srgb;
    return vk::ClearColorValue {
        std::array {srgb ? clearRed : encode(clearRed),
                    srgb ? clearGreen : encode(clearGreen),
                    srgb ? clearBlue : encode(clearBlue),
                    1.0F}
    };
}

auto createSwapchainGeneration(vk::PhysicalDevice physicalDevice,
                               vk::Device device,
                               VkSurfaceKHR surface,
                               FramebufferExtent requested,
                               vk::SwapchainKHR oldSwapchain,
                               const ContextDeviceInfo& deviceInfo) -> std::expected<SwapchainGeneration, Error>
{
    const auto config = selectSurfaceConfig(physicalDevice, surface, requested);
    if (!config)
    {
        return std::unexpected {config.error()};
    }
    auto swapchain = createSwapchain(device, surface, oldSwapchain, *config, deviceInfo);
    if (!swapchain)
    {
        return std::unexpected {std::move(swapchain.error())};
    }
    auto generation = SwapchainGeneration {.swapchain = std::move(*swapchain),
                                           .images = {},
                                           .extent = config->extent,
                                           .format = config->format.format};
    if (const auto loaded = populateImages(device, generation); !loaded)
    {
        return std::unexpected {loaded.error()};
    }
    return generation;
}

}
