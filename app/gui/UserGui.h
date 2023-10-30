#pragma once

#include <panda/gfx/vulkan/Scene.h>

namespace app
{

class UserGui
{
public:
    auto render(panda::gfx::vulkan::Scene& scene) -> void;

private:
    auto objectListBox(const std::vector<std::string>& objects) -> std::string;
    static auto objectInfo(panda::gfx::vulkan::Scene& scene, const std::string& name) -> void;
    static auto getAllNames(const panda::gfx::vulkan::Scene& scene) -> std::vector<std::string>;

    static auto vulkanObject(panda::gfx::vulkan::Object& object) -> void;
    static auto attenuation(panda::gfx::Attenuation& attenuation) -> void;
    static auto baseLight(panda::gfx::BaseLight& light) -> void;
    static auto directionalLight(panda::gfx::DirectionalLight& light) -> void;
    static auto pointLight(panda::gfx::PointLight& light) -> void;
    static auto spotLight(panda::gfx::SpotLight& light) -> void;

    int _currentObject = 0;
};

}
