#include "App.h"

#include <GLFW/glfw3.h>
#include <panda/Logger.h>
#include <panda/gfx/Camera.h>
#include <panda/gfx/Light.h>
#include <panda/gfx/vulkan/Context.h>
#include <panda/gfx/vulkan/Renderer.h>
#include <panda/gfx/vulkan/Scene.h>
#include <panda/gfx/vulkan/object/Object.h>
#include <panda/utils/Assert.h>
#include <panda/utils/Signal.h>
#include <panda/utils/Signals.h>

#include <array>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <glm/common.hpp>
#include <glm/ext/scalar_constants.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_uint2.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/trigonometric.hpp>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

#include "gui/GuiManager.h"
#include "inputHandlers/MouseHandler.h"
#include "internal/config.h"
#include "movementHandlers/MovementHandler.h"
#include "movementHandlers/RotationHandler.h"
#include "utils/Signals.h"
#include "utils/Utils.h"

namespace
{

[[nodiscard]] constexpr auto getSignalName(int signalValue) noexcept -> std::string_view
{
    switch (signalValue)
    {
    case SIGABRT:
        return "SIGABRT";
    case SIGFPE:
        return "SIGFPE";
    case SIGILL:
        return "SIGILL";
    case SIGINT:
        return "SIGINT";
    case SIGSEGV:
        return "SIGSEGV";
    case SIGTERM:
        return "SIGTERM";
    default:
        return "unknown";
    }
}

[[noreturn]] auto signalHandler(int signalValue) -> void
{
    panda::log::Error("Received {} signal", getSignalName(signalValue));
    panda::log::FileLogger::instance().terminate();
    std::_Exit(signalValue);
}

void processCamera(float deltaTime,
                   const app::GlfwWindow& window,
                   panda::gfx::vulkan::Transform& cameraObject,
                   panda::gfx::Camera& camera)
{
    static constexpr auto rotationVelocity = 500.F;
    static constexpr auto moveVelocity = 2.5F;

    if (window.getMouseHandler().getButtonState(GLFW_MOUSE_BUTTON_LEFT) == app::MouseHandler::ButtonState::Pressed)
    {
        cameraObject.rotation +=
            glm::vec3 {app::RotationHandler {window}.getRotation() * rotationVelocity * deltaTime, 0};
    }

    cameraObject.rotation.x = glm::clamp(cameraObject.rotation.x, -glm::half_pi<float>(), glm::half_pi<float>());
    cameraObject.rotation.y = glm::mod(cameraObject.rotation.y, glm::two_pi<float>());

    const auto rawMovement = app::MovementHandler {window}.getMovement();

    const auto cameraDirection = glm::vec3 {glm::cos(-cameraObject.rotation.x) * glm::sin(cameraObject.rotation.y),
                                            glm::sin(-cameraObject.rotation.x),
                                            glm::cos(-cameraObject.rotation.x) * glm::cos(cameraObject.rotation.y)};
    const auto cameraRight = glm::vec3 {glm::cos(cameraObject.rotation.y), 0, -glm::sin(cameraObject.rotation.y)};

    auto translation = glm::vec3 {};
    if (!app::utils::isZero(rawMovement.z))
    {
        translation += cameraDirection * rawMovement.z;
    }
    if (!app::utils::isZero(rawMovement.x))
    {
        translation += cameraRight * rawMovement.x;
    }
    if (!app::utils::isZero(rawMovement.y))
    {
        translation.y = rawMovement.y;
    }

    if (glm::dot(translation, translation) > std::numeric_limits<float>::epsilon())
    {
        cameraObject.translation += glm::normalize(translation) * moveVelocity * deltaTime;
    }

    camera.setViewYXZ(panda::gfx::view::YXZ {
        .position = cameraObject.translation,
        .rotation = {-cameraObject.rotation.x, cameraObject.rotation.y, 0}
    });
}

class TimeData
{
public:
    TimeData()
        : _time {std::chrono::steady_clock::now()}
    {
    }

    auto update() -> void
    {
        const auto currentTime = std::chrono::steady_clock::now();
        _deltaTime = std::chrono::duration<float>(currentTime - _time).count();
        _time = currentTime;
    }

    [[nodiscard]] auto getDelta() const noexcept -> float
    {
        return _deltaTime;
    }

private:
    std::chrono::steady_clock::time_point _time;
    float _deltaTime {};
};
}

namespace app
{

App::App()
{
    _newMeshAddedReceiver = utils::signals::newMeshAdded.connect([this](auto newMeshAddedData) {
        if (_window->getId() != newMeshAddedData.id)
        {
            return;
        }

        auto& object = _scene.addObject(newMeshAddedData.fileName,
                                        {panda::gfx::vulkan::Object::loadSurfaces(*_api, newMeshAddedData.fileName)});
        if (object.getSurfaces().empty())
        {
            panda::log::Warning("Failed to load a model from file: {}", newMeshAddedData.fileName);
            _scene.removeObjectByName(object.getName());
        }
    });
}

auto App::run() -> int
{
    initializeLogger();
    registerSignalHandlers();

    static constexpr auto defaultWidth = uint32_t {1920};
    static constexpr auto defaultHeight = uint32_t {1080};
    _window = std::make_unique<GlfwWindow>(glm::uvec2 {defaultWidth, defaultHeight}, config::appName.data());
    _api = std::make_unique<panda::gfx::vulkan::Context>(*_window, 10);

    setDefaultScene();
    mainLoop();
    return 0;
}

auto App::mainLoop() -> void
{
    auto currentTime = TimeData {};

    auto cameraObject = panda::gfx::vulkan::Transform {};

    cameraObject.translation = {0, 0.5, -5};
    _scene.getCamera().setViewYXZ(
        panda::gfx::view::YXZ {.position = cameraObject.translation, .rotation = cameraObject.rotation});

    [[maybe_unused]] const auto gui = GuiManager {*_window};

    while (!_window->shouldClose()) [[likely]]
    {
        if (!_window->isMinimized())
        {
            panda::utils::signals::gameLoopIterationStarted.registerSender()();
            _window->processInput();

            currentTime.update();

            _scene.getCamera().setPerspectiveProjection(
                panda::gfx::projection::Perspective {.fovY = glm::radians(50.F),
                                                     .aspect = _api->getRenderer().getAspectRatio(),
                                                     .near = 0.1F,
                                                     .far = 100});
            processCamera(currentTime.getDelta(), *_window, cameraObject, _scene.getCamera());

            _api->makeFrame(currentTime.getDelta(), _scene);
        }
        else [[unlikely]]
        {
            _window->waitForInput();
        }
    }
}

auto App::initializeLogger() -> void
{
    using enum panda::log::Level;

    if constexpr (config::isDebug)
    {
        panda::log::FileLogger::instance().setLevels(std::array {Debug, Info, Warning, Error});
    }
    else
    {
        panda::log::FileLogger::instance().setLevels(std::array {Info, Warning, Error});
    }

    panda::log::FileLogger::instance().start();
}

auto App::registerSignalHandlers() -> void
{
    panda::shouldNotBe(std::signal(SIGABRT, signalHandler), SIG_ERR, "Failed to register signal handler");
    panda::shouldNotBe(std::signal(SIGFPE, signalHandler), SIG_ERR, "Failed to register signal handler");
    panda::shouldNotBe(std::signal(SIGILL, signalHandler), SIG_ERR, "Failed to register signal handler");
    panda::shouldNotBe(std::signal(SIGINT, signalHandler), SIG_ERR, "Failed to register signal handler");
    panda::shouldNotBe(std::signal(SIGSEGV, signalHandler), SIG_ERR, "Failed to register signal handler");
    panda::shouldNotBe(std::signal(SIGTERM, signalHandler), SIG_ERR, "Failed to register signal handler");
}

void App::setDefaultScene()
{
    const auto f1Mesh =
        panda::gfx::vulkan::Object::loadSurfaces(*_api, config::resource::models / "formula_1.obj", true);

    const auto roadMesh = panda::gfx::vulkan::Object::loadSurfaces(*_api, config::resource::models / "road.obj");

    auto& road = _scene.addObject("Road", {roadMesh});
    road.transform.translation = {};
    road.transform.rotation = {glm::pi<float>(), -glm::quarter_pi<float>(), 0};
    road.transform.scale = {1.F, 1.F, 1.F};

    auto& f1Car = _scene.addObject("F1", {f1Mesh});
    f1Car.transform.rotation = {0, glm::quarter_pi<float>() + glm::half_pi<float>(), 0};
    f1Car.transform.translation = {0, 0.3F, 0};
    f1Car.transform.scale = {0.01, 0.01, 0.01};

    auto spotLight = _scene.addLight<panda::gfx::SpotLight>("SpotLight");
    if (spotLight.has_value())
    {
        spotLight->get().makeColorLight({1.F, 1.F, 1.F}, 0.0F, 0.8F, 1.F, 1.F);
        spotLight->get().position = {0.F, -5.F, 0.F};
        spotLight->get().attenuation = {.constant = 1.F, .linear = 0.05F, .exp = 0.005F};
        spotLight->get().direction = {0.F, 1.F, 0.F};
        spotLight->get().cutOff = glm::cos(glm::radians(30.F));
    }

    auto directionalLight = _scene.addLight<panda::gfx::DirectionalLight>("DirectionalLight");
    if (directionalLight.has_value())
    {
        directionalLight->get().makeColorLight({1.F, .8F, .8F}, 0.F, 0.8F, 1.F, 0.1F);
        directionalLight->get().direction = {-6.2F, -2.F, 0};
    }
}
}
