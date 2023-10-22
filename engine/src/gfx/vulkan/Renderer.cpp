#include "panda/gfx/vulkan/Renderer.h"

#include "panda/gfx/vulkan/Context.h"

namespace panda::gfx::vulkan
{
Renderer::Renderer(const Window& window, const Device& device, const vk::SurfaceKHR& surface)
    : _device {device},
      _swapChain {std::make_unique<SwapChain>(device, surface, window)},
      _commandBuffers {createCommandBuffers()}
{
}

Renderer::~Renderer() noexcept
{
    _device.logicalDevice.freeCommandBuffers(_device.commandPool, _commandBuffers);
}

auto Renderer::beginFrame() -> vk::CommandBuffer
{
    expectNot(_isFrameStarted, "Can't begin frame when already began");

    const auto imageIndex = _swapChain->acquireNextImage();
    if (!imageIndex.has_value())
    {
        return nullptr;
    }
    _currentImageIndex = imageIndex.value();
    _isFrameStarted = true;
    const auto commandBuffer = getCurrentCommandBuffer();
    const auto beginInfo = vk::CommandBufferBeginInfo {};
    expect(commandBuffer.begin(beginInfo), vk::Result::eSuccess, "Can't begin commandBuffer");
    return commandBuffer;
}

auto Renderer::endFrame() -> void
{
    expect(_isFrameStarted, "Can't end frame which isn't began");
    expect(getCurrentCommandBuffer().end(), vk::Result::eSuccess, "Can't end command buffer");
    _swapChain->submitCommandBuffers(getCurrentCommandBuffer(), _currentImageIndex);

    _isFrameStarted = false;
    _currentFrameIndex = (_currentFrameIndex + 1) % Context::maxFramesInFlight;
}

auto Renderer::beginSwapChainRenderPass() const -> void
{
    expect(_isFrameStarted, "Can't begin render pass when frame is not began");
    const auto clearColor = vk::ClearValue {
        vk::ClearColorValue {0.f, 0.f, 0.f, 1.f}
    };
    const auto depthStencil = vk::ClearValue {
        vk::ClearDepthStencilValue {1.f, 0}
    };
    const auto clearValues = std::array {clearColor, depthStencil};
    const auto renderPassBeginInfo = vk::RenderPassBeginInfo {
        _swapChain->getRenderPass(),
        _swapChain->getFrameBuffer(_currentImageIndex),
        {{0, 0}, _swapChain->getExtent()},
        clearValues
    };

    const auto commandBuffer = getCurrentCommandBuffer();

    commandBuffer.beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eInline);

    const auto viewport = vk::Viewport {0.f,
                                        0.f,
                                        static_cast<float>(_swapChain->getExtent().width),
                                        static_cast<float>(_swapChain->getExtent().height),
                                        0.f,
                                        1.f};
    commandBuffer.setViewport(0, viewport);

    const auto scissor = vk::Rect2D {
        {0, 0},
        _swapChain->getExtent()
    };

    commandBuffer.setScissor(0, scissor);
}

auto Renderer::endSwapChainRenderPass() const -> void
{
    expect(_isFrameStarted, "Can't end render pass when frame is not began");
    getCurrentCommandBuffer().endRenderPass();
}

auto Renderer::isFrameInProgress() const noexcept -> bool
{
    return _isFrameStarted;
}

auto Renderer::getCurrentCommandBuffer() const noexcept -> const vk::CommandBuffer&
{
    expect(_isFrameStarted, "Can't get command buffer when frame not in progress");
    return _commandBuffers[_currentFrameIndex];
}

auto Renderer::getSwapChainRenderPass() const noexcept -> const vk::RenderPass&
{
    return _swapChain->getRenderPass();
}

auto Renderer::createCommandBuffers() -> std::vector<vk::CommandBuffer>
{
    const auto allocationInfo = vk::CommandBufferAllocateInfo {_device.commandPool,
                                                               vk::CommandBufferLevel::ePrimary,
                                                               static_cast<uint32_t>(_swapChain->imagesCount())};
    return expect(_device.logicalDevice.allocateCommandBuffers(allocationInfo),
                  vk::Result::eSuccess,
                  "Can't allocate command buffer");
}

auto Renderer::getFrameIndex() const noexcept -> uint32_t
{
    expect(_isFrameStarted, "Can't get frame index which is not in progress");
    return _currentFrameIndex;
}

auto Renderer::getAspectRatio() const noexcept -> float
{
    return _swapChain->getExtentAspectRatio();
}

auto Renderer::beginSingleTimeCommandBuffer() const noexcept -> vk::CommandBuffer
{
    const auto allocationInfo =
        vk::CommandBufferAllocateInfo {_device.commandPool, vk::CommandBufferLevel::ePrimary, 1};
    const auto commandBuffer = expect(_device.logicalDevice.allocateCommandBuffers(allocationInfo),
                                      vk::Result::eSuccess,
                                      "Can't allocate command buffer");
    const auto beginInfo = vk::CommandBufferBeginInfo {vk::CommandBufferUsageFlagBits::eOneTimeSubmit};
    expect(commandBuffer.front().begin(beginInfo), vk::Result::eSuccess, "Couldn't begin command buffer");
    return commandBuffer.front();
}

auto Renderer::endSingleTimeCommandBuffer(vk::CommandBuffer buffer) const noexcept -> void
{
    expect(buffer.end(), vk::Result::eSuccess, "Couldn't end command buffer");

    const auto submitInfo = vk::SubmitInfo {{}, {}, buffer};
    expect(_device.graphicsQueue.submit(submitInfo), vk::Result::eSuccess, "Couldn't submit graphics queue");
    shouldBe(_device.graphicsQueue.waitIdle(), vk::Result::eSuccess, "Couldn't wait idle on graphics queue");
    _device.logicalDevice.free(_device.commandPool, buffer);
}

}