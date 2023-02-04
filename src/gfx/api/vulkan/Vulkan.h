#pragma once

#include <optional>

#include "gfx/api/RenderingApi.h"

namespace panda::gfx::vulkan
{

class Vulkan : public RenderingApi
{
public:
    Vulkan();
    Vulkan(const Vulkan&) = delete;
    Vulkan(Vulkan&&) = default;
    auto operator=(const Vulkan&) -> Vulkan& = delete;
    auto operator=(Vulkan&&) -> Vulkan& = default;
    ~Vulkan() noexcept override;

    auto render() -> void override;

private:
    struct QueueFamily
    {
        std::optional<uint32_t> index;
    };

    [[nodiscard]] static constexpr auto shouldEnableValidationLayers() noexcept -> bool
    {
        return PD_DEBUG;
    }

    [[nodiscard]] static auto getRequiredExtensions() -> std::vector<const char*>;
    [[nodiscard]] static auto createDebugMessengerCreateInfo() noexcept -> vk::DebugUtilsMessengerCreateInfoEXT;
    [[nodiscard]] static auto isDeviceSuitable(vk::PhysicalDevice device) -> bool;
    [[nodiscard]] static auto findQueueFamily(vk::PhysicalDevice device) -> QueueFamily;
    [[nodiscard]] static auto areRequiredExtensionsAvailable(const std::vector<const char*>& requiredExtensions) -> bool;

    [[nodiscard]] auto pickPhysicalDevice() const -> vk::PhysicalDevice;
    [[nodiscard]] auto areValidationLayersSupported() const -> bool;
    [[nodiscard]] auto createLogicalDevice(uint32_t queueFamilyIndex) const-> vk::Device;
    [[nodiscard]] auto createInstance() -> vk::Instance;
    auto enableValidationLayers(vk::InstanceCreateInfo& createInfo) -> bool;

    inline static const vk::DebugUtilsMessengerCreateInfoEXT debugMessengerCreateInfo =
        createDebugMessengerCreateInfo();

    vk::Instance instance {};
    vk::Device device {};
    vk::PhysicalDevice physicalDevice {};

    vk::Queue graphicsQueue {};
    vk::DebugUtilsMessengerEXT debugMessenger {};
    std::vector<const char*> requiredValidationLayers;
};

}
