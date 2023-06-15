#include "Renderer.h"

#include "Vulkan.h"

namespace panda::gfx::vulkan
{
Renderer::Renderer(const Window& windowRef,
                   const Device& deviceRef,
                   const vk::SurfaceKHR& surface)
    : window {windowRef},
      device {deviceRef},
      swapChain {std::make_unique<SwapChain>(device, surface, window)},
      commandBuffers {createCommandBuffers()}
{
}

Renderer::~Renderer()
{
    device.logicalDevice.freeCommandBuffers(device.commandPool, commandBuffers);
}

auto Renderer::beginFrame() -> vk::CommandBuffer
{
    expectNot(isFrameStarted, "Can't begin frame when already began");

    const auto imageIndex = swapChain->acquireNextImage();
    if (!imageIndex.has_value())
    {
        return nullptr;
    }
    currentImageIndex = imageIndex.value();
    isFrameStarted = true;
    const auto commandBuffer = getCurrentCommandBuffer();
    const auto beginInfo = vk::CommandBufferBeginInfo {};
    expect(commandBuffer.begin(beginInfo), vk::Result::eSuccess, "Can't begin commandBuffer");
    return commandBuffer;
}

auto Renderer::endFrame() -> void
{
    expect(isFrameStarted, "Can't end frame which isn't began");
    expect(getCurrentCommandBuffer().end(), vk::Result::eSuccess, "Can't end command buffer");
    swapChain->submitCommandBuffers(getCurrentCommandBuffer(), currentImageIndex);

    isFrameStarted = false;
    currentFrameIndex = (currentFrameIndex + 1) % Vulkan::maxFramesInFlight;
}

auto Renderer::beginSwapChainRenderPass() const -> void
{
    expect(isFrameStarted, "Can't begin render pass when frame is not began");
    const auto clearColor = vk::ClearValue {
        vk::ClearColorValue {0.f, 0.f, 0.f, 1.f}
    };
    const auto depthStencil = vk::ClearValue {
        vk::ClearDepthStencilValue {1.f, 0}
    };
    const auto clearValues = std::array {clearColor, depthStencil};
    const auto renderPassBeginInfo = vk::RenderPassBeginInfo {
        swapChain->getRenderPass(),
        swapChain->getFrameBuffer(currentImageIndex),
        {{0, 0}, swapChain->getExtent()},
        clearValues
    };

    const auto commandBuffer = getCurrentCommandBuffer();

    commandBuffer.beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eInline);

    const auto viewport = vk::Viewport {0.f,
                                        0.f,
                                        static_cast<float>(swapChain->getExtent().width),
                                        static_cast<float>(swapChain->getExtent().height),
                                        0.f,
                                        1.f};
    commandBuffer.setViewport(0, viewport);

    const auto scissor = vk::Rect2D {
        {0, 0},
        swapChain->getExtent()
    };

    commandBuffer.setScissor(0, scissor);
}

auto Renderer::endSwapChainRenderPass() const -> void
{
    expect(isFrameStarted, "Can't end render pass when frame is not began");
    getCurrentCommandBuffer().endRenderPass();
}

auto Renderer::isFrameInProgress() const noexcept -> bool
{
    return isFrameStarted;
}

auto Renderer::getCurrentCommandBuffer() const noexcept -> const vk::CommandBuffer&
{
    expect(isFrameStarted, "Can't get command buffer when frame not in progress");
    return commandBuffers[currentFrameIndex];
}

auto Renderer::getSwapChainRenderPass() const noexcept -> const vk::RenderPass&
{
    return swapChain->getRenderPass();
}

auto Renderer::createCommandBuffers() -> std::vector<vk::CommandBuffer>
{
    const auto allocationInfo = vk::CommandBufferAllocateInfo {device.commandPool,
                                                               vk::CommandBufferLevel::ePrimary,
                                                               static_cast<uint32_t>(swapChain->imagesCount())};
    return expect(device.logicalDevice.allocateCommandBuffers(allocationInfo),
                  vk::Result::eSuccess,
                  "Can't allocate command buffer");
}

auto Renderer::getFrameIndex() const noexcept -> uint32_t
{
    expect(isFrameStarted, "Can't get frame index which is not in progress");
    return currentFrameIndex;
}

}