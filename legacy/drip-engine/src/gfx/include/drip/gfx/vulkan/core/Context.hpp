#pragma once

// clang-format off
#include <drip/common/utils/Assert.hpp> // NOLINT(misc-include-cleaner)
// clang-format on

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <vector>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_handles.hpp>

#include "drip/gfx/Config.hpp"
#include "drip/gfx/gui/GuiManager.hpp"

namespace drip::gfx
{

class Window;
class Buffer;
class DescriptorPool;
class Device;
class Renderer;
class Scene;
class Mesh;
class Texture;
class RenderSystem;

class Context
{
public:
    explicit Context(const Window& window);
    Context(const Context&) = delete;
    auto operator=(const Context&) = delete;
    Context(Context&&) = delete;
    auto operator=(Context&&) = delete;
    ~Context() noexcept;

    static constexpr auto maxFramesInFlight = size_t {2};

    void makeFrame(const Scene& scene) const;
    [[nodiscard]] auto getDevice() const noexcept -> const Device&;
    void registerTexture(std::unique_ptr<Texture> texture);
    void registerMesh(std::unique_ptr<Mesh> mesh);
    [[nodiscard]] auto getAspectRatio() const noexcept -> float;
    auto getGuiManager() -> GuiManager&;

private:
    struct InstanceDeleter
    {
        void operator()(vk::Instance* instance) const noexcept;
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
    [[nodiscard]] static auto createDebugMessanger(vk::Instance instance) -> std::optional<vk::DebugUtilsMessengerEXT>;

    [[nodiscard]] auto areValidationLayersSupported() const -> bool;

    auto enableValidationLayers(vk::InstanceCreateInfo& createInfo) -> bool;
    void initializeImGui();
    void setupRenderSystems();

    inline static const vk::DebugUtilsMessengerCreateInfoEXT debugMessengerCreateInfo =
        createDebugMessengerCreateInfo();

    vk::SurfaceKHR _surface;
    std::vector<const char*> _requiredValidationLayers;
    std::unique_ptr<vk::Instance, InstanceDeleter> _instance;
    std::unique_ptr<Device> _device;
    std::unique_ptr<Renderer> _renderer;
    std::vector<std::unique_ptr<RenderSystem>> _renderSystems;
    std::optional<vk::DebugUtilsMessengerEXT> _debugMessenger;
    std::vector<std::unique_ptr<Texture>> _textures;
    std::vector<std::unique_ptr<Mesh>> _meshes;
    std::vector<std::unique_ptr<Buffer>> _uboFragBuffers;
    std::vector<std::unique_ptr<Buffer>> _uboVertBuffers;
    std::unique_ptr<DescriptorPool> _guiPool;
    GuiManager _guiManager;

    const Window& _window;
};

}
