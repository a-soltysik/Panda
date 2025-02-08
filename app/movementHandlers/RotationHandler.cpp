#include "RotationHandler.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/gtc/constants.hpp>

#include "GlfwWindow.h"
#include "inputHandlers/MouseHandler.h"

namespace app
{

RotationHandler::RotationHandler(const GlfwWindow& window)
    : _window {window}
{
}

auto RotationHandler::getRotation() const -> glm::vec2
{
    const auto delta = _window.getMouseHandler().getCursorDeltaPosition();
    const auto ratio = getPixelsToAngleRatio();

    return {delta.y * ratio.y, delta.x * ratio.x};
}

auto RotationHandler::getPixelsToAngleRatio() const -> glm::vec2
{
    const auto widowSize = _window.getSize();
    return {glm::two_pi<float>() / static_cast<float>(widowSize.x),
            glm::two_pi<float>() / static_cast<float>(widowSize.y)};
}

}
