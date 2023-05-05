#pragma once

#include <optional>
#include <span>
#include <unordered_set>

#include "Device.h"
#include "Vertex.h"
#include "app/Window.h"
#include "gfx/api/RenderingApi.h"

namespace panda::gfx::vulkan
{

class Vulkan : public RenderingApi
{
public:
    explicit Vulkan(Window& mainWindow);
    Vulkan(const Vulkan&) = delete;
    Vulkan(Vulkan&&) = default;
    auto operator=(const Vulkan&) -> Vulkan& = delete;
    auto operator=(Vulkan&&) -> Vulkan& = delete;
    ~Vulkan() noexcept override;

    auto render() -> void override;

private:

    [[nodiscard]] static constexpr auto shouldEnableValidationLayers() noexcept -> bool
    {
        return PD_DEBUG;
    }

    [[nodiscard]] static auto getRequiredExtensions() -> std::vector<const char*>;
    [[nodiscard]] static auto createDebugMessengerCreateInfo() noexcept -> vk::DebugUtilsMessengerCreateInfoEXT;
    [[nodiscard]] static auto areRequiredExtensionsAvailable(std::span<const char* const> requiredExtensions) -> bool;
    [[nodiscard]] static auto chooseSwapSurfaceFormat(std::span<const vk::SurfaceFormatKHR> availableFormats) noexcept
        -> vk::SurfaceFormatKHR;
    [[nodiscard]] static auto choosePresentationMode(
        std::span<const vk::PresentModeKHR> availablePresentationModes) noexcept -> vk::PresentModeKHR;

    [[nodiscard]] auto areValidationLayersSupported() const -> bool;
    [[nodiscard]] auto createInstance() -> vk::Instance;
    [[nodiscard]] auto createSurface() -> vk::SurfaceKHR;
    [[nodiscard]] auto createSwapchain() -> vk::SwapchainKHR;
    [[nodiscard]] auto createImageViews() -> std::vector<vk::ImageView>;
    [[nodiscard]] auto chooseSwapExtent(const vk::SurfaceCapabilitiesKHR& capabilities) const -> vk::Extent2D;
    [[nodiscard]] auto createPipeline() -> vk::Pipeline;
    [[nodiscard]] auto createRenderPass() -> vk::RenderPass;
    [[nodiscard]] auto createFrameBuffers() -> std::vector<vk::Framebuffer>;
    [[nodiscard]] auto createCommandPool() -> vk::CommandPool;
    [[nodiscard]] auto createCommandBuffers() -> std::vector<vk::CommandBuffer>;
    auto recordCommandBuffer(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) -> void;
    auto createSyncObjects() -> void;
    [[nodiscard]] auto createVertexBuffer() -> vk::Buffer;
    auto recreateSwapchain() -> void;
    auto cleanupSwapchain() -> void;
    [[nodiscard]] auto findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties) -> uint32_t;

    auto enableValidationLayers(vk::InstanceCreateInfo& createInfo) -> bool;

    static constexpr auto maxFramesInFlight = size_t{2};
    static constexpr auto requiredDeviceExtensions = std::array {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    inline static const vk::DebugUtilsMessengerCreateInfoEXT debugMessengerCreateInfo =
        createDebugMessengerCreateInfo();

    vk::Instance instance {};
    std::unique_ptr<Device> device;
    vk::PhysicalDevice physicalDevice {};
    vk::Queue graphicsQueue {};
    vk::Queue presentationQueue {};
    vk::SwapchainKHR swapchain {};
    vk::DebugUtilsMessengerEXT debugMessenger {};
    vk::SurfaceKHR surface {};
    std::vector<vk::Image> swapChainImages;
    std::vector<vk::ImageView> swapchainImageViews;
    vk::Format swapChainImageFormat {};
    vk::RenderPass renderPass {};
    vk::PipelineLayout pipelineLayout {};
    vk::Pipeline pipeline {};
    std::vector<vk::Framebuffer> swapchainFramebuffers;
    vk::CommandPool commandPool {};
    std::vector<vk::CommandBuffer> commandBuffers;
    vk::Extent2D swapChainExtent {};
    std::vector<vk::Semaphore> imageAvailableSemaphores;
    std::vector<vk::Semaphore> renderFinishedSemaphores;
    std::vector<vk::Fence> inFlightFences;
    std::vector<const char*> requiredValidationLayers;
    vk::Buffer vertexBuffer;
    vk::DeviceMemory vertexBufferMemory;
    std::vector<Vertex> vertices = {
        {{0.0f, -0.5f}, {1.0f, 0.0f, 0.0f}},
        {{0.5f, 0.5f}, {0.0f, 1.0f, 0.0f}},
        {{-0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}}
    };
    uint32_t currentFrame = 0;
    bool frameBufferResized = false;
    const Window& window;
};

}
