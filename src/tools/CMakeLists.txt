panda_add_component(panda_tools
    ALIAS Panda::Tools
    INCLUDE_DIRECTORY "${PROJECT_SOURCE_DIR}/include/tools"
    SOURCES gui/Window.cpp gui/GlfwSession.cpp image/StbImageWriter.cpp image/Writer.cpp
    PUBLIC_HEADERS panda/tools/gui/Window.hpp panda/tools/image/Writer.hpp
    PRIVATE_HEADERS gui/GlfwSession.hpp
    LIBRARIES PUBLIC Panda::Panda glm::glm PRIVATE glfw)
target_include_directories(panda_tools SYSTEM PRIVATE "${stb_SOURCE_DIR}")
# This file contains only stb's implementation; keep Panda wrapper code under normal linting.
set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/image/StbImageWriter.cpp" PROPERTIES SKIP_LINTING ON)
