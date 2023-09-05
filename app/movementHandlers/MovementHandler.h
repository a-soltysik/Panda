#pragma once

#include "GlfwWindow.h"

namespace app
{

class MovementHandler
{
public:
    explicit MovementHandler(const GlfwWindow& window);
    [[nodiscard]] auto getMovement() const -> glm::vec3;

private:
    const GlfwWindow& _window;
};

}
