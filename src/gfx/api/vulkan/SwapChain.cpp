#include "SwapChain.h"

#include "utils/format/api/vulkan/ResultFormatter.h"

namespace panda::gfx::vulkan
{

SwapChain::SwapChain(const Device& deviceRef,
                     const vk::SurfaceKHR& surfaceRef,
                     const Window& windowRef,
                     size_t maxFrames)
    : device {deviceRef},
      window {windowRef},
      surface {surfaceRef},
      maxFramesInFlight {maxFrames},
      swapChainExtent {chooseSwapExtent(device.querySwapChainSupport().capabilities, window)},
      swapChainImageFormat {chooseSwapSurfaceFormat(device.querySwapChainSupport().formats)},
      swapChain {createSwapChain(surface, swapChainExtent, device, swapChainImageFormat)},
      swapChainImages {expect(
          device.logicalDevice.getSwapchainImagesKHR(swapChain), vk::Result::eSuccess, "Can't get swapchain images")},
      swapChainImageViews {createImageViews(swapChainImages, swapChainImageFormat, device)},
      depthImages {createDepthImages(device, swapChainExtent, swapChainImages.size())},
      depthImageMemories {createDepthImageMemories(device, depthImages, swapChainImages.size())},
      depthImageViews {createDepthImageViews(device, depthImages, swapChainImages.size())},
      renderPass {createRenderPass(swapChainImageFormat, device)},
      swapChainFramebuffers {
          createFrameBuffers(swapChainImageViews, depthImageViews, renderPass, swapChainExtent, device)},
      frameBufferResizeReceiver { utils::Signals::frameBufferResized.connect([this](auto, auto) noexcept {
          log::Debug("Received framebuffer resized notif");
          frameBufferResized = true;
      })}
{
    createSyncObjects();
}

SwapChain::~SwapChain() noexcept
{
    cleanup();

    device.logicalDevice.destroy(renderPass);
    for (const auto semaphore : renderFinishedSemaphores)
    {
        device.logicalDevice.destroy(semaphore);
    }
    for (const auto semaphore : imageAvailableSemaphores)
    {
        device.logicalDevice.destroy(semaphore);
    }
    for (const auto fence : inFlightFences)
    {
        device.logicalDevice.destroy(fence);
    }
}

auto SwapChain::choosePresentationMode(std::span<const vk::PresentModeKHR> availablePresentationModes) noexcept
    -> vk::PresentModeKHR
{
    const auto it = std::ranges::find(availablePresentationModes, vk::PresentModeKHR::eMailbox);

    if (it == availablePresentationModes.end())
    {
        log::Warning("Choosing default mode: Fifo");
        return vk::PresentModeKHR::eFifo;
    }
    return *it;
}

auto SwapChain::chooseSwapSurfaceFormat(std::span<const vk::SurfaceFormatKHR> availableFormats) noexcept
    -> vk::SurfaceFormatKHR
{
    const auto it = std::ranges::find_if(availableFormats, [](const auto& availableFormat) noexcept {
        return availableFormat.format == vk::Format::eB8G8R8A8Srgb &&
               availableFormat.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear;
    });

    if (it == availableFormats.end())
    {
        log::Warning("Choosing default format");
        return availableFormats.front();
    }
    return *it;
}

auto SwapChain::chooseSwapExtent(const vk::SurfaceCapabilitiesKHR& capabilities, const Window& window) -> vk::Extent2D
{
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) [[likely]]
    {
        return capabilities.currentExtent;
    }

    const auto size = window.getSize();

    return {std::clamp<uint32_t>(size.x, capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
            std::clamp<uint32_t>(size.y, capabilities.minImageExtent.height, capabilities.maxImageExtent.height)};
}

auto SwapChain::createImageViews(const std::vector<vk::Image>& swapChainImages,
                                 const vk::SurfaceFormatKHR& swapChainImageFormat,
                                 const Device& device) -> std::vector<vk::ImageView>
{
    auto imageViews = std::vector<vk::ImageView> {};
    imageViews.reserve(swapChainImages.size());

    for (const auto& image : swapChainImages)
    {
        const auto subResourceRange = vk::ImageSubresourceRange {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
        const auto createInfo = vk::ImageViewCreateInfo {{},
                                                         image,
                                                         vk::ImageViewType::e2D,
                                                         swapChainImageFormat.format,
                                                         {},
                                                         subResourceRange};

        imageViews.push_back(
            expect(device.logicalDevice.createImageView(createInfo), vk::Result::eSuccess, "Can't create image view"));
    }

    return imageViews;
}

auto SwapChain::createRenderPass(const vk::SurfaceFormatKHR& swapChainImageFormat, const Device& device)
    -> vk::RenderPass
{
    const auto depthAttachment = vk::AttachmentDescription {{},
                                                            findDepthFormat(device),
                                                            vk::SampleCountFlagBits::e1,
                                                            vk::AttachmentLoadOp::eClear,
                                                            vk::AttachmentStoreOp::eDontCare,
                                                            vk::AttachmentLoadOp::eDontCare,
                                                            vk::AttachmentStoreOp::eDontCare,
                                                            vk::ImageLayout::eUndefined,
                                                            vk::ImageLayout::eDepthStencilAttachmentOptimal};

    const auto depthAttachmentRef = vk::AttachmentReference {1, vk::ImageLayout::eDepthStencilAttachmentOptimal};

    const auto colorAttachment = vk::AttachmentDescription {{},
                                                            swapChainImageFormat.format,
                                                            vk::SampleCountFlagBits::e1,
                                                            vk::AttachmentLoadOp::eClear,
                                                            vk::AttachmentStoreOp::eStore,
                                                            vk::AttachmentLoadOp::eDontCare,
                                                            vk::AttachmentStoreOp::eDontCare,
                                                            vk::ImageLayout::eUndefined,
                                                            vk::ImageLayout::ePresentSrcKHR};

    const auto colorAttachmentRef = vk::AttachmentReference {0, vk::ImageLayout::eColorAttachmentOptimal};

    const auto subpass = vk::SubpassDescription {{},
                                                 vk::PipelineBindPoint::eGraphics,
                                                 {},
                                                 {},
                                                 1,
                                                 &colorAttachmentRef,
                                                 {},
                                                 &depthAttachmentRef};

    const auto dependency = vk::SubpassDependency {VK_SUBPASS_EXTERNAL,
                                                   0,
                                                   vk::PipelineStageFlagBits::eColorAttachmentOutput,
                                                   vk::PipelineStageFlagBits::eColorAttachmentOutput,
                                                   vk::AccessFlagBits::eNone,
                                                   vk::AccessFlagBits::eColorAttachmentWrite};

    const auto attachments = std::array {colorAttachment, depthAttachment};

    const auto renderPassInfo = vk::RenderPassCreateInfo {{}, attachments, subpass, dependency};

    return expect(device.logicalDevice.createRenderPass(renderPassInfo),
                  vk::Result::eSuccess,
                  "Can't create render pass");
}

auto SwapChain::createFrameBuffers(const std::vector<vk::ImageView>& swapChainImageViews,
                                   const std::vector<vk::ImageView>& depthImageViews,
                                   const vk::RenderPass& renderPass,
                                   vk::Extent2D swapChainExtent,
                                   const Device& device) -> std::vector<vk::Framebuffer>
{
    expect(swapChainImageViews.size() == depthImageViews.size(),
           "Swap chain image views count is different than depth image views count");
    auto result = std::vector<vk::Framebuffer> {};
    result.reserve(swapChainImageViews.size());

    for (auto i = size_t {}; i < swapChainImageViews.size(); i++)
    {
        const auto attachments = std::array {swapChainImageViews[i], depthImageViews[i]};
        const auto frameBufferInfo =
            vk::FramebufferCreateInfo {{}, renderPass, attachments, swapChainExtent.width, swapChainExtent.height, 1};
        result.push_back(expect(device.logicalDevice.createFramebuffer(frameBufferInfo),
                                vk::Result::eSuccess,
                                "Can't create framebuffer"));
    }
    return result;
}

auto SwapChain::createSyncObjects() -> void
{
    const auto semaphoreInfo = vk::SemaphoreCreateInfo {};
    const auto fenceInfo = vk::FenceCreateInfo {vk::FenceCreateFlagBits::eSignaled};

    imageAvailableSemaphores.reserve(maxFramesInFlight);
    renderFinishedSemaphores.reserve(maxFramesInFlight);
    inFlightFences.reserve(maxFramesInFlight);
    imagesInFlight.resize(imagesCount());

    for (auto i = size_t {}; i < maxFramesInFlight; i++)
    {
        imageAvailableSemaphores.push_back(expect(device.logicalDevice.createSemaphore(semaphoreInfo),
                                                  vk::Result::eSuccess,
                                                  "Failed to createSemaphore"));
        renderFinishedSemaphores.push_back(expect(device.logicalDevice.createSemaphore(semaphoreInfo),
                                                  vk::Result::eSuccess,
                                                  "Failed to createSemaphore"));
        inFlightFences.push_back(
            expect(device.logicalDevice.createFence(fenceInfo), vk::Result::eSuccess, "Failed to create fence"));
    }
}

auto SwapChain::recreate() -> void
{
    log::Info("Starting to recreate swapchain");
    shouldBe(device.logicalDevice.waitIdle(), vk::Result::eSuccess, "Wait idle didn't succeed");

    cleanup();
    const auto swapChainSupport = device.querySwapChainSupport();
    swapChainExtent = chooseSwapExtent(swapChainSupport.capabilities, window);
    swapChainImageFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
    swapChain = createSwapChain(surface, swapChainExtent, device, swapChainImageFormat);
    swapChainImages = expect(device.logicalDevice.getSwapchainImagesKHR(swapChain),
                             vk::Result::eSuccess,
                             "Can't get swapchain images");
    swapChainImageViews = createImageViews(swapChainImages, swapChainImageFormat, device);
    depthImages = createDepthImages(device, swapChainExtent, swapChainImages.size());
    depthImageMemories = createDepthImageMemories(device, depthImages, swapChainImages.size());
    depthImageViews = createDepthImageViews(device, depthImages, swapChainImages.size());
    swapChainFramebuffers = createFrameBuffers(swapChainImageViews, depthImageViews, renderPass, swapChainExtent, device);

    log::Info("Swapchain recreated");
}

auto SwapChain::cleanup() -> void
{
    for (const auto framebuffer : swapChainFramebuffers)
    {
        device.logicalDevice.destroy(framebuffer);
    }
    for (const auto imageView : swapChainImageViews)
    {
        device.logicalDevice.destroy(imageView);
    }
    for (const auto image : depthImages)
    {
        device.logicalDevice.destroy(image);
    }
    for (const auto imageView : depthImageViews)
    {
        device.logicalDevice.destroy(imageView);
    }
    for (const auto imageMemory : depthImageMemories)
    {
        device.logicalDevice.free(imageMemory);
    }
    device.logicalDevice.destroy(swapChain);
}

auto SwapChain::createSwapChain(const vk::SurfaceKHR& surface,
                                const vk::Extent2D extent,
                                const Device& device,
                                const vk::SurfaceFormatKHR& surfaceFormat) -> vk::SwapchainKHR
{
    const auto swapChainSupport = device.querySwapChainSupport();
    const auto presentationMode = choosePresentationMode(swapChainSupport.presentationModes);

    auto imageCount = swapChainSupport.capabilities.minImageCount + 1;
    if (swapChainSupport.capabilities.maxImageCount > 0 && imageCount > swapChainSupport.capabilities.maxImageCount)
    {
        imageCount = swapChainSupport.capabilities.maxImageCount;
    }

    auto createInfo = vk::SwapchainCreateInfoKHR {{},
                                                  surface,
                                                  imageCount,
                                                  surfaceFormat.format,
                                                  surfaceFormat.colorSpace,
                                                  extent,
                                                  1,
                                                  vk::ImageUsageFlagBits::eColorAttachment,
                                                  {},
                                                  {},
                                                  swapChainSupport.capabilities.currentTransform,
                                                  vk::CompositeAlphaFlagBitsKHR::eOpaque,
                                                  presentationMode,
                                                  VK_TRUE};

    if (device.queueFamilies.graphicsFamily != device.queueFamilies.presentationFamily)
    {
        const auto indicesArray =
            std::array {device.queueFamilies.graphicsFamily, device.queueFamilies.presentationFamily};
        createInfo.imageSharingMode = vk::SharingMode::eConcurrent;
        createInfo.setQueueFamilyIndices(indicesArray);
    }
    else
    {
        createInfo.imageSharingMode = vk::SharingMode::eExclusive;
    }

    return expect(device.logicalDevice.createSwapchainKHR(createInfo), vk::Result::eSuccess, "Can't create swapchain");
}

auto SwapChain::getRenderPass() const noexcept -> const vk::RenderPass&
{
    return renderPass;
}

auto SwapChain::getFrameBuffer(size_t index) const noexcept -> const vk::Framebuffer&
{
    return swapChainFramebuffers[index];
}

auto SwapChain::getExtent() const noexcept -> const vk::Extent2D&
{
    return swapChainExtent;
}

auto SwapChain::findDepthFormat(const Device& device) -> vk::Format
{
    return expect(device.findSupportedFormat(
                      std::array {vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint, vk::Format::eD24UnormS8Uint},
                      vk::ImageTiling::eOptimal,
                      vk::FormatFeatureFlagBits::eDepthStencilAttachment),
                  "Failed to find supported format");
}

auto SwapChain::acquireNextImage() -> std::optional<uint32_t>
{
    if (frameBufferResized) [[unlikely]]
    {
        frameBufferResized = false;
        recreate();
        return {};
    }

    shouldBe(
        device.logicalDevice.waitForFences(inFlightFences[currentFrame], VK_TRUE, std::numeric_limits<uint64_t>::max()),
        vk::Result::eSuccess,
        "Waiting for the fences didn't succeed");

    const auto imageIndex = device.logicalDevice.acquireNextImageKHR(swapChain,
                                                                     std::numeric_limits<uint64_t>::max(),
                                                                     imageAvailableSemaphores[currentFrame]);

    if (imageIndex.result == vk::Result::eErrorOutOfDateKHR || imageIndex.result == vk::Result::eSuboptimalKHR)
        [[unlikely]]
    {
        recreate();
        return {};
    }
    else if (imageIndex.result != vk::Result::eSuccess) [[unlikely]]
    {
        panic("Failed to acquire swap chain image");
    }

    return imageIndex.value;
}

auto SwapChain::submitCommandBuffers(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) -> void
{
    if (imagesInFlight[imageIndex]) [[likely]]
    {
        shouldBe(device.logicalDevice.waitForFences(*imagesInFlight[imageIndex],
                                                    VK_TRUE,
                                                    std::numeric_limits<uint64_t>::max()),
                 vk::Result::eSuccess,
                 "Failed to wait for imagesInFlight fence");
    }
    imagesInFlight[imageIndex] = &inFlightFences[currentFrame];

    static constexpr auto waitStages =
        std::array {vk::PipelineStageFlags {vk::PipelineStageFlagBits::eColorAttachmentOutput}};
    const auto submitInfo = vk::SubmitInfo {imageAvailableSemaphores[currentFrame],
                                            waitStages,
                                            commandBuffer,
                                            renderFinishedSemaphores[currentFrame]};

    shouldBe(device.logicalDevice.resetFences(inFlightFences[currentFrame]),
             vk::Result::eSuccess,
             "Failed to Reset inFlight fence");
    shouldBe(device.graphicsQueue.submit(submitInfo, inFlightFences[currentFrame]),
             vk::Result::eSuccess,
             "Submitting the graphics queue didn't succeeded");

    const auto presentInfo = vk::PresentInfoKHR {renderFinishedSemaphores[currentFrame], swapChain, imageIndex};

    const auto presentationResult = device.presentationQueue.presentKHR(presentInfo);

    if (presentationResult == vk::Result::eErrorOutOfDateKHR || presentationResult == vk::Result::eSuboptimalKHR)
        [[unlikely]]
    {
        recreate();
    }
    else if (presentationResult != vk::Result::eSuccess) [[unlikely]]
    {
        log::Warning("Presenting the queue didn't succeeded: {}", presentationResult);
    }

    currentFrame = (currentFrame + 1) % maxFramesInFlight;
}

auto SwapChain::imagesCount() const noexcept -> size_t
{
    return swapChainImages.size();
}

auto SwapChain::createDepthImages(const Device& device, vk::Extent2D swapChainExtent, size_t imagesCount)
    -> std::vector<vk::Image>
{
    const auto depthFormat = findDepthFormat(device);
    auto depthImages = std::vector<vk::Image> {};
    depthImages.reserve(imagesCount);

    for (auto i = size_t {}; i < imagesCount; i++)
    {
        const auto imageInfo = vk::ImageCreateInfo {
            {},
            vk::ImageType::e2D,
            depthFormat,
            {swapChainExtent.width, swapChainExtent.height, 1},
            1,
            1,
            vk::SampleCountFlagBits::e1,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eDepthStencilAttachment,
            vk::SharingMode::eExclusive
        };
        depthImages.push_back(
            expect(device.logicalDevice.createImage(imageInfo), vk::Result::eSuccess, "Failed to create depth image"));
    }
    return depthImages;
}

auto SwapChain::createDepthImageViews(const Device& device,
                                      const std::vector<vk::Image>& depthImages,
                                      size_t imagesCount) -> std::vector<vk::ImageView>
{
    const auto depthFormat = findDepthFormat(device);
    auto depthImageViews = std::vector<vk::ImageView> {};
    depthImageViews.reserve(imagesCount);

    for (auto i = size_t {}; i < imagesCount; i++)
    {
        const auto viewInfo = vk::ImageViewCreateInfo {
            {},
            depthImages[i],
            vk::ImageViewType::e2D,
            depthFormat,
            {},
            {vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1}
        };
        depthImageViews.push_back(expect(device.logicalDevice.createImageView(viewInfo),
                                         vk::Result::eSuccess,
                                         "Failed to create depth image view"));
    }
    return depthImageViews;
}

auto SwapChain::createDepthImageMemories(const Device& device,
                                         const std::vector<vk::Image>& depthImages,
                                         size_t imagesCount) -> std::vector<vk::DeviceMemory>
{
    auto depthImageMemories = std::vector<vk::DeviceMemory> {};
    depthImageMemories.reserve(imagesCount);

    for (auto i = size_t {}; i < imagesCount; i++)
    {
        const auto memoryRequirements = device.logicalDevice.getImageMemoryRequirements(depthImages[i]);
        const auto allocInfo = vk::MemoryAllocateInfo {
            memoryRequirements.size,
            expect(device.findMemoryType(memoryRequirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal), "Failed to find memory type")};
        depthImageMemories.push_back(expect(device.logicalDevice.allocateMemory(allocInfo),
                                            vk::Result::eSuccess,
                                            "Failed to allocate depth image memory"));
        expect(device.logicalDevice.bindImageMemory(depthImages[i], depthImageMemories[i], 0),
               vk::Result::eSuccess,
               "Failed to bind depth image memory");
    }
    return depthImageMemories;
}

}