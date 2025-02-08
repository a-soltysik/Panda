#pragma once

#include <glm/ext/vector_float3.hpp>

namespace app
{

class GlfwWindow;

class MovementHandler
{
public:
    explicit MovementHandler(const GlfwWindow& window);
    [[nodiscard]] auto getMovement() const -> glm::vec3;

private:
    const GlfwWindow& _window;
};

}
