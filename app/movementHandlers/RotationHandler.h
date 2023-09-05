#pragma once

#include "GlfwWindow.h"

namespace app
{

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
