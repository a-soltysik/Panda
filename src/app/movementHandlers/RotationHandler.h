#pragma once

#include "app/Window.h"

namespace panda::app
{

class RotationHandler
{
public:
    [[nodiscard]] static auto instance(const Window& window) -> const RotationHandler&;
    [[nodiscard]] auto getRotation() const -> glm::vec2;

private:
    explicit RotationHandler(const Window& window);

    [[nodiscard]] auto getPixelsToAngleRatio() const -> glm::vec2;

    const Window& _window;
};

}

