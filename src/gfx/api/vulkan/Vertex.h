#pragma once

#include <glm/glm.hpp>

namespace panda::gfx::vulkan
{

struct Vertex
{
    glm::vec2 position;
    glm::vec3 color;

    static constexpr auto getBindingDescription() -> vk::VertexInputBindingDescription
    {
        return vk::VertexInputBindingDescription {0, sizeof(Vertex), vk::VertexInputRate::eVertex};
    }

    static constexpr auto getAttributeDescriptions() -> std::array<vk::VertexInputAttributeDescription, 2>
    {
        return {
            vk::VertexInputAttributeDescription {0, 0, vk::Format::eR32G32Sfloat,    offsetof(Vertex, position)},
            vk::VertexInputAttributeDescription {1, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, color)   }
        };
    }
};

}