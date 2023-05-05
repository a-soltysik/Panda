#pragma once

#include <optional>
#include <unordered_set>

namespace panda::gfx::vulkan
{

class Device
{
public:
    struct QueueFamilies
    {
        uint32_t graphicsFamily;
        uint32_t presentationFamily;

        [[nodiscard]] auto getUniqueQueueFamilies() const -> std::unordered_set<uint32_t>;
    };

    struct SwapChainSupportDetails
    {
        vk::SurfaceCapabilitiesKHR capabilities;
        std::vector<vk::SurfaceFormatKHR> formats;
        std::vector<vk::PresentModeKHR> presentationModes;
    };

    Device(const vk::Instance& instance,
           const vk::SurfaceKHR& currentSurface,
           std::span<const char* const> requiredExtensions);
    Device(const vk::Instance& instance,
           const vk::SurfaceKHR& currentSurface,
           std::span<const char* const> requiredExtensions,
           std::span<const char* const> requiredValidationLayers);

    auto querySwapChainSupport() -> SwapChainSupportDetails;

    const vk::PhysicalDevice physicalDevice;
    const QueueFamilies queueFamilies;

    const vk::Device logicalDevice;

private:
    static auto pickPhysicalDevice(const vk::Instance& instance,
                                   const vk::SurfaceKHR& surface,
                                   std::span<const char* const> requiredExtensions) -> vk::PhysicalDevice;
    static auto isDeviceSuitable(vk::PhysicalDevice device,
                                 vk::SurfaceKHR surface,
                                 std::span<const char* const> requiredExtensions) -> bool;
    static auto findQueueFamilies(vk::PhysicalDevice device, vk::SurfaceKHR surface) -> std::optional<QueueFamilies>;
    static auto querySwapChainSupport(vk::PhysicalDevice device, vk::SurfaceKHR surface) -> SwapChainSupportDetails;
    static auto checkDeviceExtensionSupport(vk::PhysicalDevice device, std::span<const char* const> requiredExtensions)
        -> bool;
    static auto createLogicalDevice(vk::PhysicalDevice device,
                                    const QueueFamilies& queueFamilies,
                                    std::span<const char* const> requiredExtensions) -> vk::Device;
    static auto createLogicalDevice(vk::PhysicalDevice device,
                                    const QueueFamilies& queueFamilies,
                                    std::span<const char* const> requiredExtensions,
                                    std::span<const char* const> requiredValidationLayers) -> vk::Device;

    const vk::SurfaceKHR& surface;
};

}
