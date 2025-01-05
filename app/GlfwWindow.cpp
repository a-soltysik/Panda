#include "GlfwWindow.h"

#include <backends/imgui_impl_glfw.h>
#include <imgui.h>
#include <implot.h>

namespace
{

void framebufferResizeCallback(GLFWwindow* window, int width, int height)
{
    static const auto sender = panda::utils::signals::frameBufferResized.registerSender();
    const auto id = app::GlfwWindow::makeId(window);
    panda::log::Info("Size of window [{}] changed to {}x{}", id, width, height);

    sender(panda::utils::signals::FrameBufferResizedData {.id = id, .x = width, .y = height});
}

}

namespace app
{

GlfwWindow::GlfwWindow(glm::uvec2 size, const char* name)
    : _window {createWindow(size, name)},
      _size {size}
{
    keyboardHandler = std::make_unique<KeyboardHandler>(*this);
    mouseHandler = std::make_unique<MouseHandler>(*this);
    glfwSetFramebufferSizeCallback(_window, framebufferResizeCallback);

    _frameBufferResizedReceiver = panda::utils::signals::frameBufferResized.connect([this](auto data) {
        if (data.id == getId())
        {
            panda::log::Debug("Received framebuffer resized notif");
            _size = {data.x, data.y};
        }
    });

    setupImGui();
}

GlfwWindow::~GlfwWindow() noexcept
{
    ImPlot::DestroyContext();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(_window);
    glfwTerminate();
    panda::log::Info("Window [{}] destroyed", static_cast<void*>(_window));
}

auto GlfwWindow::createWindow(glm::uvec2 size, const char* name) -> GLFWwindow*
{
    panda::expect(glfwInit(), GLFW_TRUE, "Failed to initialize GLFW");

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    auto* window = glfwCreateWindow(static_cast<int>(size.x), static_cast<int>(size.y), name, nullptr, nullptr);
    panda::log::Info("Window [{}] {}x{} px created", static_cast<void*>(window), size.x, size.y);

    return window;
}

auto GlfwWindow::getRequiredExtensions() const -> std::vector<const char*>
{
    auto glfwExtensionsCount = uint32_t {};
    const auto* glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionsCount);

    if (glfwExtensions == nullptr)
    {
        return {};
    }

    const auto extensionsSpan = std::span(glfwExtensions, glfwExtensionsCount);

    return {extensionsSpan.begin(), extensionsSpan.end()};
}

auto GlfwWindow::createSurface(VkInstance instance) const -> VkSurfaceKHR
{
    auto* newSurface = VkSurfaceKHR {};
    glfwCreateWindowSurface(instance, _window, nullptr, &newSurface);

    return panda::expect(
        newSurface,
        [](const auto* result) {
            return result != nullptr;
        },
        "Unable to create surface");
}

auto GlfwWindow::shouldClose() const -> bool
{
    return glfwWindowShouldClose(_window) == GLFW_TRUE;
}

auto GlfwWindow::processInput() -> void
{
    glfwPollEvents();
}

auto GlfwWindow::getSize() const -> glm::uvec2
{
    return _size;
}

auto GlfwWindow::isMinimized() const -> bool
{
    return _size.x == 0 || _size.y == 0;
}

auto GlfwWindow::waitForInput() -> void
{
    glfwWaitEvents();
}

auto GlfwWindow::setKeyCallback(GLFWkeyfun callback) const noexcept -> GLFWkeyfun
{
    return glfwSetKeyCallback(_window, callback);
}

auto GlfwWindow::setMouseButtonCallback(GLFWmousebuttonfun callback) const noexcept -> GLFWmousebuttonfun
{
    return glfwSetMouseButtonCallback(_window, callback);
}

auto GlfwWindow::setCursorPositionCallback(GLFWcursorposfun callback) const noexcept -> GLFWcursorposfun
{
    return glfwSetCursorPosCallback(_window, callback);
}

auto GlfwWindow::getId() const -> size_t
{
    return makeId(_window);
}

auto GlfwWindow::getKeyboardHandler() const noexcept -> const KeyboardHandler&
{
    return *keyboardHandler;
}

auto GlfwWindow::makeId(GLFWwindow* window) -> panda::Window::Id
{
    return std::bit_cast<size_t>(window);
}

auto GlfwWindow::getMouseHandler() const noexcept -> const MouseHandler&
{
    return *mouseHandler;
}

auto GlfwWindow::setupImGui() -> void
{
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGui_ImplGlfw_InitForVulkan(_window, true);
}

}
