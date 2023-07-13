#include "RotationHandler.h"

#include <glm/ext/scalar_constants.hpp>

#include "app/inputHandlers/MouseHandler.h"

namespace panda::app
{

RotationHandler::RotationHandler(const Window& window)
    : _window {window}
{
}

auto RotationHandler::instance(const Window& window) -> const RotationHandler&
{
    static const auto rotationHandler = RotationHandler {window};

    expect(rotationHandler._window.getHandle(),
           window.getHandle(),
           fmt::format("Can't register for window [{}]. RotationHandler is already registered for window [{}]",
                       static_cast<void*>(window.getHandle()),
                       static_cast<void*>(rotationHandler._window.getHandle())));

    return rotationHandler;
}

auto RotationHandler::getRotation() const -> glm::vec2
{
    const auto delta = MouseHandler::instance(_window).getCursorDeltaPosition();
    const auto ratio = getPixelsToAngleRatio();

    return {delta.y * ratio.y, delta.x * ratio.x};
}

auto RotationHandler::getPixelsToAngleRatio() const -> glm::vec2
{
    const auto widowSize = _window.getSize();
    return {2.f * glm::pi<float>() / static_cast<float>(widowSize.x),
            2.f * glm::pi<float>() / static_cast<float>(widowSize.y)};
}

}
