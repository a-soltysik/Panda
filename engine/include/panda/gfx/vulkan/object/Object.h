#pragma once

#include <cstddef>
#include <filesystem>
#include <glm/ext/vector_float3.hpp>
#include <string>
#include <vector>

#include "Surface.h"
#include "panda/Common.h"

namespace panda::gfx::vulkan
{

struct Transform
{
    glm::vec3 translation {};
    glm::vec3 scale {1.F, 1.F, 1.F};
    glm::vec3 rotation {};
};

class Scene;
class Context;

class Object
{
public:
    using Id = size_t;

    static auto loadSurfaces(Context& context, const std::filesystem::path& path, bool shouldBeInstanced = false)
        -> std::vector<Surface>;

    Object(std::string name, Scene& scene);
    PD_DELETE_ALL(Object);
    ~Object() noexcept = default;

    [[nodiscard]] auto getId() const noexcept -> Id;
    [[nodiscard]] auto getName() const noexcept -> const std::string&;
    auto addSurface(const Surface& surface) -> void;
    [[nodiscard]] auto getSurfaces() const noexcept -> std::vector<Surface>;

    Transform transform;

private:
    inline static Id currentId = 0;
    std::vector<Surface> surfaces;
    std::string _name;
    Scene& _scene;
    Id _id;
};

}
