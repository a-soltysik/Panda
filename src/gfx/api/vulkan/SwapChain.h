#pragma once

#include <app/Window.h>

#include "Device.h"

namespace panda::gfx::vulkan
{

class SwapChain
{
public:
    SwapChain(const Device& device, const vk::SurfaceKHR& surface, const Window& window);
    PD_DELETE_ALL(SwapChain);
    ~SwapChain() noexcept;

    [[nodiscard]] auto getRenderPass() const noexcept -> const vk::RenderPass&;
    [[nodiscard]] auto getFrameBuffer(size_t index) const noexcept -> const vk::Framebuffer&;
    [[nodiscard]] auto getExtent() const noexcept -> const vk::Extent2D&;
    [[nodiscard]] auto acquireNextImage() -> std::optional<uint32_t>;
    [[nodiscard]] auto imagesCount() const noexcept -> size_t;
    auto submitCommandBuffers(const vk::CommandBuffer& commandBuffers, uint32_t imageIndex) -> void;

private:
    [[nodiscard]] static auto createSwapChain(const vk::SurfaceKHR& surface,
                                              vk::Extent2D extent,
                                              const Device& device,
                                              const vk::SurfaceFormatKHR& surfaceFormat) -> vk::SwapchainKHR;
    [[nodiscard]] static auto chooseSwapSurfaceFormat(std::span<const vk::SurfaceFormatKHR> availableFormats) noexcept
        -> vk::SurfaceFormatKHR;
    [[nodiscard]] static auto choosePresentationMode(
        std::span<const vk::PresentModeKHR> availablePresentationModes) noexcept -> vk::PresentModeKHR;
    [[nodiscard]] static auto chooseSwapExtent(const vk::SurfaceCapabilitiesKHR& capabilities, const Window& window)
        -> vk::Extent2D;

    [[nodiscard]] static auto createImageViews(const std::vector<vk::Image>& swapChainImages,
                                               const vk::SurfaceFormatKHR& swapChainImageFormat,
                                               const Device& device) -> std::vector<vk::ImageView>;
    [[nodiscard]] static auto createRenderPass(const vk::SurfaceFormatKHR& imageFormat,
                                               const vk::SurfaceFormatKHR& depthFormat,
                                               const Device& device) -> vk::RenderPass;
    [[nodiscard]] static auto createFrameBuffers(const std::vector<vk::ImageView>& swapChainImageViews,
                                                 const std::vector<vk::ImageView>& depthImageViews,
                                                 const vk::RenderPass& renderPass,
                                                 vk::Extent2D swapChainExtent,
                                                 const Device& device) -> std::vector<vk::Framebuffer>;
    [[nodiscard]] static auto createDepthImages(const Device& device,
                                                vk::Extent2D swapChainExtent,
                                                size_t imagesCount,
                                                const vk::SurfaceFormatKHR& depthFormat) -> std::vector<vk::Image>;
    [[nodiscard]] static auto createDepthImageViews(const Device& device,
                                                    const std::vector<vk::Image>& depthImages,
                                                    size_t imagesCount,
                                                    const vk::SurfaceFormatKHR& depthFormat)
        -> std::vector<vk::ImageView>;
    [[nodiscard]] static auto createDepthImageMemories(const Device& device,
                                                       const std::vector<vk::Image>& depthImages,
                                                       size_t imagesCount) -> std::vector<vk::DeviceMemory>;
    [[nodiscard]] static auto findDepthFormat(const Device& device) -> vk::Format;

    auto createSyncObjects() -> void;
    auto cleanup() -> void;
    auto recreate() -> void;

    const Device& device;
    const Window& window;
    const vk::SurfaceKHR& surface;

    vk::Extent2D swapChainExtent;
    vk::SurfaceFormatKHR swapChainImageFormat;
    vk::SurfaceFormatKHR swapChainDepthFormat;
    vk::SwapchainKHR swapChain;
    std::vector<vk::Image> swapChainImages;
    std::vector<vk::ImageView> swapChainImageViews;
    std::vector<vk::Image> depthImages;
    std::vector<vk::DeviceMemory> depthImageMemories;
    std::vector<vk::ImageView> depthImageViews;

    vk::RenderPass renderPass {};
    std::vector<vk::Framebuffer> swapChainFramebuffers;
    std::vector<vk::Semaphore> imageAvailableSemaphores;
    std::vector<vk::Semaphore> renderFinishedSemaphores;
    std::vector<vk::Fence> inFlightFences;
    std::vector<vk::Fence*> imagesInFlight;

    utils::Signals::FrameBufferResized::ReceiverT frameBufferResizeReceiver;
    uint32_t currentFrame = 0;
    bool frameBufferResized = false;
};

}
