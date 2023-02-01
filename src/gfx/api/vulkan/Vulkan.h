#pragma once

#include <optional>

#include "ValidationLayersHandler.h"
#include "gfx/api/Api.h"

namespace panda::gfx::vk
{

class Vulkan : public Api
{
public:
    Vulkan() = default;
    Vulkan(const Vulkan&) = delete;
    Vulkan(Vulkan&&) = default;
    auto operator=(const Vulkan&) -> Vulkan& = delete;
    auto operator=(Vulkan&&) -> Vulkan& = default;
    ~Vulkan() noexcept override;

    auto init() -> bool override;
    auto cleanup() -> void override;

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
    [[nodiscard]] static auto getAvailableExtensions() -> std::vector<VkExtensionProperties>;
    [[nodiscard]] static auto createDebugMessengerCreateInfo() noexcept -> VkDebugUtilsMessengerCreateInfoEXT;
    [[nodiscard]] static auto isDeviceSuitable(VkPhysicalDevice device) -> bool;
    [[nodiscard]] static auto findQueueFamily(VkPhysicalDevice device) -> QueueFamily;
    static auto checkRequiredExtensions(const std::vector<const char*>& requiredExtensions) -> void;

    [[nodiscard]] auto pickPhysicalDevice() const -> VkPhysicalDevice;
    auto createLogicalDevice() -> bool;
    auto destroy() -> void;
    auto createInstance() -> bool;
    auto setupDebugMessenger() -> bool;
    auto enableValidationLayers(VkInstanceCreateInfo& createInfo) -> bool;
    auto createDebugUtilsMessengerExt(const VkDebugUtilsMessengerCreateInfoEXT& createInfo) -> VkResult;
    auto destroyDebugMessenger() const -> void;

    inline static const VkDebugUtilsMessengerCreateInfoEXT debugMessengerCreateInfo = createDebugMessengerCreateInfo();

    VkInstance instance {};
    VkPhysicalDevice physicalDevice {};
    VkDevice device {};
    VkQueue graphicsQueue {};
    VkDebugUtilsMessengerEXT debugMessenger{};
    ValidationLayersHandler validationLayersHandler;
    bool isInitialized = false;
};

}
