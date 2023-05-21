#pragma once

#include <optional>
#include <span>
#include <unordered_set>

#include "Buffer.h"
#include "Device.h"
#include "Pipeline.h"
#include "SwapChain.h"
#include "Vertex.h"
#include "app/Window.h"
#include "gfx/api/RenderingApi.h"

namespace panda::gfx::vulkan
{

class Vulkan : public RenderingApi
{
public:
    explicit Vulkan(Window& mainWindow);
    Vulkan(const Vulkan&) = delete;
    Vulkan(Vulkan&&) = default;
    auto operator=(const Vulkan&) -> Vulkan& = delete;
    auto operator=(Vulkan&&) -> Vulkan& = delete;
    ~Vulkan() noexcept override;

    auto render() -> void override;

private:
    struct InstanceDeleter
    {
        auto operator()(vk::Instance* instance) const noexcept -> void;
        const vk::SurfaceKHR& surface;
    };

    [[nodiscard]] static constexpr auto shouldEnableValidationLayers() noexcept -> bool
    {
        return PD_DEBUG;
    }

    [[nodiscard]] static auto getRequiredExtensions() -> std::vector<const char*>;
    [[nodiscard]] static auto createDebugMessengerCreateInfo() noexcept -> vk::DebugUtilsMessengerCreateInfoEXT;
    [[nodiscard]] static auto areRequiredExtensionsAvailable(std::span<const char* const> requiredExtensions) -> bool;

    [[nodiscard]] auto areValidationLayersSupported() const -> bool;
    [[nodiscard]] auto createInstance() -> std::unique_ptr<vk::Instance, InstanceDeleter>;
    [[nodiscard]] auto createSurface(const Window& window) -> vk::SurfaceKHR;
    [[nodiscard]] auto createPipeline() -> std::unique_ptr<Pipeline>;
    [[nodiscard]] auto createCommandBuffers() -> std::vector<vk::CommandBuffer>;
    auto recordCommandBuffer(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) -> void;
    [[nodiscard]] auto createVertexBuffer() -> std::unique_ptr<Buffer>;
    [[nodiscard]] auto createIndexBuffer() -> std::unique_ptr<Buffer>;

    auto enableValidationLayers(vk::InstanceCreateInfo& createInfo) -> bool;

    static constexpr auto maxFramesInFlight = size_t {2};
    static constexpr auto requiredDeviceExtensions = std::array {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    inline static const vk::DebugUtilsMessengerCreateInfoEXT debugMessengerCreateInfo =
        createDebugMessengerCreateInfo();

    std::unique_ptr<vk::Instance, InstanceDeleter> instance;
    std::unique_ptr<Device> device;
    std::unique_ptr<SwapChain> swapChain;
    std::unique_ptr<Pipeline> pipeline;
    vk::DebugUtilsMessengerEXT debugMessenger {};
    vk::SurfaceKHR surface {};
    vk::PipelineLayout pipelineLayout {};

    std::vector<vk::CommandBuffer> commandBuffers;
    std::vector<const char*> requiredValidationLayers;
    std::unique_ptr<Buffer> vertexBuffer;
    std::unique_ptr<Buffer> indexBuffer;

    const std::vector<Vertex> vertices = {
        {{-0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}},
        {{0.5f, -0.5f},  {0.0f, 1.0f, 0.0f}},
        {{0.5f, 0.5f},   {0.0f, 0.0f, 1.0f}},
        {{-0.5f, 0.5f},  {1.0f, 1.0f, 1.0f}}
    };
    const std::vector<uint16_t> indices = {0, 1, 2, 2, 3, 0};
};

}
