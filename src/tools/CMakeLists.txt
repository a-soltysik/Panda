panda_add_component(panda_tools
    ALIAS Panda::Tools
    INCLUDE_DIRECTORY "${PROJECT_SOURCE_DIR}/include/tools"
    SOURCES Camera.cpp gui/Window.cpp gui/GlfwSession.cpp
    PUBLIC_HEADERS panda/tools/Camera.hpp panda/tools/gui/Window.hpp
    PRIVATE_HEADERS gui/GlfwSession.hpp
    LIBRARIES PUBLIC Panda::Panda glm::glm PRIVATE glfw)
