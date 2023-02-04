#pragma once

namespace panda
{

class Window
{
public:
    Window();
    ~Window() noexcept;

    [[nodiscard]] auto getHandle() const noexcept -> GLFWwindow*;
    auto shouldClose() const noexcept -> bool;
    auto processInput() const noexcept -> void;

private:
    GLFWwindow* window = nullptr;
};

}
