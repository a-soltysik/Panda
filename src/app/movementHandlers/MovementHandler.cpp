#include "MovementHandler.h"

#include "app/inputHandlers/KeyboardHandler.h"

namespace panda::app
{

MovementHandler::MovementHandler(const Window& window)
    : _window {window}
{
}

auto MovementHandler::instance(const Window& window) -> const MovementHandler&
{
    static const MovementHandler movementHandler {window};

    expect(movementHandler._window.getHandle(),
           window.getHandle(),
           fmt::format("Can't register for window [{}]. MovementHandler is already registered for window [{}]",
                       static_cast<void*>(window.getHandle()),
                       static_cast<void*>(movementHandler._window.getHandle())));

    return movementHandler;
}

auto MovementHandler::getMovement() const -> glm::vec3
{
    auto direction = glm::vec3 {};
    const auto& keyboardHandler = KeyboardHandler::instance(_window);

    const auto forwardButton = keyboardHandler.getKeyState(GLFW_KEY_W);
    const auto backButton = keyboardHandler.getKeyState(GLFW_KEY_S);
    const auto rightButton = keyboardHandler.getKeyState(GLFW_KEY_D);
    const auto leftButton = keyboardHandler.getKeyState(GLFW_KEY_A);
    const auto upButton = keyboardHandler.getKeyState(GLFW_KEY_SPACE);
    const auto downButton = keyboardHandler.getKeyState(GLFW_KEY_LEFT_SHIFT);

    using enum KeyboardHandler::State;

    if (forwardButton == Pressed || forwardButton == JustPressed)
    {
        direction.z += 1.f;
    }
    if (backButton == Pressed || backButton == JustPressed)
    {
        direction.z -= 1.f;
    }
    if (rightButton == Pressed || rightButton == JustPressed)
    {
        direction.x += 1.f;
    }
    if (leftButton == Pressed || rightButton == JustPressed)
    {
        direction.x -= 1.f;
    }
    if (upButton == Pressed || upButton == JustPressed)
    {
        direction.y += 1.f;
    }
    if (downButton == Pressed || downButton == JustPressed)
    {
        direction.y -= 1.f;
    }

    return glm::clamp(direction, -1.f, 1.f);
}

}
