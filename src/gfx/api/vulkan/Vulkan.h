#pragma once

#include <app/Window.h>
#include <gfx/api/RenderingApi.h>

#include <optional>
#include <span>
#include <unordered_set>

#include "Device.h"
#include "Model.h"
#include "Object.h"
#include "RenderSystem.h"
#include "Renderer.h"
#include "gfx/Camera.h"
#include "internal/config.h"

namespace panda::gfx::vulkan
{

class Vulkan : public RenderingApi
{
public:
    explicit Vulkan(const Window& window);
    PD_DELETE_ALL(Vulkan);
    ~Vulkan() noexcept override;

    static constexpr auto maxFramesInFlight = size_t {2};

    auto makeFrame() -> void override;

private:
    struct InstanceDeleter
    {
        auto operator()(vk::Instance* instance) const noexcept -> void;
        const vk::SurfaceKHR& surface;
    };

    [[nodiscard]] static constexpr auto shouldEnableValidationLayers() noexcept -> bool
    {
        return config::isDebug;
    }

    [[nodiscard]] static auto getRequiredExtensions() -> std::vector<const char*>;
    [[nodiscard]] static auto createDebugMessengerCreateInfo() noexcept -> vk::DebugUtilsMessengerCreateInfoEXT;
    [[nodiscard]] static auto areRequiredExtensionsAvailable(std::span<const char* const> requiredExtensions) -> bool;

    [[nodiscard]] auto areValidationLayersSupported() const -> bool;
    [[nodiscard]] auto createInstance() -> std::unique_ptr<vk::Instance, InstanceDeleter>;
    [[nodiscard]] auto createSurface(const Window& window) -> vk::SurfaceKHR;

    auto enableValidationLayers(vk::InstanceCreateInfo& createInfo) -> bool;

    static constexpr auto requiredDeviceExtensions = std::array {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    inline static const vk::DebugUtilsMessengerCreateInfoEXT debugMessengerCreateInfo =
        createDebugMessengerCreateInfo();

    vk::SurfaceKHR _surface {};
    std::vector<const char*> _requiredValidationLayers;
    std::unique_ptr<vk::Instance, InstanceDeleter> _instance;
    std::unique_ptr<Device> _device;
    std::unique_ptr<Renderer> _renderer;
    std::unique_ptr<RenderSystem> _renderSystem;
    vk::DebugUtilsMessengerEXT _debugMessenger {};

    std::unique_ptr<Model> _model;
    std::vector<Object> _objects;
    Camera _camera;
};

}
