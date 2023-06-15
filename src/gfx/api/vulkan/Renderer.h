#pragma once

#include <app/Window.h>

#include "Device.h"
#include "Object.h"
#include "SwapChain.h"

namespace panda::gfx::vulkan
{

class Renderer
{
public:
    Renderer(const Window& windowRef, const Device& deviceRef, const vk::SurfaceKHR& surface);
    PD_DELETE_ALL(Renderer);
    ~Renderer() noexcept;

    [[nodiscard]] auto beginFrame() -> vk::CommandBuffer;
    auto endFrame() -> void;
    auto beginSwapChainRenderPass() const -> void;
    auto endSwapChainRenderPass() const -> void;

    [[nodiscard]] auto isFrameInProgress() const noexcept -> bool;
    [[nodiscard]] auto getCurrentCommandBuffer() const noexcept -> const vk::CommandBuffer&;
    [[nodiscard]] auto getSwapChainRenderPass() const noexcept -> const vk::RenderPass&;

    [[nodiscard]] auto getFrameIndex() const noexcept -> uint32_t;

private:
    [[nodiscard]] auto createCommandBuffers() -> std::vector<vk::CommandBuffer>;

    const Window& window;
    const Device& device;
    std::unique_ptr<SwapChain> swapChain;
    std::vector<vk::CommandBuffer> commandBuffers;
    std::vector<Object> objects;

    uint32_t currentImageIndex = 0;
    uint32_t currentFrameIndex = 0;
    bool isFrameStarted = false;

};

}
