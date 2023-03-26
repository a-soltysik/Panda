VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

#include "Vulkan.h"

#include <algorithm>
#include <iostream>
#include <numeric>

#include "Shader.h"

namespace panda::gfx::vulkan
{

static VKAPI_ATTR VkBool32 VKAPI_CALL
debugCallback([[maybe_unused]] VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
              [[maybe_unused]] VkDebugUtilsMessageTypeFlagsEXT messageType,
              const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
              [[maybe_unused]] void* pUserData)
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
        debugMessenger = instance.createDebugUtilsMessengerEXT(debugMessengerCreateInfo);
    }
    surface = createSurface();
    if (!surface)
    {
        throw std::runtime_error {"Unable to create surface"};
    }
    else
    {
        log::Info("Created surface");
    }

    physicalDevice = pickPhysicalDevice();
    if (!physicalDevice)
    {
        throw std::runtime_error {"Unable to pick physical device"};
    }
    else
    {
        log::Info("Picked physical device: {}", physicalDevice.getProperties().deviceName);
    }

    queueFamiliesIndices = findQueueFamilies(physicalDevice, surface);
    if (!queueFamiliesIndices.hasValues())
    {
        throw std::runtime_error {"Not all queue families exist"};
    }
    device = createLogicalDevice();
    VULKAN_HPP_DEFAULT_DISPATCHER.init(device);

    graphicsQueue = device.getQueue(queueFamiliesIndices.graphicsFamily.value(), 0);
    presentationQueue = device.getQueue(queueFamiliesIndices.presentationFamily.value(), 0);
    swapchain = createSwapchain();
    swapChainImages = device.getSwapchainImagesKHR(swapchain);
    swapchainImageViews = createImageViews();
    renderPass = createRenderPass();
    pipeline = createPipeline();
    swapchainFramebuffers = createFrameBuffers();
    commandPool = createCommandPool();
    commandBuffers = createCommandBuffers();
    createSyncObjects();

    mainWindow.subscribeForFrameBufferResize([this](auto, auto) noexcept {
        frameBufferResized = true;
    });
}

Vulkan::~Vulkan() noexcept
{
    device.waitIdle();
    for (auto i = size_t {}; i < maxFramesInFlight; i++)
    {
        device.destroySemaphore(imageAvailableSemaphores[i]);
        device.destroySemaphore(renderFinishedSemaphores[i]);
        device.destroyFence(inFlightFences[i]);
    }
    cleanupSwapchain();
    device.destroyPipeline(pipeline);
    device.destroyPipelineLayout(pipelineLayout);
    device.destroyRenderPass(renderPass);
    device.destroyCommandPool(commandPool);
    device.destroy();

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

    if (!areRequiredExtensionsAvailable(requiredExtensions))
    {
        throw std::runtime_error {"There are missing extensions"};
    }

    auto createInfo =
        vk::InstanceCreateInfo({}, &appInfo, {}, {}, requiredExtensions.size(), requiredExtensions.data());

    if constexpr (shouldEnableValidationLayers())
    {
        if (!enableValidationLayers(createInfo))
        {
            std::cerr << "Unable to enable validation Layers\n";
        }

        createInfo.pNext = &debugMessengerCreateInfo;
    }
    return vk::createInstance(createInfo);
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
    for (const auto* requiredExtension : requiredExtensions)
    {
        const auto it = std::ranges::find_if(
            availableExtensions,
            [requiredExtension](const auto& availableExtension) {
                return std::string_view {requiredExtension} == std::string_view {availableExtension};
            },
            &vk::ExtensionProperties::extensionName);

        if (it == availableExtensions.cend())
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

auto Vulkan::pickPhysicalDevice() const -> vk::PhysicalDevice
{
    const auto devices = instance.enumeratePhysicalDevices();
    const auto it = std::ranges::find_if(devices, [this](const auto& currentDevice) {
        return isDeviceSuitable(currentDevice, surface);
    });

    if (it != devices.cend())
    {
        return *it;
    }
    log::Error("None of physical devices is suitable");
    return VK_NULL_HANDLE;
}

auto Vulkan::isDeviceSuitable(vk::PhysicalDevice device, vk::SurfaceKHR surface) -> bool
{
    const auto queueFamilies = findQueueFamilies(device, surface);
    const auto swapChainSupport = querySwapChainSupport(device, surface);
    return queueFamilies.hasValues() && checkDeviceExtensionSupport(device) && !swapChainSupport.formats.empty() &&
           !swapChainSupport.presentationModes.empty();
}

auto Vulkan::findQueueFamilies(vk::PhysicalDevice device, vk::SurfaceKHR surface) -> QueueFamilies
{
    const auto queueFamilies = device.getQueueFamilyProperties();
    auto queueFamilyIndices = QueueFamilies {};

    for (auto i = size_t {}; i < queueFamilies.size(); i++)
    {
        if (queueFamilies[i].queueFlags & vk::QueueFlagBits::eGraphics)
        {
            queueFamilyIndices.graphicsFamily = i;
        }
        if (device.getSurfaceSupportKHR(i, surface) != 0)
        {
            queueFamilyIndices.presentationFamily = i;
        }
        if (queueFamilyIndices.hasValues())
        {
            return queueFamilyIndices;
        }
    }
    return queueFamilyIndices;
}

auto Vulkan::areValidationLayersSupported() const -> bool
{
    const auto availableLayers = vk::enumerateInstanceLayerProperties();

    for (const auto* layerName : requiredValidationLayers)
    {
        const auto it = std::ranges::find_if(
            availableLayers,
            [layerName](const auto& availableLayer) {
                return std::string_view {layerName} == std::string_view {availableLayer};
            },
            &vk::LayerProperties::layerName);

        if (it == availableLayers.cend())
        {
            log::Error("{} layer is not supported", layerName);
            return false;
        }
    }

    return true;
}

auto Vulkan::createLogicalDevice() const -> vk::Device
{
    const auto uniqueFamilies = queueFamiliesIndices.getUniqueQueueFamilies();
    const auto queuePriority = 1.f;

    auto queueCreateInfos = std::vector<vk::DeviceQueueCreateInfo> {};
    queueCreateInfos.reserve(uniqueFamilies.size());

    for (const auto queueFamily : queueFamiliesIndices.getUniqueQueueFamilies())
    {
        queueCreateInfos.emplace_back(vk::DeviceQueueCreateInfo({}, queueFamily, 1, &queuePriority));
    }
    const auto physicalDeviceFeatures = vk::PhysicalDeviceFeatures {};

    if constexpr (shouldEnableValidationLayers())
    {
        const auto createInfo = vk::DeviceCreateInfo({},
                                                     queueCreateInfos,
                                                     requiredValidationLayers,
                                                     requiredDeviceExtensions,
                                                     &physicalDeviceFeatures);
        return physicalDevice.createDevice(createInfo);
    }
    else
    {
        const auto createInfo =
            vk::DeviceCreateInfo({}, queueCreateInfos, {}, requiredDeviceExtensions, &physicalDeviceFeatures);
        return physicalDevice.createDevice(createInfo);
    }
}

auto Vulkan::render() -> void
{
    if (device.waitForFences(1, &inFlightFences[currentFrame], VK_TRUE, std::numeric_limits<uint64_t>::max()) !=
        vk::Result::eSuccess) [[unlikely]]
    {
        log::Warning("Waiting for the fences didn't succeed");
    }

    const auto imageIndex = device.acquireNextImageKHR(swapchain,
                                                       std::numeric_limits<uint64_t>::max(),
                                                       imageAvailableSemaphores[currentFrame]);

    if (imageIndex.result == vk::Result::eErrorOutOfDateKHR) [[unlikely]]
    {
        recreateSwapchain();
        return;
    }

    if (device.resetFences(1, &inFlightFences[currentFrame]) != vk::Result::eSuccess) [[unlikely]]
    {
        log::Warning("Resetting the fences didn't succeed");
    }

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

    if (graphicsQueue.submit(1, &submitInfo, inFlightFences[currentFrame]) != vk::Result::eSuccess)
    {
        log::Warning("Submitting the graphics queue didn't succeeded");
    }

    const auto presentInfo =
        vk::PresentInfoKHR {1, &renderFinishedSemaphores[currentFrame], 1, &swapchain, &imageIndex.value};

    const auto presentationResult = presentationQueue.presentKHR(presentInfo);
    if (presentationResult == vk::Result::eErrorOutOfDateKHR || frameBufferResized) [[unlikely]]
    {
        frameBufferResized = false;
        recreateSwapchain();
    }
    else if (presentationResult != vk::Result::eSuccess) [[unlikely]]
    {
        log::Warning("Presenting the queue didn't succeeded");
    }

    currentFrame = (currentFrame + 1) % maxFramesInFlight;
}

auto Vulkan::createSurface() -> vk::SurfaceKHR
{
    auto* newSurface = VkSurfaceKHR {};
    glfwCreateWindowSurface(static_cast<VkInstance>(instance), window.getHandle(), nullptr, &newSurface);
    return newSurface;
}

auto Vulkan::checkDeviceExtensionSupport(vk::PhysicalDevice device) -> bool
{
    const auto availableExtensions = device.enumerateDeviceExtensionProperties();

    for (const auto* extension : requiredDeviceExtensions)
    {
        const auto it = std::ranges::find_if(
            availableExtensions,
            [extension](const auto& availableExtension) {
                return std::string_view {extension} == std::string_view {availableExtension};
            },
            &vk::ExtensionProperties::extensionName);

        if (it == availableExtensions.cend())
        {
            log::Error("{} extension is unavailable", extension);
            return false;
        }
    }
    return true;
}

auto Vulkan::querySwapChainSupport(vk::PhysicalDevice device, vk::SurfaceKHR surface) -> SwapChainSupportDetails
{
    return {device.getSurfaceCapabilitiesKHR(surface),
            device.getSurfaceFormatsKHR(surface),
            device.getSurfacePresentModesKHR(surface)};
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
    const auto swapChainSupport = querySwapChainSupport(physicalDevice, surface);

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

    if (queueFamiliesIndices.graphicsFamily != queueFamiliesIndices.presentationFamily)
    {
        const auto indicesArray =
            std::array {queueFamiliesIndices.graphicsFamily.value(), queueFamiliesIndices.presentationFamily.value()};
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
    return device.createSwapchainKHR(createInfo);
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
        imageViews.push_back(device.createImageView(createInfo));
    }

    return imageViews;
}

auto Vulkan::createPipeline() -> vk::Pipeline
{
    const auto fragmentShader = Shader::createFromFile(device, "shader/triangle.frag.spv").value();
    const auto vertexShader = Shader::createFromFile(device, "shader/triangle.vert.spv").value();

    const auto vertexShaderStageInfo =
        vk::PipelineShaderStageCreateInfo {{}, vk::ShaderStageFlagBits::eVertex, vertexShader.module, "main"};

    const auto fragmentShaderStageInfo =
        vk::PipelineShaderStageCreateInfo {{}, vk::ShaderStageFlagBits::eFragment, fragmentShader.module, "main"};

    const auto shaderStages = std::array {vertexShaderStageInfo, fragmentShaderStageInfo};
    const auto dynamicStates = std::array {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
    const auto dynamicState = vk::PipelineDynamicStateCreateInfo {{}, dynamicStates};

    const auto vertexInputInfo = vk::PipelineVertexInputStateCreateInfo {};
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
    pipelineLayout = device.createPipelineLayout(pipelineLayoutInfo);

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

    const auto result = device.createGraphicsPipeline(nullptr, pipelineInfo);

    device.destroy(fragmentShader.module);
    device.destroy(vertexShader.module);

    if (result.result == vk::Result::eSuccess)
    {
        log::Info("Pipeline created successfully");
        return result.value;
    }
    log::Error("Cannot create pipeline, error: {}",
               static_cast<std::underlying_type_t<decltype(result.result)>>(result.result));
    throw std::runtime_error {"Cannot create pipeline"};
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

    return device.createRenderPass(renderPassInfo);
}

auto Vulkan::createFrameBuffers() -> std::vector<vk::Framebuffer>
{
    auto result = std::vector<vk::Framebuffer> {};
    result.reserve(swapchainImageViews.size());
    for (const auto& imageView : swapchainImageViews)
    {
        const auto frameBufferInfo =
            vk::FramebufferCreateInfo {{}, renderPass, 1, &imageView, swapChainExtent.width, swapChainExtent.height, 1};
        result.push_back(device.createFramebuffer(frameBufferInfo));
    }
    return result;
}

auto Vulkan::createCommandPool() -> vk::CommandPool
{
    const auto queueFamilyIndices = findQueueFamilies(physicalDevice, surface);
    const auto commandPoolInfo = vk::CommandPoolCreateInfo {vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
                                                            queueFamilyIndices.graphicsFamily.value()};
    return device.createCommandPool(commandPoolInfo);
}

auto Vulkan::createCommandBuffers() -> std::vector<vk::CommandBuffer>
{
    const auto allocationInfo =
        vk::CommandBufferAllocateInfo {commandPool, vk::CommandBufferLevel::ePrimary, maxFramesInFlight};
    return device.allocateCommandBuffers(allocationInfo);
}

auto Vulkan::recordCommandBuffer(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) -> void
{
    const auto beginInfo = vk::CommandBufferBeginInfo {};
    commandBuffer.begin(beginInfo);

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

    commandBuffer.draw(3, 1, 0, 0);
    commandBuffer.endRenderPass();
    commandBuffer.end();
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
        imageAvailableSemaphores.push_back(device.createSemaphore(semaphoreInfo));
        renderFinishedSemaphores.push_back(device.createSemaphore(semaphoreInfo));
        inFlightFences.push_back(device.createFence(fenceInfo));
    }
}

auto Vulkan::recreateSwapchain() -> void
{
    device.waitIdle();

    cleanupSwapchain();
    swapchain = createSwapchain();
    swapChainImages = device.getSwapchainImagesKHR(swapchain);
    swapchainImageViews = createImageViews();
    swapchainFramebuffers = createFrameBuffers();

    log::Info("Recreated swapchain");
}

auto Vulkan::cleanupSwapchain() -> void
{
    for (const auto& frameBuffer : swapchainFramebuffers)
    {
        device.destroyFramebuffer(frameBuffer);
    }
    for (const auto& imageView : swapchainImageViews)
    {
        device.destroyImageView(imageView);
    }
    device.destroySwapchainKHR(swapchain);
}

auto Vulkan::QueueFamilies::getUniqueQueueFamilies() const -> std::unordered_set<uint32_t>
{
    auto families = std::unordered_set<uint32_t> {};
    if (graphicsFamily.has_value())
    {
        families.insert(graphicsFamily.value());
    }
    if (presentationFamily.has_value())
    {
        families.insert(presentationFamily.value());
    }
    return families;
}

auto Vulkan::QueueFamilies::hasValues() const noexcept -> bool
{
    return graphicsFamily.has_value() && presentationFamily.has_value();
}

}
