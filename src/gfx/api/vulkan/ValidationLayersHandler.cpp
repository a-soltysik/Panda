#include "ValidationLayersHandler.h"

#include <algorithm>

namespace panda::gfx::vk
{

void ValidationLayersHandler::add(std::string_view validationLayer) {
    validationLayers.push_back(validationLayer.data());
}

auto ValidationLayersHandler::areValidationLayersSupported() const -> bool
{
    auto layerCount = uint32_t {};
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

    auto availableLayers = std::vector<VkLayerProperties>(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    for (const auto* layerName : validationLayers)
    {
        const auto it = std::ranges::find_if(availableLayers, [layerName](const auto* availableLayer) {
            return std::string_view{layerName} == std::string_view{availableLayer};
        }, &VkLayerProperties::layerName);

        if (it == availableLayers.cend())
        {
            return false;
        }
    }

    return true;
}

auto ValidationLayersHandler::getData() const noexcept -> const char* const*
{
    return validationLayers.data();
}

auto ValidationLayersHandler::getCount() const noexcept -> size_t
{
    return validationLayers.size();
}

}