#pragma once

#include "app/Window.h"

namespace panda::app
{

class MovementHandler
{
public:
    [[nodiscard]] static auto instance(const Window& window) -> const MovementHandler&;
    [[nodiscard]] auto getMovement() const -> glm::vec3;

private:
    explicit MovementHandler(const Window& window);

    const Window& _window;
};

}
