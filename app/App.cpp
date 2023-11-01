#include "App.h"

#include <csignal>

#include "backend/Profiler.h"
#include "gui/GuiManager.h"
#include "internal/config.h"
#include "movementHandlers/MovementHandler.h"
#include "movementHandlers/RotationHandler.h"

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
    static constexpr auto rotationVelocity = 500.f;
    static constexpr auto moveVelocity = 2.5f;

    if (window.getMouseHandler().getButtonState(GLFW_MOUSE_BUTTON_LEFT) == app::MouseHandler::ButtonState::Pressed)
    {
        cameraObject.transform.rotation +=
            glm::vec3 {app::RotationHandler {window}.getRotation() * rotationVelocity * deltaTime, 0.f};
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
        glm::vec3 {glm::cos(cameraObject.transform.rotation.y), 0.f, -glm::sin(cameraObject.transform.rotation.y)};

    auto translation = glm::vec3 {};
    if (rawMovement.z != 0.f)
    {
        translation += cameraDirection * rawMovement.z;
    }
    if (rawMovement.x != 0.f)
    {
        translation += cameraRight * rawMovement.x;
    }
    if (rawMovement.y != 0.f)
    {
        translation.y = rawMovement.y;
    }

    if (glm::dot(translation, translation) > std::numeric_limits<float>::epsilon())
    {
        cameraObject.transform.translation += glm::normalize(translation) * moveVelocity * deltaTime;
    }

    camera.setViewYXZ(cameraObject.transform.translation,
                      {-cameraObject.transform.rotation.x, cameraObject.transform.rotation.y, 0.f});
}

void setObjects(panda::gfx::vulkan::Scene& scene,
                panda::gfx::vulkan::Mesh* vaseMesh,
                panda::gfx::vulkan::Mesh* floorMesh)
{
    auto object = panda::gfx::vulkan::Object {"Vase_1"};
    object.mesh = vaseMesh;
    object.transform.rotation = {};
    object.transform.translation = {1.f, 0.f, 0.f};
    object.transform.scale = {5.f, 5.f, 5.f};

    scene.objects.push_back(std::move(object));

    object = panda::gfx::vulkan::Object {"Vase_2"};
    object.mesh = vaseMesh;
    object.transform.rotation = {};
    object.transform.translation = {-1.f, 0.f, 0.f};
    object.transform.scale = {5.f, 5.f, 5.f};

    scene.objects.push_back(std::move(object));

    object = panda::gfx::vulkan::Object {"Floor"};
    object.mesh = floorMesh;
    object.transform.rotation = {};
    object.transform.translation = {0.f, 0.f, 0.f};
    object.transform.scale = {10.f, 10.f, 10.f};

    scene.objects.push_back(std::move(object));

    scene.lights.pointLights.push_back(panda::gfx::PointLight {
        panda::gfx::makeColorLight("Light_1", {1.f, 0.f,   0.f   },
         0.0f, 0.8f, 1.f, 1.f),
        {2.f, -2.f,  -1.5f },
        {1.f, 0.05f, 0.005f}
    });

    scene.lights.spotLights.push_back(panda::gfx::SpotLight {
        {panda::gfx::makeColorLight("SpotLight", {0.f, 1.f, 0.f}, 0.0f, 0.8f, 1.f, 1.f),
         {0.f, -5.f, 0.f},
         {1.f, 0.05f, 0.005f}},
        {0.f, 1.f, 0.f},
        glm::cos(glm::radians(30.f))
    });

    scene.lights.pointLights.push_back(panda::gfx::PointLight {
        panda::gfx::makeColorLight("Light_2", {0.f,  0.f,   1.f   },
         0.0f, 0.8f, 1.f, 1.f),
        {-2.f, -2.f,  -1.5f },
        {1.f,  0.05f, 0.005f}
    });

    scene.lights.directionalLights.push_back(panda::gfx::DirectionalLight {
        panda::gfx::makeColorLight("DirectionalLight", {1.f, .8f,  .8f },
         0.0f, 0.8f, 1.f, 0.02f),
        {0.f, -2.f, 10.f},
    });
}

struct TimeData
{
    TimeData()
        : time {std::chrono::steady_clock::now()}
    {
    }

    std::chrono::steady_clock::time_point time;
    float deltaTime {};

    auto update() -> void
    {
        auto currentTime = std::chrono::steady_clock::now();
        deltaTime = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - time).count();
        time = currentTime;
    }
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

    mainLoop();
    return 0;
}

auto App::mainLoop() -> void
{
    auto currentTime = TimeData {};

    const auto vaseMesh =
        panda::gfx::vulkan::Mesh::loadMesh(_api->getDevice(), config::resource::models / "smooth_vase.obj");
    const auto floorMesh =
        panda::gfx::vulkan::Mesh::loadMesh(_api->getDevice(), config::resource::models / "square.obj");

    auto scene = panda::gfx::vulkan::Scene {};

    setObjects(scene, vaseMesh.get(), floorMesh.get());

    auto cameraObject = panda::gfx::vulkan::Object {"Camera"};

    cameraObject.transform.translation = {0.f, 2.f, -8.f};
    scene.camera.setViewYXZ(cameraObject.transform.translation, cameraObject.transform.rotation);

    [[maybe_unused]] const auto gui = GuiManager {*_window};

    while (!_window->shouldClose()) [[likely]]
    {
        if (!_window->isMinimized())
        {
            panda::utils::signals::gameLoopIterationStarted.registerSender()();
            _window->processInput();

            currentTime.update();

            scene.camera.setPerspectiveProjection(glm::radians(50.f),
                                                  _api->getRenderer().getAspectRatio(),
                                                  0.1f,
                                                  100.f);
            processCamera(currentTime.deltaTime, *_window, cameraObject, scene.camera);

            _api->makeFrame(currentTime.deltaTime, scene);
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

}
