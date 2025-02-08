#pragma once

#include <glm/ext/vector_float2.hpp>

namespace app
{

class GlfwWindow;

class RotationHandler
{
public:
    explicit RotationHandler(const GlfwWindow& window);
    [[nodiscard]] auto getRotation() const -> glm::vec2;

private:
    [[nodiscard]] auto getPixelsToAngleRatio() const -> glm::vec2;

    const GlfwWindow& _window;
};

}
