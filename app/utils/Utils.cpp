#include "Utils.h"

namespace app::utils
{

auto getNamesFromScene(const panda::gfx::vulkan::Scene& scene) -> std::vector<std::string>
{
    const auto objectNames = scene.objects | std::ranges::views::transform(&panda::gfx::vulkan::Object::getName);
    const auto directionalLightNames =
        scene.lights.directionalLights | std::ranges::views::transform(&panda::gfx::BaseLight::name);
    const auto pointLightNames = scene.lights.pointLights | std::ranges::views::transform(&panda::gfx::BaseLight::name);
    const auto spotLightNames = scene.lights.spotLights | std::ranges::views::transform(&panda::gfx::BaseLight::name);

    const auto size =
        objectNames.size() + directionalLightNames.size() + pointLightNames.size() + spotLightNames.size();
    auto result = std::vector<std::string> {};
    result.reserve(size);

    std::ranges::copy(objectNames, std::back_inserter(result));
    std::ranges::copy(directionalLightNames, std::back_inserter(result));
    std::ranges::copy(pointLightNames, std::back_inserter(result));
    std::ranges::copy(spotLightNames, std::back_inserter(result));

    return result;
}

auto getUniqueName(const std::string& name, const std::vector<std::string>& names) -> std::string
{
    const auto it = std::ranges::find(names, name);

    if (it == std::ranges::end(names))
    {
        return name;
    }

    const auto prefix = name + '#';
    auto maxNumber = uint32_t {1};
    for (const auto& currentName : names)
    {
        const auto nameView = std::string_view {currentName};
        const auto position = nameView.find(prefix);
        if (position != std::string::npos)
        {
            const auto number = nameView.substr(position + prefix.size());
            const auto numberValue = utils::toNumber<uint32_t>(number);
            if (numberValue.has_value())
            {
                maxNumber = std::max(maxNumber, numberValue.value() + 1);
            }
        }
    }
    return prefix + utils::toString(maxNumber);
}

}
