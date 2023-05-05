VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

#include "Vulkan.h"

#include <algorithm>
#include <iostream>
#include <numeric>

#include "Shader.h"
#include "utils/format/api/vulkan/ResultFormatter.h"

namespace panda::gfx::vulkan
{

namespace
{
VKAPI_ATTR auto VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                                         [[maybe_unused]] VkDebugUtilsMessageTypeFlagsEXT messageType,
                                         const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
                                         [[maybe_unused]] void* pUserData) -> VkBool32
{
    switch (messageSeverity)
    {
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
        log::Debug(pCallbackData->pMessage);
        break;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
        log::Info(pCallbackData->pMessage);
        break;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
        log::Warning(pCallbackData->pMessage);
        break;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
        log::Error(pCallbackData->pMessage);
        break;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_FLAG_BITS_MAX_ENUM_EXT:
        log::Debug(pCallbackData->pMessage);
        break;
    }

    return VK_FALSE;
}

}

Vulkan::Vulkan(Window& mainWindow)
    : window {mainWindow}
{
    const auto dynamicLoader = vk::DynamicLoader {};
    const auto vkGetInstanceProcAddr = dynamicLoader.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
    VULKAN_HPP_DEFAULT_DISPATCHER.init(vkGetInstanceProcAddr);

    instance = createInstance();
    VULKAN_HPP_DEFAULT_DISPATCHER.init(instance);

    if constexpr (shouldEnableValidationLayers())
    {
        debugMessenger = expect(instance.createDebugUtilsMessengerEXT(debugMessengerCreateInfo),
                                vk::Result::eSuccess,
                                "Unable to create debug messenger");
    }
    surface = createSurface();
    log::Info("Created surface");

    if constexpr (shouldEnableValidationLayers())
    {
        device = std::make_unique<Device>(instance, surface, requiredDeviceExtensions, requiredValidationLayers);
    }
    else
    {
        device = std::make_unique<Device>(instance, surface, requiredDeviceExtensions);
    }

    VULKAN_HPP_DEFAULT_DISPATCHER.init(device->logicalDevice);

    graphicsQueue = device->logicalDevice.getQueue(device->queueFamilies.graphicsFamily, 0);
    presentationQueue = device->logicalDevice.getQueue(device->queueFamilies.presentationFamily, 0);
    swapchain = createSwapchain();
    swapChainImages = expect(device->logicalDevice.getSwapchainImagesKHR(swapchain),
                             vk::Result::eSuccess,
                             "Unable to get swapchain images");
    swapchainImageViews = createImageViews();
    renderPass = createRenderPass();
    pipeline = createPipeline();
    log::Info("Pipeline created successfully");

    swapchainFramebuffers = createFrameBuffers();
    commandPool = createCommandPool();
    vertexBuffer = createVertexBuffer();

    const auto memoryRequirements = device->logicalDevice.getBufferMemoryRequirements(vertexBuffer);
    const auto allocInfo = vk::MemoryAllocateInfo {
        memoryRequirements.size,
        findMemoryType(memoryRequirements.memoryTypeBits,
                       vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent)};

    vertexBufferMemory = expect(device->logicalDevice.allocateMemory(allocInfo),
                                vk::Result::eSuccess,
                                "Failed to allocate vertex buffer memory");
    expect(device->logicalDevice.bindBufferMemory(vertexBuffer, vertexBufferMemory, 0),
           vk::Result::eSuccess,
           "Failed to bind vertex buffer");

    auto* data =
        expect(device->logicalDevice.mapMemory(vertexBufferMemory, 0, sizeof(vertices[0]) * vertices.size(), {}),
               vk::Result::eSuccess,
               "Failed to map memory of vertex buffer");
    std::copy(vertices.cbegin(), vertices.cend(), static_cast<decltype(vertices)::value_type*>(data));
    device->logicalDevice.unmapMemory(vertexBufferMemory);

    commandBuffers = createCommandBuffers();
    createSyncObjects();

    mainWindow.subscribeForFrameBufferResize([this](auto, auto) noexcept {
        log::Debug("Received framebuffer resized notif");
        frameBufferResized = true;
    });
}

Vulkan::~Vulkan() noexcept
{
    shouldBe(device->logicalDevice.waitIdle(), vk::Result::eSuccess, "Wait idle didn't succeed");

    for (auto i = size_t {}; i < maxFramesInFlight; i++)
    {
        device->logicalDevice.destroySemaphore(imageAvailableSemaphores[i]);
        device->logicalDevice.destroySemaphore(renderFinishedSemaphores[i]);
        device->logicalDevice.destroyFence(inFlightFences[i]);
    }
    cleanupSwapchain();
    device->logicalDevice.destroyBuffer(vertexBuffer);
    device->logicalDevice.freeMemory(vertexBufferMemory);
    device->logicalDevice.destroyPipeline(pipeline);
    device->logicalDevice.destroyPipelineLayout(pipelineLayout);
    device->logicalDevice.destroyRenderPass(renderPass);
    device->logicalDevice.destroyCommandPool(commandPool);
    device->logicalDevice.destroy();

    if constexpr (shouldEnableValidationLayers())
    {
        instance.destroyDebugUtilsMessengerEXT(debugMessenger);
    }

    instance.destroySurfaceKHR(surface);
    instance.destroy();
}

auto Vulkan::createInstance() -> vk::Instance
{
    const auto appInfo = vk::ApplicationInfo(PROJECT_NAME,
                                             VK_API_VERSION_1_0,
                                             ENGINE_TARGET_NAME,
                                             VK_API_VERSION_1_0,
                                             VK_API_VERSION_1_3);

    const auto requiredExtensions = getRequiredExtensions();

    expect(areRequiredExtensionsAvailable(requiredExtensions), true, "There are missing extensions");

    auto createInfo =
        vk::InstanceCreateInfo({}, &appInfo, {}, {}, requiredExtensions.size(), requiredExtensions.data());

    if constexpr (shouldEnableValidationLayers())
    {
        shouldBe(enableValidationLayers(createInfo), true, "Unable to enable validation layers");
        createInfo.pNext = &debugMessengerCreateInfo;
    }

    return expect(vk::createInstance(createInfo), vk::Result::eSuccess, "Creating instance didn't succeed");
}

auto Vulkan::enableValidationLayers(vk::InstanceCreateInfo& createInfo) -> bool
{
    requiredValidationLayers.push_back("VK_LAYER_KHRONOS_validation");

    if (areValidationLayersSupported())
    {
        createInfo.enabledLayerCount = requiredValidationLayers.size();
        createInfo.ppEnabledLayerNames = requiredValidationLayers.data();

        return true;
    }
    return false;
}

auto Vulkan::getRequiredExtensions() -> std::vector<const char*>
{
    auto glfwExtensionsCount = uint32_t {};
    const auto* glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionsCount);

    auto extensions = std::vector(glfwExtensions, glfwExtensions + glfwExtensionsCount);

    if constexpr (shouldEnableValidationLayers())
    {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    }

    return extensions;
}

auto Vulkan::areRequiredExtensionsAvailable(std::span<const char* const> requiredExtensions) -> bool
{
    const auto availableExtensions = vk::enumerateInstanceExtensionProperties();
    if (availableExtensions.result != vk::Result::eSuccess)
    {
        log::Error("Can't get available extensions: {}", availableExtensions.result);
        return false;
    }
    for (const auto* requiredExtension : requiredExtensions)
    {
        const auto it = std::ranges::find_if(
            availableExtensions.value,
            [requiredExtension](const auto& availableExtension) {
                return std::string_view {requiredExtension} == std::string_view {availableExtension};
            },
            &vk::ExtensionProperties::extensionName);

        if (it == availableExtensions.value.cend())
        {
            log::Error("{} extension is unavailable", requiredExtension);
            return false;
        }
    }
    return true;
}

auto Vulkan::createDebugMessengerCreateInfo() noexcept -> vk::DebugUtilsMessengerCreateInfoEXT
{
    static constexpr auto severityMask =
        vk::DebugUtilsMessageSeverityFlagBitsEXT::eError | vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
        vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo | vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose;

    static constexpr auto typeMask =
        vk::DebugUtilsMessageTypeFlagBitsEXT::eDeviceAddressBinding | vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
        vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance;
    return {{}, severityMask, typeMask, &debugCallback};
}

auto Vulkan::areValidationLayersSupported() const -> bool
{
    const auto availableLayers = vk::enumerateInstanceLayerProperties();
    if (availableLayers.result != vk::Result::eSuccess)
    {
        log::Error("Can't enumerate available layers");
        return false;
    }

    for (const auto* layerName : requiredValidationLayers)
    {
        const auto it = std::ranges::find_if(
            availableLayers.value,
            [layerName](const auto& availableLayer) {
                return std::string_view {layerName} == std::string_view {availableLayer};
            },
            &vk::LayerProperties::layerName);

        if (it == availableLayers.value.cend())
        {
            log::Warning("{} layer is not supported", layerName);
            return false;
        }
    }

    return true;
}

auto Vulkan::render() -> void
{
    shouldBe(device->logicalDevice.waitForFences(1,
                                                 &inFlightFences[currentFrame],
                                                 VK_TRUE,
                                                 std::numeric_limits<uint64_t>::max()),
             vk::Result::eSuccess,
             "Waiting for the fences didn't succeed");
    shouldBe(device->logicalDevice.waitIdle(), vk::Result::eSuccess, "Wait idle didn't succeed");

    if (frameBufferResized)
    {
        frameBufferResized = false;
        recreateSwapchain();
        return;
    }

    const auto imageIndex = device->logicalDevice.acquireNextImageKHR(swapchain,
                                                                      std::numeric_limits<uint64_t>::max(),
                                                                      imageAvailableSemaphores[currentFrame]);

    if (imageIndex.result == vk::Result::eErrorOutOfDateKHR || imageIndex.result == vk::Result::eSuboptimalKHR)
        [[unlikely]]
    {
        frameBufferResized = false;
        recreateSwapchain();

        return;
    }
    else if (imageIndex.result != vk::Result::eSuccess) [[unlikely]]
    {
        panic("Failed to acquire swap chain image");
    }

    shouldBe(device->logicalDevice.resetFences(1, &inFlightFences[currentFrame]),
             vk::Result::eSuccess,
             "Resetting the fences didn't succeed");

    commandBuffers[currentFrame].reset();
    recordCommandBuffer(commandBuffers[currentFrame], imageIndex.value);
    static constexpr auto waitStages =
        std::array {vk::PipelineStageFlags {vk::PipelineStageFlagBits::eColorAttachmentOutput}};
    const auto submitInfo = vk::SubmitInfo {1,
                                            &imageAvailableSemaphores[currentFrame],
                                            waitStages.data(),
                                            1,
                                            &commandBuffers[currentFrame],
                                            1,
                                            &renderFinishedSemaphores[currentFrame]};

    shouldBe(graphicsQueue.submit(1, &submitInfo, inFlightFences[currentFrame]),
             vk::Result::eSuccess,
             "Submitting the graphics queue didn't succeeded");

    const auto presentInfo = vk::PresentInfoKHR {renderFinishedSemaphores[currentFrame], swapchain, imageIndex.value};

    const auto presentationResult = presentationQueue.presentKHR(presentInfo);

    if (presentationResult == vk::Result::eErrorOutOfDateKHR || presentationResult == vk::Result::eSuboptimalKHR ||
        frameBufferResized) [[unlikely]]
    {
        frameBufferResized = false;
        recreateSwapchain();
    }
    else if (presentationResult != vk::Result::eSuccess) [[unlikely]]
    {
        log::Warning("Presenting the queue didn't succeeded: {}", presentationResult);
    }

    currentFrame = (currentFrame + 1) % maxFramesInFlight;
}

auto Vulkan::createSurface() -> vk::SurfaceKHR
{
    auto* newSurface = VkSurfaceKHR {};
    glfwCreateWindowSurface(static_cast<VkInstance>(instance), window.getHandle(), nullptr, &newSurface);

    return expect(
        newSurface,
        [](const auto* result) {
            return result != nullptr;
        },
        "Unable to create surface");
}

auto Vulkan::choosePresentationMode(std::span<const vk::PresentModeKHR> availablePresentationModes) noexcept
    -> vk::PresentModeKHR
{
    const auto it = std::ranges::find(availablePresentationModes, vk::PresentModeKHR::eMailbox);

    if (it == availablePresentationModes.end())
    {
        return vk::PresentModeKHR::eFifo;
    }
    return *it;
}

auto Vulkan::chooseSwapSurfaceFormat(std::span<const vk::SurfaceFormatKHR> availableFormats) noexcept
    -> vk::SurfaceFormatKHR
{
    const auto it = std::ranges::find_if(availableFormats, [](const auto& availableFormat) noexcept {
        return availableFormat.format == vk::Format::eB8G8R8A8Srgb &&
               availableFormat.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear;
    });

    if (it == availableFormats.end())
    {
        return availableFormats.front();
    }
    return *it;
}

auto Vulkan::chooseSwapExtent(const vk::SurfaceCapabilitiesKHR& capabilities) const -> vk::Extent2D
{
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
    {
        return capabilities.currentExtent;
    }
    auto width = int {};
    auto height = int {};

    glfwGetFramebufferSize(window.getHandle(), &width, &height);

    return {std::clamp<uint32_t>(width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
            std::clamp<uint32_t>(height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height)};
}

auto Vulkan::createSwapchain() -> vk::SwapchainKHR
{
    const auto swapChainSupport = device->querySwapChainSupport();

    const auto surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
    const auto presentationMode = choosePresentationMode(swapChainSupport.presentationModes);
    const auto extent = chooseSwapExtent(swapChainSupport.capabilities);

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
                                                  1};

    if (device->queueFamilies.graphicsFamily != device->queueFamilies.presentationFamily)
    {
        const auto indicesArray =
            std::array {device->queueFamilies.graphicsFamily, device->queueFamilies.presentationFamily};
        createInfo.imageSharingMode = vk::SharingMode::eConcurrent;
        createInfo.queueFamilyIndexCount = indicesArray.size();
        createInfo.pQueueFamilyIndices = indicesArray.data();
    }
    else
    {
        createInfo.imageSharingMode = vk::SharingMode::eExclusive;
    }

    swapChainImageFormat = surfaceFormat.format;
    swapChainExtent = extent;

    return expect(device->logicalDevice.createSwapchainKHR(createInfo), vk::Result::eSuccess, "Can't create swapchain");
}

auto Vulkan::createImageViews() -> std::vector<vk::ImageView>
{
    auto imageViews = std::vector<vk::ImageView> {};
    imageViews.reserve(swapChainImages.size());

    for (const auto& image : swapChainImages)
    {
        const auto subResourceRange = vk::ImageSubresourceRange {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
        const auto createInfo =
            vk::ImageViewCreateInfo {{}, image, vk::ImageViewType::e2D, swapChainImageFormat, {}, subResourceRange};

        imageViews.push_back(
            expect(device->logicalDevice.createImageView(createInfo), vk::Result::eSuccess, "Can't create image view"));
    }

    return imageViews;
}

auto Vulkan::createPipeline() -> vk::Pipeline
{
    const auto fragmentShader = Shader::createFromFile(device->logicalDevice, "shader/triangle.frag.spv");
    const auto vertexShader = Shader::createFromFile(device->logicalDevice, "shader/triangle.vert.spv");

    auto shaders = std::vector<vk::ShaderModule> {};

    if (fragmentShader.has_value())
    {
        shaders.push_back(fragmentShader->module);
    }
    if (vertexShader.has_value())
    {
        shaders.push_back(vertexShader->module);
    }

    const auto vertexShaderStageInfo =
        vk::PipelineShaderStageCreateInfo {{}, vk::ShaderStageFlagBits::eVertex, vertexShader->module, "main"};

    const auto fragmentShaderStageInfo =
        vk::PipelineShaderStageCreateInfo {{}, vk::ShaderStageFlagBits::eFragment, fragmentShader->module, "main"};

    const auto shaderStages = std::array {vertexShaderStageInfo, fragmentShaderStageInfo};
    const auto dynamicStates = std::array {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
    const auto dynamicState = vk::PipelineDynamicStateCreateInfo {{}, dynamicStates};

    static constexpr auto bindingDescription = Vertex::getBindingDescription();
    static constexpr auto attributeDescriptions = Vertex::getAttributeDescriptions();

    const auto vertexInputInfo = vk::PipelineVertexInputStateCreateInfo {{}, bindingDescription, attributeDescriptions};
    const auto inputAssembly =
        vk::PipelineInputAssemblyStateCreateInfo {{}, vk::PrimitiveTopology::eTriangleList, VK_FALSE};

    const auto viewportState = vk::PipelineViewportStateCreateInfo {{}, 1, {}, 1, {}};
    const auto rasterizer = vk::PipelineRasterizationStateCreateInfo {{},
                                                                      VK_FALSE,
                                                                      VK_FALSE,
                                                                      vk::PolygonMode::eFill,
                                                                      vk::CullModeFlagBits::eBack,
                                                                      vk::FrontFace::eClockwise,
                                                                      VK_FALSE,
                                                                      {},
                                                                      {},
                                                                      {},
                                                                      1.f};

    const auto multisampling = vk::PipelineMultisampleStateCreateInfo {{}, vk::SampleCountFlagBits::e1, VK_FALSE};
    const auto colorBlendAttachment =
        vk::PipelineColorBlendAttachmentState {VK_FALSE,
                                               vk::BlendFactor::eSrcAlpha,
                                               vk::BlendFactor::eOneMinusSrcAlpha,
                                               vk::BlendOp::eAdd,
                                               vk::BlendFactor::eOne,
                                               vk::BlendFactor::eZero,
                                               vk::BlendOp::eAdd,
                                               vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                                                   vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA};

    const auto colorBlending =
        vk::PipelineColorBlendStateCreateInfo {{}, VK_FALSE, vk::LogicOp::eCopy, 1, &colorBlendAttachment};
    const auto pipelineLayoutInfo = vk::PipelineLayoutCreateInfo {};
    pipelineLayout = expect(device->logicalDevice.createPipelineLayout(pipelineLayoutInfo),
                            vk::Result::eSuccess,
                            "Can't create pipeline layout");

    const auto pipelineInfo = vk::GraphicsPipelineCreateInfo {{},
                                                              shaderStages,
                                                              &vertexInputInfo,
                                                              &inputAssembly,
                                                              {},
                                                              &viewportState,
                                                              &rasterizer,
                                                              &multisampling,
                                                              {},
                                                              &colorBlending,
                                                              &dynamicState,
                                                              pipelineLayout,
                                                              renderPass,
                                                              0};

    return expect(device->logicalDevice.createGraphicsPipeline(nullptr, pipelineInfo),
                  vk::Result::eSuccess,
                  "Cannot create pipeline");
}

auto Vulkan::createRenderPass() -> vk::RenderPass
{
    const auto colorAttachment = vk::AttachmentDescription {{},
                                                            swapChainImageFormat,
                                                            vk::SampleCountFlagBits::e1,
                                                            vk::AttachmentLoadOp::eClear,
                                                            vk::AttachmentStoreOp::eStore,
                                                            vk::AttachmentLoadOp::eDontCare,
                                                            vk::AttachmentStoreOp::eDontCare,
                                                            vk::ImageLayout::eUndefined,
                                                            vk::ImageLayout::ePresentSrcKHR};

    const auto dependency = vk::SubpassDependency {VK_SUBPASS_EXTERNAL,
                                                   0,
                                                   vk::PipelineStageFlagBits::eColorAttachmentOutput,
                                                   vk::PipelineStageFlagBits::eColorAttachmentOutput,
                                                   vk::AccessFlagBits::eNone,
                                                   vk::AccessFlagBits::eColorAttachmentWrite};

    const auto colorAttachmentRef = vk::AttachmentReference {0, vk::ImageLayout::eColorAttachmentOptimal};
    const auto subpass = vk::SubpassDescription {{}, vk::PipelineBindPoint::eGraphics, {}, {}, 1, &colorAttachmentRef};
    const auto renderPassInfo = vk::RenderPassCreateInfo {{}, 1, &colorAttachment, 1, &subpass, 1, &dependency};

    return expect(device->logicalDevice.createRenderPass(renderPassInfo),
                  vk::Result::eSuccess,
                  "Can't create render pass");
}

auto Vulkan::createFrameBuffers() -> std::vector<vk::Framebuffer>
{
    auto result = std::vector<vk::Framebuffer> {};
    result.reserve(swapchainImageViews.size());
    for (const auto& imageView : swapchainImageViews)
    {
        const auto frameBufferInfo =
            vk::FramebufferCreateInfo {{}, renderPass, 1, &imageView, swapChainExtent.width, swapChainExtent.height, 1};
        result.push_back(expect(device->logicalDevice.createFramebuffer(frameBufferInfo),
                                vk::Result::eSuccess,
                                "Can't create framebuffer"));
    }
    return result;
}

auto Vulkan::createCommandPool() -> vk::CommandPool
{
    const auto commandPoolInfo = vk::CommandPoolCreateInfo {vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
                                                            device->queueFamilies.graphicsFamily};
    return expect(device->logicalDevice.createCommandPool(commandPoolInfo),
                  vk::Result::eSuccess,
                  "Can't create command pool");
}

auto Vulkan::createCommandBuffers() -> std::vector<vk::CommandBuffer>
{
    const auto allocationInfo =
        vk::CommandBufferAllocateInfo {commandPool, vk::CommandBufferLevel::ePrimary, maxFramesInFlight};
    return expect(device->logicalDevice.allocateCommandBuffers(allocationInfo),
                  vk::Result::eSuccess,
                  "Can't allocate command buffer");
}

auto Vulkan::recordCommandBuffer(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) -> void
{
    const auto beginInfo = vk::CommandBufferBeginInfo {};
    expect(commandBuffer.begin(beginInfo), vk::Result::eSuccess, "Can't begin commandBuffer");

    const auto clearColor = vk::ClearValue {
        {0.f, 0.f, 0.f, 1.f}
    };
    const auto renderPassBeginInfo = vk::RenderPassBeginInfo {
        renderPass,
        swapchainFramebuffers[imageIndex],
        {{0, 0}, swapChainExtent},
        1,
        &clearColor
    };
    commandBuffer.beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eInline);
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
    commandBuffer.bindVertexBuffers(0, vertexBuffer, {0});

    const auto viewport = vk::Viewport {0.f,
                                        0.f,
                                        static_cast<float>(swapChainExtent.width),
                                        static_cast<float>(swapChainExtent.height),
                                        0.f,
                                        1.f};

    commandBuffer.setViewport(0, 1, &viewport);

    const auto scissor = vk::Rect2D {
        {0, 0},
        swapChainExtent
    };

    commandBuffer.setScissor(0, 1, &scissor);

    commandBuffer.draw(vertices.size(), 1, 0, 0);
    commandBuffer.endRenderPass();
    expect(commandBuffer.end(), vk::Result::eSuccess, "Can't end command buffer");
}

auto Vulkan::createSyncObjects() -> void
{
    const auto semaphoreInfo = vk::SemaphoreCreateInfo {};
    const auto fenceInfo = vk::FenceCreateInfo {vk::FenceCreateFlagBits::eSignaled};

    imageAvailableSemaphores.reserve(maxFramesInFlight);
    renderFinishedSemaphores.reserve(maxFramesInFlight);
    inFlightFences.reserve(maxFramesInFlight);

    for (auto i = size_t {}; i < maxFramesInFlight; i++)
    {
        imageAvailableSemaphores.push_back(device->logicalDevice.createSemaphore(semaphoreInfo).value);
        renderFinishedSemaphores.push_back(device->logicalDevice.createSemaphore(semaphoreInfo).value);
        inFlightFences.push_back(device->logicalDevice.createFence(fenceInfo).value);
    }
}

auto Vulkan::recreateSwapchain() -> void
{
    log::Info("Starting to recreate swapchain");
    shouldBe(device->logicalDevice.waitIdle(), vk::Result::eSuccess, "Wait idle didn't succeed");

    cleanupSwapchain();
    swapchain = createSwapchain();
    swapChainImages = expect(device->logicalDevice.getSwapchainImagesKHR(swapchain),
                             vk::Result::eSuccess,
                             "Can't get swapchain images");
    swapchainImageViews = createImageViews();
    swapchainFramebuffers = createFrameBuffers();

    log::Info("Swapchain recreated");
}

auto Vulkan::cleanupSwapchain() -> void
{
    for (const auto& frameBuffer : swapchainFramebuffers)
    {
        device->logicalDevice.destroyFramebuffer(frameBuffer);
    }
    for (const auto& imageView : swapchainImageViews)
    {
        device->logicalDevice.destroyImageView(imageView);
    }
    device->logicalDevice.destroySwapchainKHR(swapchain);
}

auto Vulkan::createVertexBuffer() -> vk::Buffer
{
    const auto bufferInfo = vk::BufferCreateInfo {{},
                                                  sizeof(vertices[0]) * vertices.size(),
                                                  vk::BufferUsageFlagBits::eVertexBuffer,
                                                  vk::SharingMode::eExclusive};
    return expect(device->logicalDevice.createBuffer(bufferInfo), vk::Result::eSuccess, "Can't create vertex buffer");
}

auto Vulkan::findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties) -> uint32_t
{
    const auto memoryProperties = device->physicalDevice.getMemoryProperties();

    for (auto i = uint32_t {}; i < memoryProperties.memoryTypeCount; i++)
    {
        if ((typeFilter & (uint32_t {1} << i)) &&
            (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties)
        {
            return i;
        }
    }
    panic("Failed to find suitable memory type");
}

}
