#pragma once

#include "Device.h"
#include "SwapChain.h"
#include "panda/Window.h"
#include "panda/gfx/vulkan/Object.h"

namespace panda::gfx::vulkan
{

class Renderer
{
public:
    Renderer(const Window& window, const Device& device, const vk::SurfaceKHR& surface);
    PD_DELETE_ALL(Renderer);
    ~Renderer() noexcept;

    [[nodiscard]] auto beginFrame() -> vk::CommandBuffer;
    auto endFrame() -> void;
    auto beginSwapChainRenderPass() const -> void;
    auto endSwapChainRenderPass() const -> void;

    [[nodiscard]] auto getAspectRatio() const noexcept -> float;
    [[nodiscard]] auto isFrameInProgress() const noexcept -> bool;
    [[nodiscard]] auto getCurrentCommandBuffer() const noexcept -> const vk::CommandBuffer&;
    [[nodiscard]] auto getSwapChainRenderPass() const noexcept -> const vk::RenderPass&;

    [[nodiscard]] auto getFrameIndex() const noexcept -> uint32_t;

private:
    [[nodiscard]] auto createCommandBuffers() -> std::vector<vk::CommandBuffer>;

    const Device& _device;
    std::unique_ptr<SwapChain> _swapChain;
    std::vector<vk::CommandBuffer> _commandBuffers;
    std::vector<Object> _objects;

    uint32_t _currentImageIndex = 0;
    uint32_t _currentFrameIndex = 0;
    bool _isFrameStarted = false;
};

}
