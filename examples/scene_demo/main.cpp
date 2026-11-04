#include <chrono>
#include <cstdlib>
#include <exception>
#include <glm/ext/quaternion_trigonometric.hpp>
#include <glm/ext/vector_float2.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>
#include <iostream>
#include <memory>
#include <panda/Assert.hpp>
#include <panda/CompletionPoint.hpp>
#include <panda/Context.hpp>
#include <panda/Cube.hpp>
#include <panda/Logger.hpp>
#include <panda/Mesh.hpp>
#include <panda/Plane.hpp>
#include <panda/Transform.hpp>
#include <panda/UploadBatch.hpp>
#include <panda/scene/Scene.hpp>
#include <panda/tools/gui/Window.hpp>
#include <span>
#include <tuple>
#include <vector>

namespace
{
struct AnimatedObjects
{
    panda::scene::Entity group;
    std::vector<panda::scene::Entity> cubes;
};

void addCameraAndLight(panda::scene::Scene& scene)
{
    static constexpr auto position = glm::vec3 {7.0F, 5.0F, 9.0F};
    const auto camera = panda::expect(
        scene.createEntity({
            .name = "Camera",
            .transform = {.position = position,
                          .rotation = glm::quatLookAtRH(glm::normalize(-position), glm::vec3 {0.0F, 1.0F, 0.0F})}
    }),
        "Create camera object");
    panda::expect(
        scene.setCamera(camera, panda::scene::PerspectiveCamera {.verticalFieldOfViewRadians = glm::radians(50.0F)}),
        "Attach camera lens");
    panda::expect(scene.setActiveCamera(camera), "Select camera");
    const auto light = panda::expect(
        scene.createEntity({.name = "Sun",
                            .transform = {.rotation = glm::quatLookAtRH(glm::normalize(glm::vec3 {0.55F, -0.6F, 0.58F}),
                                                                        glm::vec3 {0.0F, 1.0F, 0.0F})}}),
        "Create light object");
    panda::expect(scene.setDirectionalLight(light, panda::scene::DirectionalLight {}), "Attach directional light");
}

void addGround(panda::scene::Scene& scene, const panda::Mesh& ground)
{
    const auto floor =
        panda::expect(scene.createEntity({
                          .name = "Ground",
                          .transform = {.position = {0.0F, -0.5F, 0.0F}, .scale = {14.0F, 1.0F, 14.0F}}
    }),
                      "Create ground object");
    panda::expect(scene.setMesh(floor,
                                panda::scene::MeshInstance {
                                    .mesh = ground,
                                    .tint = {0.65F, 0.67F, 0.7F, 1.0F}
    }),
                  "Attach ground");
}

auto addCube(panda::scene::Scene& scene, const panda::Mesh& cube, const panda::scene::Entity& group, glm::vec2 grid)
    -> panda::scene::Entity
{
    const auto object = panda::expect(
        scene.createEntity({
            .name = "Cube",
            .transform = {.position = {2.0F * grid.x, 0.0F, 2.0F * grid.y},
                          .rotation = glm::angleAxis(glm::radians(30.0F), glm::vec3 {0.0F, 1.0F, 0.0F})}
    }),
        "Create cube object");
    const auto color = glm::vec4 {0.5F + (0.25F * grid.x), 0.5F + (0.25F * grid.y), 0.7F, 1.0F};
    panda::expect(scene.setMesh(object, panda::scene::MeshInstance {.mesh = cube, .tint = color}),
                  "Attach shared cube mesh");
    panda::expect(scene.setParent(object, group), "Parent cube to group");
    return object;
}

auto addObjects(panda::scene::Scene& scene, const panda::Mesh& cube, const panda::Mesh& ground) -> AnimatedObjects
{
    addGround(scene, ground);
    auto objects =
        AnimatedObjects {.group = panda::expect(scene.createEntity({.name = "Animated group"}), "Create group"),
                         .cubes = {}};
    for (auto column = -1; column <= 1; ++column)
    {
        for (auto row = -1; row <= 1; ++row)
        {
            objects.cubes.push_back(
                addCube(scene, cube, objects.group, glm::vec2 {static_cast<float>(column), static_cast<float>(row)}));
        }
    }
    return objects;
}

void animateGroup(panda::scene::Scene& scene, const panda::scene::Entity& group, float elapsed)
{
    static constexpr auto angularSpeed = 0.3F;
    panda::expect(
        scene.setLocalTransform(group,
                                {.rotation = glm::angleAxis(angularSpeed * elapsed, glm::vec3 {0.0F, 1.0F, 0.0F})}),
        "Rotate group");
}

void animateWave(panda::scene::Scene& scene, std::span<const panda::scene::Entity> cubes, float elapsed)
{
    static constexpr auto amplitude = 0.8F;
    static constexpr auto angularSpeed = 2.0F;
    for (const auto& cube : cubes)
    {
        auto transform = panda::expect(scene.getLocalTransform(cube), "Read cube transform");
        const auto radius = glm::length(glm::vec2 {transform.position.x, transform.position.z});
        transform.position.y = amplitude * (1.0F + glm::sin((angularSpeed * elapsed) - radius));
        panda::expect(scene.setLocalTransform(cube, transform), "Animate cube wave");
    }
}

void drawLoop(const panda::tools::Window& window,
              panda::Context& context,
              panda::scene::Scene& scene,
              const AnimatedObjects& objects)
{
    const auto start = std::chrono::steady_clock::now();
    while (!window.shouldClose())
    {
        const auto extent = panda::expect(window.getFramebufferExtent(), "Read framebuffer size");
        if (extent.width == 0 || extent.height == 0)
        {
            panda::expect(panda::tools::Window::waitForInput(), "Wait while framebuffer is suspended");
            continue;
        }
        const auto elapsed = std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count();
        animateGroup(scene, objects.group, elapsed);
        animateWave(scene, objects.cubes, elapsed);
        const auto aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
        const auto view = panda::expect(scene.createRenderView(aspect), "Prepare scene");
        const auto frame = panda::expect(context.render(view), "Render scene");
        panda::expect(frame.status == panda::FrameResult::Status::Suspended ? panda::tools::Window::waitForInput()
                                                                            : window.processInput(),
                      "Process input");
    }
}

void run(const panda::tools::Window& window, panda::Context& context)
{
    const auto cubeGeometry = panda::createCubeGeometry();
    const auto groundGeometry = panda::createPlaneGeometry();
    const auto cube = panda::expect(context.createMeshFromGeometry(cubeGeometry), "Create cube mesh");
    const auto ground = panda::expect(context.createMeshFromGeometry(groundGeometry), "Create ground mesh");
    auto batch = panda::expect(context.createUploadBatch(), "Create upload batch");
    panda::expect(batch.write(cube, cubeGeometry), "Stage cube");
    panda::expect(batch.write(ground, groundGeometry), "Stage ground");
    const auto completion = panda::expect(batch.submit(), "Submit geometry upload");
    const auto status = panda::expect(context.wait(completion, std::chrono::seconds {10}), "Wait for geometry upload");
    panda::expect(status == panda::WaitStatus::Completed, "Geometry upload did not finish in time");
    auto scene = panda::expect(panda::scene::Scene::create(context), "Create scene");
    addCameraAndLight(scene);
    const auto objects = addObjects(scene, cube, ground);
    drawLoop(window, context, scene, objects);
}
}

auto main() -> int
{
    try
    {
        std::ignore = panda::log::Logger::instance().addSink(std::make_unique<panda::log::StreamSink>(std::clog));
        const auto window =
            panda::expect(panda::tools::Window::create(glm::uvec2 {1280, 720}, "Panda - Scene demo"), "Create window");
        auto context = panda::expect(
            panda::Context::createWithSurface(window,
                                              {.applicationName = "Panda scene demo", .enableValidation = true}),
            "Create Vulkan context");
        run(window, context);
        return EXIT_SUCCESS;
    }
    catch (const std::exception& error)
    {
        panda::panic(error.what());
    }
}
