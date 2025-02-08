#pragma once

// clang-format off
#include "panda/utils/Assert.h"
// clang-format on

#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <vector>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_handles.hpp>

#include "panda/Common.h"
#include "panda/Window.h"
#include "panda/gfx/vulkan/Buffer.h"
#include "panda/gfx/vulkan/Descriptor.h"
#include "panda/gfx/vulkan/Device.h"
#include "panda/gfx/vulkan/Renderer.h"
#include "panda/gfx/vulkan/Scene.h"
#include "panda/gfx/vulkan/object/Mesh.h"
#include "panda/gfx/vulkan/object/Texture.h"
#include "panda/gfx/vulkan/systems/LightSystem.h"
#include "panda/gfx/vulkan/systems/RenderSystem.h"
#include "panda/internal/config.h"
#include "systems/InstancedRenderSystem.h"

namespace panda::gfx::vulkan
{

class Context
{
public:
    explicit Context(const Window& window,
                     const std::optional<size_t>& instancedObjectsCount = std::nullopt,
                     bool useSingleRendering = true);
    PD_DELETE_ALL(Context);
    ~Context() noexcept;

    static constexpr auto maxFramesInFlight = size_t {2};

    auto makeFrame(float deltaTime, Scene& scene) const -> void;
    [[nodiscard]] auto getDevice() const noexcept -> const Device&;
    [[nodiscard]] auto getRenderer() const noexcept -> const Renderer&;
    auto registerTexture(std::unique_ptr<Texture> texture) -> void;
    auto registerMesh(std::unique_ptr<Mesh> mesh) -> void;

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

    [[nodiscard]] static auto getRequiredExtensions(const Window& window) -> std::vector<const char*>;
    [[nodiscard]] auto createInstance(const Window& window) -> std::unique_ptr<vk::Instance, InstanceDeleter>;
    [[nodiscard]] static auto createDebugMessengerCreateInfo() noexcept -> vk::DebugUtilsMessengerCreateInfoEXT;
    [[nodiscard]] static auto areRequiredExtensionsAvailable(std::span<const char* const> requiredExtensions) -> bool;

    [[nodiscard]] auto areValidationLayersSupported() const -> bool;

    auto enableValidationLayers(vk::InstanceCreateInfo& createInfo) -> bool;
    auto initializeImGui() -> void;

    static constexpr auto requiredDeviceExtensions =
        std::array {vk::KHRSwapchainExtensionName, vk::KHRPushDescriptorExtensionName};
    inline static const vk::DebugUtilsMessengerCreateInfoEXT debugMessengerCreateInfo =
        createDebugMessengerCreateInfo();

    vk::SurfaceKHR _surface;
    std::vector<const char*> _requiredValidationLayers;
    std::unique_ptr<vk::Instance, InstanceDeleter> _instance;
    std::unique_ptr<Device> _device;
    std::unique_ptr<Renderer> _renderer;
    std::unique_ptr<RenderSystem> _renderSystem;
    std::unique_ptr<InstancedRenderSystem> _instancedRenderSystem;
    std::unique_ptr<LightSystem> _pointLightSystem;
    vk::DebugUtilsMessengerEXT _debugMessenger;
    std::vector<std::unique_ptr<Texture>> _textures;
    std::vector<std::unique_ptr<Mesh>> _meshes;
    std::vector<std::unique_ptr<Buffer>> _uboFragBuffers;
    std::vector<std::unique_ptr<Buffer>> _uboVertBuffers;
    std::unique_ptr<DescriptorPool> _guiPool;

    const Window& _window;
};

}
