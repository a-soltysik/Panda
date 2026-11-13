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
#include <panda/Geometry.hpp>
#include <panda/Logger.hpp>
#include <panda/Mesh.hpp>
#include <panda/Plane.hpp>
#include <panda/RenderView.hpp>
#include <panda/Sphere.hpp>
#include <panda/Transform.hpp>
#include <panda/UploadBatch.hpp>
#include <panda/scene/Entity.hpp>
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
    std::vector<panda::scene::Entity> objects;
};

struct SceneMeshes
{
    panda::Mesh cube;
    panda::Mesh ground;
    panda::Mesh sphere;
    panda::Mesh triangle;
};

auto createTriangleGeometry() -> panda::Geometry
{
    auto geometry = panda::Geometry {};
    geometry.vertices = {
        {.position = {-0.7F, 0.0F, 0.0F}, .normal = {0.0F, 0.0F, 1.0F}, .color = {1.0F, 0.2F, 0.1F, 1.0F}},
        {.position = {0.7F, 0.0F, 0.0F},  .normal = {0.0F, 0.0F, 1.0F}, .color = {0.1F, 1.0F, 0.2F, 1.0F}},
        {.position = {0.0F, 1.4F, 0.0F},  .normal = {0.0F, 0.0F, 1.0F}, .color = {0.2F, 0.3F, 1.0F, 1.0F}}
    };
    return geometry;
}

auto addTriangle(panda::scene::Scene& scene, const panda::Mesh& triangle, glm::vec2 grid) -> panda::scene::Entity
{
    const auto object = panda::expect(
        scene.createEntity({
            .name = "Vertex-colored triangle",
            .transform = {.position = {2.0F * grid.x, 0.0F, 2.0F * grid.y},
                          .rotation = glm::angleAxis(glm::radians(30.0F), glm::vec3 {0.0F, 1.0F, 0.0F})}
    }),
        "Create triangle object");
    panda::expect(scene.setMesh(object, panda::scene::MeshInstance {.mesh = triangle}), "Attach triangle mesh");
    return object;
}

void addCamera(panda::scene::Scene& scene)
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
}

void addDirectionalLights(panda::scene::Scene& scene)
{
    const auto light = panda::expect(
        scene.createEntity({.name = "Sun",
                            .transform = {.rotation = glm::quatLookAtRH(glm::normalize(glm::vec3 {0.55F, -0.6F, 0.58F}),
                                                                        glm::vec3 {0.0F, 1.0F, 0.0F})}}),
        "Create light object");
    panda::expect(scene.setLight(light, panda::scene::DirectionalLight {.shadow = panda::ShadowSettings {}}),
                  "Attach directional light");
    const auto fillLight = panda::expect(
        scene.createEntity(
            {.name = "Unshadowed fill",
             .transform = {.rotation = glm::quatLookAtRH(glm::normalize(glm::vec3 {-0.65F, -0.45F, -0.3F}),
                                                         glm::vec3 {0.0F, 1.0F, 0.0F})}}),
        "Create fill light");
    panda::expect(scene.setLight(fillLight, panda::scene::DirectionalLight {.intensity = 0.25F, .shadow = {}}),
                  "Attach fill light");
}

void addPointLight(panda::scene::Scene& scene)
{
    const auto pointLight =
        panda::expect(scene.createEntity({.name = "Blue point light", .transform = {.position = {-3.0F, 1.5F, 0.0F}}}),
                      "Create point light");
    panda::expect(scene.setLight(pointLight,
                                 panda::scene::PointLight {
                                     .color = {0.25F, 0.45F, 1.0F},
                                     .intensity = 18.0F,
                                     .range = 6.0F,
                                     .shadow = panda::ShadowSettings {}
    }),
                  "Attach point light");
}

void addSpotLight(panda::scene::Scene& scene)
{
    static constexpr auto spotPosition = glm::vec3 {3.0F, 5.0F, 2.0F};
    const auto spotDirection = glm::normalize(-spotPosition);
    const auto spotLight =
        panda::expect(scene.createEntity({
                          .name = "Warm spot light",
                          .transform = {.position = spotPosition,
                                        .rotation = glm::quatLookAtRH(spotDirection, glm::vec3 {0.0F, 1.0F, 0.0F})}
    }),
                      "Create spot light");
    panda::expect(scene.setLight(spotLight,
                                 panda::scene::SpotLight {
                                     .color = {1.0F, 0.68F, 0.35F},
                                     .intensity = 32.0F,
                                     .range = 9.0F,
                                     .innerConeAngleRadians = glm::radians(15.0F),
                                     .outerConeAngleRadians = glm::radians(25.0F),
                                     .shadow = panda::ShadowSettings {}
    }),
                  "Attach spot light");
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
                                    .tint = {0.45F, 0.47F, 0.5F, 1.0F}
    }),
                  "Attach ground");
}

auto addSphere(panda::scene::Scene& scene, const panda::Mesh& sphere, const panda::scene::Entity& group)
    -> panda::scene::Entity
{
    const auto object = panda::expect(scene.createEntity({.name = "Sphere", .transform = {.scale = glm::vec3 {1.3F}}}),
                                      "Create sphere object");
    panda::expect(scene.setMesh(object,
                                panda::scene::MeshInstance {
                                    .mesh = sphere,
                                    .tint = {0.95F, 0.38F, 0.12F, 1.0F}
    }),
                  "Attach sphere mesh");
    panda::expect(scene.setParent(object, group), "Parent sphere to group");
    return object;
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

void addGridObjects(panda::scene::Scene& scene, const SceneMeshes& meshes, AnimatedObjects& objects)
{
    static constexpr auto gridRadius = 1;
    static constexpr auto gridSide = (gridRadius * 2) + 1;
    static constexpr auto centerIndex = (gridSide * gridSide) / 2;
    static constexpr auto triangleIndex = (gridSide * gridSide) - 1;
    for (auto index = 0; index < gridSide * gridSide; ++index)
    {
        const auto column = (index / gridSide) - gridRadius;
        const auto row = (index % gridSide) - gridRadius;
        if (index == centerIndex)
        {
            objects.objects.push_back(addSphere(scene, meshes.sphere, objects.group));
        }
        else if (index == triangleIndex)
        {
            objects.objects.push_back(
                addTriangle(scene, meshes.triangle, glm::vec2 {static_cast<float>(column), static_cast<float>(row)}));
        }
        else
        {
            objects.objects.push_back(addCube(scene,
                                              meshes.cube,
                                              objects.group,
                                              glm::vec2 {static_cast<float>(column), static_cast<float>(row)}));
        }
    }
}

auto addObjects(panda::scene::Scene& scene, const SceneMeshes& meshes) -> AnimatedObjects
{
    addGround(scene, meshes.ground);
    auto objects =
        AnimatedObjects {.group = panda::expect(scene.createEntity({.name = "Animated group"}), "Create group"),
                         .objects = {}};
    addGridObjects(scene, meshes, objects);
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

void animateWave(panda::scene::Scene& scene, std::span<const panda::scene::Entity> objects, float elapsed)
{
    static constexpr auto amplitude = 0.8F;
    static constexpr auto angularSpeed = 2.0F;
    for (const auto& object : objects)
    {
        auto transform = panda::expect(scene.getLocalTransform(object), "Read object transform");
        const auto radius = glm::length(glm::vec2 {transform.position.x, transform.position.z});
        transform.position.y = amplitude * (1.0F + glm::sin((angularSpeed * elapsed) - radius));
        panda::expect(scene.setLocalTransform(object, transform), "Animate object wave");
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
        animateWave(scene, objects.objects, elapsed);
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
    const auto sphereGeometry =
        panda::expect(panda::createSphereGeometry({.longitudeSegments = 48, .latitudeRings = 24}),
                      "Create sphere geometry");
    const auto triangleGeometry = createTriangleGeometry();
    const auto meshes = SceneMeshes {
        .cube = panda::expect(context.createMeshFromGeometry(cubeGeometry), "Create cube mesh"),
        .ground = panda::expect(context.createMeshFromGeometry(groundGeometry), "Create ground mesh"),
        .sphere = panda::expect(context.createMeshFromGeometry(sphereGeometry), "Create sphere mesh"),
        .triangle = panda::expect(context.createMeshFromGeometry(triangleGeometry), "Create triangle mesh")};
    auto batch = panda::expect(context.createUploadBatch(), "Create upload batch");
    panda::expect(batch.write(meshes.cube, cubeGeometry), "Stage cube");
    panda::expect(batch.write(meshes.ground, groundGeometry), "Stage ground");
    panda::expect(batch.write(meshes.sphere, sphereGeometry), "Stage sphere");
    panda::expect(batch.write(meshes.triangle, triangleGeometry), "Stage triangle");
    const auto completion = panda::expect(batch.submit(), "Submit geometry upload");
    const auto status = panda::expect(context.wait(completion, std::chrono::seconds {10}), "Wait for geometry upload");
    panda::expect(status == panda::WaitStatus::Completed, "Geometry upload did not finish in time");
    auto scene = panda::expect(panda::scene::Scene::create(context), "Create scene");
    addCamera(scene);
    addDirectionalLights(scene);
    addPointLight(scene);
    addSpotLight(scene);
    const auto objects = addObjects(scene, meshes);
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
