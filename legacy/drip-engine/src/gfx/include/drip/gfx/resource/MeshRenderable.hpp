#pragma once

// clang-format off
#include <drip/common/utils/Assert.hpp> // NOLINT(misc-include-cleaner)
// clang-format on

#include <cstddef>
#include <glm/ext/vector_float3.hpp>
#include <string>
#include <vector>

#include "Renderable.hpp"
#include "Surface.hpp"

namespace drip::gfx
{

struct Transform
{
    glm::vec3 translation {};
    glm::vec3 scale {1.F, 1.F, 1.F};
    glm::vec3 rotation {};
};

class Scene;
class Context;

class MeshRenderable : public Renderable
{
public:
    explicit MeshRenderable(std::string name);

    [[nodiscard]] auto getName() const -> std::string_view override;
    [[nodiscard]] auto getType() const -> Type override;
    void addSurface(const Surface& surface);
    [[nodiscard]] auto getSurfaces() const noexcept -> const std::vector<Surface>&;

    Transform transform;

private:
    std::vector<Surface> surfaces;
    std::string _name;
};

}
