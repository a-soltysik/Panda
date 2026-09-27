panda_add_component(panda_tools
    ALIAS Panda::Tools
    INCLUDE_DIRECTORY "${PROJECT_SOURCE_DIR}/include/tools"
    SOURCES gui/Window.cpp gui/GlfwSession.cpp
    LIBRARIES PUBLIC Panda::Panda PRIVATE glm::glm glfw)
