VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

#include "Vulkan.h"

#include <algorithm>
#include <iostream>

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
    : instance {createInstance()}
{
    VULKAN_HPP_DEFAULT_DISPATCHER.init(*instance);

    if constexpr (shouldEnableValidationLayers())
    {
        debugMessenger = expect(instance->createDebugUtilsMessengerEXT(debugMessengerCreateInfo),
                                vk::Result::eSuccess,
                                "Unable to create debug messenger");
        log::Info("Debug messenger is created");
    }
    surface = createSurface(mainWindow);
    log::Info("Created surface successfully");

    if constexpr (shouldEnableValidationLayers())
    {
        device = std::make_unique<Device>(*instance, surface, requiredDeviceExtensions, requiredValidationLayers);
    }
    else
    {
        device = std::make_unique<Device>(*instance, surface, requiredDeviceExtensions);
    }
    log::Info("Created device successfully");
    log::Info("Chosen GPU: {}", std::string_view {device->physicalDevice.getProperties().deviceName});

    VULKAN_HPP_DEFAULT_DISPATCHER.init(device->logicalDevice);

    swapChain = std::make_unique<SwapChain>(*device, surface, mainWindow, maxFramesInFlight);
    log::Info("Created swap chain successfully");
    pipeline = createPipeline();
    log::Info("Created pipeline successfully");

    vertexBuffer = createVertexBuffer();
    log::Info("Created vertex buffer successfully");
    indexBuffer = createIndexBuffer();
    log::Info("Created index buffer successfully");

    commandBuffers = createCommandBuffers();
    log::Info("Created command buffers successfully");

    log::Info("Vulkan API has been successfully initialized");
}

Vulkan::~Vulkan() noexcept
{
    log::Info("Starting closing Vulkan API");

    shouldBe(device->logicalDevice.waitIdle(), vk::Result::eSuccess, "Wait idle didn't succeed");

    device->logicalDevice.destroyPipelineLayout(pipelineLayout);

    if constexpr (shouldEnableValidationLayers())
    {
        instance->destroyDebugUtilsMessengerEXT(debugMessenger);
    }
}

auto Vulkan::createInstance() -> std::unique_ptr<vk::Instance, InstanceDeleter>
{
    const auto dynamicLoader = vk::DynamicLoader {};
    const auto vkGetInstanceProcAddr = dynamicLoader.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
    VULKAN_HPP_DEFAULT_DISPATCHER.init(vkGetInstanceProcAddr);

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

    return std::unique_ptr<vk::Instance, InstanceDeleter> {
        new vk::Instance {
            expect(vk::createInstance(createInfo), vk::Result::eSuccess, "Creating instance didn't succeed")},
        InstanceDeleter {surface}};
}

auto Vulkan::enableValidationLayers(vk::InstanceCreateInfo& createInfo) -> bool
{
    requiredValidationLayers.push_back("VK_LAYER_KHRONOS_validation");

    if (areValidationLayersSupported())
    {
        createInfo.setPEnabledLayerNames(requiredValidationLayers);

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
    const auto imageIndex = swapChain->acquireNextImage();
    if (!imageIndex.has_value())
    {
        return;
    }

    commandBuffers[imageIndex.value()].reset();
    recordCommandBuffer(commandBuffers[imageIndex.value()], imageIndex.value());

    swapChain->submitCommandBuffers(commandBuffers[imageIndex.value()], imageIndex.value());
}

auto Vulkan::createSurface(const Window& window) -> vk::SurfaceKHR
{
    auto* newSurface = VkSurfaceKHR {};
    glfwCreateWindowSurface(static_cast<VkInstance>(*instance), window.getHandle(), nullptr, &newSurface);

    return expect(
        newSurface,
        [](const auto* result) {
            return result != nullptr;
        },
        "Unable to create surface");
}

auto Vulkan::createPipeline() -> std::unique_ptr<Pipeline>
{
    const auto inputAssemblyInfo =
        vk::PipelineInputAssemblyStateCreateInfo {{}, vk::PrimitiveTopology::eTriangleList, VK_FALSE};

    const auto viewportInfo = vk::PipelineViewportStateCreateInfo {{}, 1, {}, 1, {}};
    const auto rasterizationInfo = vk::PipelineRasterizationStateCreateInfo {{},
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

    const auto multisamplingInfo = vk::PipelineMultisampleStateCreateInfo {{}, vk::SampleCountFlagBits::e1, VK_FALSE};
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

    const auto colorBlendInfo =
        vk::PipelineColorBlendStateCreateInfo {{}, VK_FALSE, vk::LogicOp::eCopy, colorBlendAttachment};
    const auto pipelineLayoutInfo = vk::PipelineLayoutCreateInfo {};
    pipelineLayout = expect(device->logicalDevice.createPipelineLayout(pipelineLayoutInfo),
                            vk::Result::eSuccess,
                            "Can't create pipeline layout");

    return std::make_unique<Pipeline>(*device,
                                      PipelineConfig {"shader/triangle.vert.spv",
                                                      "shader/triangle.frag.spv",
                                                      inputAssemblyInfo,
                                                      viewportInfo,
                                                      rasterizationInfo,
                                                      multisamplingInfo,
                                                      colorBlendInfo,
                                                      {},
                                                      pipelineLayout,
                                                      swapChain->getRenderPass(),
                                                      0});
}

auto Vulkan::createCommandBuffers() -> std::vector<vk::CommandBuffer>
{
    const auto allocationInfo = vk::CommandBufferAllocateInfo {device->commandPool,
                                                               vk::CommandBufferLevel::ePrimary,
                                                               static_cast<uint32_t>(swapChain->imagesCount())};
    return expect(device->logicalDevice.allocateCommandBuffers(allocationInfo),
                  vk::Result::eSuccess,
                  "Can't allocate command buffer");
}

auto Vulkan::recordCommandBuffer(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) -> void
{
    const auto beginInfo = vk::CommandBufferBeginInfo {};
    expect(commandBuffer.begin(beginInfo), vk::Result::eSuccess, "Can't begin commandBuffer");

    const auto clearColor = vk::ClearValue {
        vk::ClearColorValue {0.f, 0.f, 0.f, 1.f}
    };
    const auto depthStencil = vk::ClearValue {
        vk::ClearDepthStencilValue {1.f, 0}
    };
    const auto clearValues = std::array {clearColor, depthStencil};
    const auto& swapChainExtent = swapChain->getExtent();
    const auto renderPassBeginInfo = vk::RenderPassBeginInfo {
        swapChain->getRenderPass(),
        swapChain->getFrameBuffer(imageIndex),
        {{0, 0}, swapChainExtent},
        clearValues
    };
    commandBuffer.beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eInline);
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline->getHandle());
    commandBuffer.bindVertexBuffers(0, vertexBuffer->buffer, {0});
    commandBuffer.bindIndexBuffer(indexBuffer->buffer, 0, vk::IndexType::eUint16);

    const auto viewport = vk::Viewport {0.f,
                                        0.f,
                                        static_cast<float>(swapChain->getExtent().width),
                                        static_cast<float>(swapChain->getExtent().height),
                                        0.f,
                                        1.f};
    commandBuffer.setViewport(0, viewport);

    const auto scissor = vk::Rect2D {
        {0, 0},
        swapChainExtent
    };

    commandBuffer.setScissor(0, scissor);

    commandBuffer.drawIndexed(indices.size(), 1, 0, 0, 0);
    commandBuffer.endRenderPass();
    expect(commandBuffer.end(), vk::Result::eSuccess, "Can't end command buffer");
}

auto Vulkan::createVertexBuffer() -> std::unique_ptr<Buffer>
{
    const auto stagingBuffer =
        Buffer {*device,
                vertices,
                vk::BufferUsageFlagBits::eTransferSrc,
                vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent};

    auto newVertexBuffer =
        std::make_unique<Buffer>(*device,
                                 stagingBuffer.size,
                                 vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst,
                                 vk::MemoryPropertyFlagBits::eDeviceLocal);

    Buffer::copy(stagingBuffer, *newVertexBuffer);

    return newVertexBuffer;
}

auto Vulkan::createIndexBuffer() -> std::unique_ptr<Buffer>
{
    const auto stagingBuffer =
        Buffer {*device,
                indices,
                vk::BufferUsageFlagBits::eTransferSrc,
                vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent};

    auto newIndexBuffer =
        std::make_unique<Buffer>(*device,
                                 stagingBuffer.size,
                                 vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst,
                                 vk::MemoryPropertyFlagBits::eDeviceLocal);

    Buffer::copy(stagingBuffer, *newIndexBuffer);

    return newIndexBuffer;
}

auto Vulkan::InstanceDeleter::operator()(vk::Instance* instance) const noexcept -> void
{
    log::Info("Destroying instance");
    instance->destroy(surface);
    instance->destroy();
}
}
