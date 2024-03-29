#pragma once

#include <panda/gfx/vulkan/Scene.h>

#include "panda/Window.h"

namespace app
{

class UserGui
{
public:
    explicit UserGui(const panda::Window& window);

    auto render(panda::gfx::vulkan::Scene& scene) -> void;

private:
    auto objectListBox(const std::vector<std::string>& objects) -> std::string;
    auto removeObject(panda::gfx::vulkan::Scene& scene, const std::vector<std::string>& objects) -> void;
    auto addLight(panda::gfx::vulkan::Scene& scene, const std::vector<std::string>& objects) -> void;
    static auto objectInfo(panda::gfx::vulkan::Scene& scene, const std::string& name) -> void;

    static auto vulkanObject(panda::gfx::vulkan::Object& object) -> void;
    static auto attenuation(panda::gfx::Attenuation& attenuation) -> void;
    static auto baseLight(panda::gfx::BaseLight& light) -> void;
    static auto directionalLight(panda::gfx::DirectionalLight& light) -> void;
    static auto pointLight(panda::gfx::PointLight& light) -> void;
    static auto spotLight(panda::gfx::SpotLight& light) -> void;

    const panda::Window& _window;
    int _currentObject = 0;
    int _currentLight = 0;
};

}
