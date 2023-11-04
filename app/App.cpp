#include "App.h"

#include <csignal>

#include "gui/GuiManager.h"
#include "internal/config.h"
#include "movementHandlers/MovementHandler.h"
#include "movementHandlers/RotationHandler.h"
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
    panda::log::Config::instance().file.terminate();
    std::_Exit(signalValue);
}

void processCamera(float deltaTime,
                   const app::GlfwWindow& window,
                   panda::gfx::vulkan::Object& cameraObject,
                   panda::gfx::Camera& camera)
{
    static constexpr auto rotationVelocity = 500.F;
    static constexpr auto moveVelocity = 2.5F;

    if (window.getMouseHandler().getButtonState(GLFW_MOUSE_BUTTON_LEFT) == app::MouseHandler::ButtonState::Pressed)
    {
        cameraObject.transform.rotation +=
            glm::vec3 {app::RotationHandler {window}.getRotation() * rotationVelocity * deltaTime, 0};
    }

    cameraObject.transform.rotation.x =
        glm::clamp(cameraObject.transform.rotation.x, -glm::half_pi<float>(), glm::half_pi<float>());
    cameraObject.transform.rotation.y = glm::mod(cameraObject.transform.rotation.y, glm::two_pi<float>());

    const auto rawMovement = app::MovementHandler {window}.getMovement();

    const auto cameraDirection =
        glm::vec3 {glm::cos(-cameraObject.transform.rotation.x) * glm::sin(cameraObject.transform.rotation.y),
                   glm::sin(-cameraObject.transform.rotation.x),
                   glm::cos(-cameraObject.transform.rotation.x) * glm::cos(cameraObject.transform.rotation.y)};
    const auto cameraRight =
        glm::vec3 {glm::cos(cameraObject.transform.rotation.y), 0, -glm::sin(cameraObject.transform.rotation.y)};

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
        cameraObject.transform.translation += glm::normalize(translation) * moveVelocity * deltaTime;
    }

    camera.setViewYXZ(panda::gfx::view::YXZ {
        .position = cameraObject.transform.translation,
        .rotation = {-cameraObject.transform.rotation.x, cameraObject.transform.rotation.y, 0}
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
        _deltaTime = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - _time).count();
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

auto App::run() -> int
{
    initializeLogger();
    registerSignalHandlers();

    static constexpr auto defaultWidth = uint32_t {1280};
    static constexpr auto defaultHeight = uint32_t {720};
    _window = std::make_unique<GlfwWindow>(glm::uvec2 {defaultWidth, defaultHeight}, config::appName.data());
    _api = std::make_unique<panda::gfx::vulkan::Context>(*_window);

    _vaseMesh = panda::gfx::vulkan::Mesh::loadMesh(_api->getDevice(), config::resource::models / "smooth_vase.obj");
    _floorMesh = panda::gfx::vulkan::Mesh::loadMesh(_api->getDevice(), config::resource::models / "square.obj");

    mainLoop();
    return 0;
}

auto App::mainLoop() -> void
{
    auto currentTime = TimeData {};
    auto scene = panda::gfx::vulkan::Scene {};

    setObjects(scene);

    auto cameraObject = panda::gfx::vulkan::Object {"Camera"};

    cameraObject.transform.translation = {0, 2, -8};
    scene.camera.setViewYXZ(panda::gfx::view::YXZ {.position = cameraObject.transform.translation,
                                                   .rotation = cameraObject.transform.rotation});

    [[maybe_unused]] const auto gui = GuiManager {*_window};

    while (!_window->shouldClose()) [[likely]]
    {
        if (!_window->isMinimized())
        {
            panda::utils::signals::gameLoopIterationStarted.registerSender()();
            _window->processInput();

            currentTime.update();

            scene.camera.setPerspectiveProjection(
                panda::gfx::projection::Perspective {.fovY = glm::radians(50.F),
                                                     .aspect = _api->getRenderer().getAspectRatio(),
                                                     .near = 0.1F,
                                                     .far = 100});
            processCamera(currentTime.getDelta(), *_window, cameraObject, scene.camera);

            _api->makeFrame(currentTime.getDelta(), scene);
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
        panda::log::Config::instance().console.setLevels(std::array {Warning, Error});
        panda::log::Config::instance().console.start();
    }
    else
    {
        panda::log::Config::instance().file.setLevels(std::array {Info, Warning, Error});
    }

    panda::log::Config::instance().file.start();
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

void App::setObjects(panda::gfx::vulkan::Scene& scene)
{
    auto object = panda::gfx::vulkan::Object {"Vase_1"};
    object.mesh = _vaseMesh.get();
    object.transform.rotation = {};
    object.transform.translation = {1.F, 0.F, 0.F};
    object.transform.scale = {5.F, 5.F, 5.F};

    scene.objects.push_back(std::move(object));

    object = panda::gfx::vulkan::Object {"Vase_2"};
    object.mesh = _vaseMesh.get();
    object.transform.rotation = {};
    object.transform.translation = {-1.F, 0.F, 0.F};
    object.transform.scale = {5.F, 5.F, 5.F};

    scene.objects.push_back(std::move(object));

    object = panda::gfx::vulkan::Object {"Floor"};
    object.mesh = _floorMesh.get();
    object.transform.rotation = {};
    object.transform.translation = {0.F, 0.F, 0.F};
    object.transform.scale = {10.F, 10.F, 10.F};

    scene.objects.push_back(std::move(object));

    scene.lights.pointLights.push_back(panda::gfx::PointLight {
        panda::gfx::makeColorLight("Light_1", {1.F, 0.F,   0.F   },
         0.F, 0.8F, 1.F, 1.F),
        {2.F, -2.F,  -1.5F },
        {1.F, 0.05F, 0.005F}
    });

    scene.lights.spotLights.push_back(panda::gfx::SpotLight {
        {panda::gfx::makeColorLight("SpotLight", {0.F, 1.F, 0.F}, 0.0F, 0.8F, 1.F, 1.F),
         {0.F, -5.F, 0.F},
         {1.F, 0.05F, 0.005F}},
        {0.F, 1.F, 0.F},
        glm::cos(glm::radians(30.F))
    });

    scene.lights.pointLights.push_back(panda::gfx::PointLight {
        panda::gfx::makeColorLight("Light_2", {0.F,  0.F,   1.F   },
         0.F, 0.8F, 1.F, 1.F),
        {-2.F, -2.F,  -1.5F },
        {1.F,  0.05F, 0.005F}
    });

    scene.lights.directionalLights.push_back(panda::gfx::DirectionalLight {
        panda::gfx::makeColorLight("DirectionalLight", {1.F, .8F,  .8F },
         0.F, 0.8F, 1.F, 0.02F),
        {0.F, -2.F, 10.F},
    });
}

}
