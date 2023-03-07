#pragma once

#include <optional>
#include <span>
#include <unordered_set>

#include "app/Window.h"
#include "gfx/api/RenderingApi.h"

namespace panda::gfx::vulkan
{

class Vulkan : public RenderingApi
{
public:
    explicit Vulkan(const Window& mainWindow);
    Vulkan(const Vulkan&) = delete;
    Vulkan(Vulkan&&) = default;
    auto operator=(const Vulkan&) -> Vulkan& = delete;
    auto operator=(Vulkan&&) -> Vulkan& = delete;
    ~Vulkan() noexcept override;

    auto render() -> void override;

private:
    struct QueueFamilies
    {
        std::optional<uint32_t> graphicsFamily;
        std::optional<uint32_t> presentationFamily;

        [[nodiscard]] auto hasValues() const noexcept -> bool;
        [[nodiscard]] auto getUniqueQueueFamilies() const -> std::unordered_set<uint32_t>;
    };

    struct SwapChainSupportDetails
    {
        vk::SurfaceCapabilitiesKHR capabilities;
        std::vector<vk::SurfaceFormatKHR> formats;
        std::vector<vk::PresentModeKHR> presentationModes;
    };

    [[nodiscard]] static constexpr auto shouldEnableValidationLayers() noexcept -> bool
    {
        return PD_DEBUG;
    }

    [[nodiscard]] static auto getRequiredExtensions() -> std::vector<const char*>;
    [[nodiscard]] static auto createDebugMessengerCreateInfo() noexcept -> vk::DebugUtilsMessengerCreateInfoEXT;
    [[nodiscard]] static auto isDeviceSuitable(vk::PhysicalDevice device, vk::SurfaceKHR surface) -> bool;
    [[nodiscard]] static auto findQueueFamilies(vk::PhysicalDevice device, vk::SurfaceKHR surface) -> QueueFamilies;
    [[nodiscard]] static auto checkDeviceExtensionSupport(vk::PhysicalDevice device) -> bool;
    [[nodiscard]] static auto querySwapChainSupport(vk::PhysicalDevice device, vk::SurfaceKHR surface)
        -> SwapChainSupportDetails;
    [[nodiscard]] static auto areRequiredExtensionsAvailable(std::span<const char* const> requiredExtensions) -> bool;
    [[nodiscard]] static auto chooseSwapSurfaceFormat(std::span<const vk::SurfaceFormatKHR> availableFormats) noexcept
        -> vk::SurfaceFormatKHR;
    [[nodiscard]] static auto choosePresentationMode(
        std::span<const vk::PresentModeKHR> availablePresentationModes) noexcept -> vk::PresentModeKHR;

    [[nodiscard]] auto pickPhysicalDevice() const -> vk::PhysicalDevice;
    [[nodiscard]] auto areValidationLayersSupported() const -> bool;
    [[nodiscard]] auto createLogicalDevice() const -> vk::Device;
    [[nodiscard]] auto createInstance() -> vk::Instance;
    [[nodiscard]] auto createSurface() -> vk::SurfaceKHR;
    [[nodiscard]] auto createSwapChain() -> vk::SwapchainKHR;
    [[nodiscard]] auto createImageViews() -> std::vector<vk::ImageView>;
    [[nodiscard]] auto chooseSwapExtent(const vk::SurfaceCapabilitiesKHR& capabilities) const -> vk::Extent2D;
    [[nodiscard]] auto createPipeline() -> vk::Pipeline;
    [[nodiscard]] auto createRenderPass() -> vk::RenderPass;

    auto enableValidationLayers(vk::InstanceCreateInfo& createInfo) -> bool;

    static constexpr auto requiredDeviceExtensions = std::array {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    inline static const vk::DebugUtilsMessengerCreateInfoEXT debugMessengerCreateInfo =
        createDebugMessengerCreateInfo();

    vk::Instance instance {};
    vk::Device device {};
    vk::PhysicalDevice physicalDevice {};
    QueueFamilies queueFamiliesIndices;
    vk::Queue graphicsQueue {};
    vk::Queue presentationQueue {};
    vk::SwapchainKHR swapChain {};
    vk::DebugUtilsMessengerEXT debugMessenger {};
    vk::SurfaceKHR surface {};
    std::vector<vk::Image> swapChainImages {};
    std::vector<vk::ImageView> swapChainImageViews {};
    vk::Format swapChainImageFormat {};
    vk::RenderPass renderPass {};
    vk::PipelineLayout pipelineLayout {};
    vk::Pipeline pipeline {};
    vk::Extent2D swapChainExtent {};
    std::vector<const char*> requiredValidationLayers;
    const Window& window;
};

}
