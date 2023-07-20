#pragma once

#include "utils/Utils.h"

namespace panda::gfx::vulkan
{

struct Vertex
{
    glm::vec3 position;
    glm::vec3 color;
    glm::vec3 normal;
    glm::vec2 uv;

    constexpr auto operator==(const Vertex& rhs) const noexcept -> bool = default;

    static constexpr auto getBindingDescription() -> vk::VertexInputBindingDescription
    {
        return vk::VertexInputBindingDescription {0, sizeof(Vertex), vk::VertexInputRate::eVertex};
    }

    static constexpr auto getAttributeDescriptions() -> std::array<vk::VertexInputAttributeDescription, 4>
    {
        return {
            vk::VertexInputAttributeDescription {0, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, position)},
            vk::VertexInputAttributeDescription {1, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, color)   },
            vk::VertexInputAttributeDescription {2, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, normal)  },
            vk::VertexInputAttributeDescription {3, 0, vk::Format::eR32G32Sfloat,    offsetof(Vertex, uv)      }
        };
    }
};

}

namespace std
{

template <>
struct hash<panda::gfx::vulkan::Vertex> {
    auto operator()(panda::gfx::vulkan::Vertex const &vertex) const -> size_t {
        size_t seed = 0;
        panda::utils::hashCombine(seed, vertex.position, vertex.color, vertex.normal, vertex.uv);
        return seed;
    }
};

}