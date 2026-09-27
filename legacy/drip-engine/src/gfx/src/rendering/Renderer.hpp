#pragma once

// clang-format off
#include <drip/common/utils/Assert.hpp>
// clang-format on

#include <memory>
#include <vector>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_handles.hpp>

#include "drip/gfx/Window.hpp"
#include "drip/gfx/resource/MeshRenderable.hpp"
#include "vulkan/core/SwapChain.hpp"

namespace drip::gfx
{

class Renderer
{
public:
    Renderer(const Window& window, const Device& device, const vk::SurfaceKHR& surface);
    Renderer(const Renderer&) = delete;
    Renderer(Renderer&&) = delete;
    auto operator=(const Renderer&) = delete;
    auto operator=(Renderer&&) = delete;
    ~Renderer() noexcept;

    [[nodiscard]] auto beginFrame() -> vk::CommandBuffer;
    void endFrame();
    void beginSwapChainRenderPass() const;
    void endSwapChainRenderPass() const;

    [[nodiscard]] auto getAspectRatio() const noexcept -> float;
    [[nodiscard]] auto isFrameInProgress() const noexcept -> bool;
    [[nodiscard]] auto getCurrentCommandBuffer() const noexcept -> const vk::CommandBuffer&;
    [[nodiscard]] auto getSwapChainRenderPass() const noexcept -> const vk::RenderPass&;

    [[nodiscard]] auto getFrameIndex() const noexcept -> uint32_t;

private:
    [[nodiscard]] auto createCommandBuffers() const -> std::vector<vk::CommandBuffer>;

    const Device& _device;
    std::unique_ptr<SwapChain> _swapChain;
    std::vector<vk::CommandBuffer> _commandBuffers;
    std::vector<MeshRenderable> _objects;

    uint32_t _currentImageIndex = 0;
    uint32_t _currentFrameIndex = 0;
    bool _isFrameStarted = false;
};

}
