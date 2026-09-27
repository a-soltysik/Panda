#pragma once

#include <string>
#include <string_view>
#include <unordered_set>

namespace panda
{
class Window;

namespace gfx
{
struct Attenuation;
struct BaseLight;
struct DirectionalLight;
struct PointLight;
struct SpotLight;

namespace vulkan
{
class Scene;
class Object;
}
}
}

namespace app
{

class UserGui
{
public:
    explicit UserGui(const panda::Window& window);

    auto render(panda::gfx::vulkan::Scene& scene) -> void;

private:
    auto removeObject(panda::gfx::vulkan::Scene& scene) -> void;
    auto addLight(panda::gfx::vulkan::Scene& scene) -> void;
    auto objectListBox(const std::unordered_set<std::string_view>& objects) -> std::string;

    static auto objectInfo(panda::gfx::vulkan::Scene& scene, const std::string& name) -> void;
    static auto vulkanObject(panda::gfx::vulkan::Object& object) -> void;
    static auto attenuation(panda::gfx::Attenuation& attenuation) -> void;
    static auto baseLight(panda::gfx::BaseLight& light) -> void;
    static auto directionalLight(panda::gfx::DirectionalLight& light) -> void;
    static auto pointLight(panda::gfx::PointLight& light) -> void;
    static auto spotLight(panda::gfx::SpotLight& light) -> void;

    const panda::Window& _window;
    std::string _currentObject;
    int _currentIndex = 0;
    int _currentLight = 0;
};

}
