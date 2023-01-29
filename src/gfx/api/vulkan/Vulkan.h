#pragma once

#include "ValidationLayersHandler.h"
#include "gfx/api/Api.h"

namespace panda::gfx::vk
{

class Vulkan : public Api
{
public:
    ~Vulkan() noexcept override;
    auto init() -> bool override;
    auto cleanup() -> void override;

private:
    auto destroy() const -> void;

    auto createInstance() -> bool;
    auto setupDebugMessenger() -> bool;
    auto enableValidationLayers(VkInstanceCreateInfo& createInfo) -> bool;

    auto createDebugUtilsMessengerExt(const VkDebugUtilsMessengerCreateInfoEXT& createInfo,
                                      VkDebugUtilsMessengerEXT& debugMessenger) const -> VkResult;
    auto destroyDebugMessenger(const VkDebugUtilsMessengerEXT& debugMessenger) const -> void;
    static auto getRequiredExtensions() -> std::vector<const char*>;
    static auto getAvailableExtensions() -> std::vector<VkExtensionProperties>;
    static auto checkRequiredExtensions(const std::vector<const char*>& requiredExtensions) -> void;

    static auto createDebugMessengerCreateInfo() -> VkDebugUtilsMessengerCreateInfoEXT;

    static constexpr auto shouldEnableValidationLayers() -> bool
    {
#if defined(NDEBUG)
        return false;
#else
        return true;
#endif
    }

    inline static const VkDebugUtilsMessengerCreateInfoEXT debugMessengerCreateInfo = createDebugMessengerCreateInfo();
    VkInstance instance {};

    VkDebugUtilsMessengerEXT debugMessenger;
    ValidationLayersHandler validationLayersHandler;
    bool isInitialized = false;
};

}
