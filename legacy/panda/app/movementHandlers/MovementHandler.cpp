#include "MovementHandler.h"

#include <GLFW/glfw3.h>

#include <glm/common.hpp>
#include <glm/ext/vector_float3.hpp>

#include "GlfwWindow.h"
#include "inputHandlers/KeyboardHandler.h"

namespace app
{

MovementHandler::MovementHandler(const GlfwWindow& window)
    : _window {window}
{
}

auto MovementHandler::getMovement() const -> glm::vec3
{
    auto direction = glm::vec3 {};
    const auto& keyboardHandler = _window.getKeyboardHandler();

    const auto forwardButton = keyboardHandler.getKeyState(GLFW_KEY_W);
    const auto backButton = keyboardHandler.getKeyState(GLFW_KEY_S);
    const auto rightButton = keyboardHandler.getKeyState(GLFW_KEY_D);
    const auto leftButton = keyboardHandler.getKeyState(GLFW_KEY_A);
    const auto upButton = keyboardHandler.getKeyState(GLFW_KEY_SPACE);
    const auto downButton = keyboardHandler.getKeyState(GLFW_KEY_LEFT_SHIFT);

    using enum KeyboardHandler::State;

    if (forwardButton == Pressed || forwardButton == JustPressed)
    {
        direction.z += 1;
    }
    if (backButton == Pressed || backButton == JustPressed)
    {
        direction.z -= 1;
    }
    if (rightButton == Pressed || rightButton == JustPressed)
    {
        direction.x += 1;
    }
    if (leftButton == Pressed || rightButton == JustPressed)
    {
        direction.x -= 1;
    }
    if (upButton == Pressed || upButton == JustPressed)
    {
        direction.y += 1;
    }
    if (downButton == Pressed || downButton == JustPressed)
    {
        direction.y -= 1;
    }

    return glm::clamp(direction, -1.F, 1.F);
}

}
