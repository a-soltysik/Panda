cmake_minimum_required(VERSION 3.25)

if (NOT DEFINED CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 20)
endif ()

set(CMAKE_CXX_EXTENSIONS OFF)

project(Panda)

include(cmake/PreventInSourceBuilds.cmake)
include(Options.cmake)

PD_setup_options()

PD_global_options()
PD_local_options()

target_compile_features(PD_options INTERFACE cxx_std_${CMAKE_CXX_STANDARD})

add_library(PD::Options ALIAS PD_options)
add_library(PD::Warnings ALIAS PD_warnings)

set(ENGINE_DIR ${PROJECT_SOURCE_DIR}/engine)
set(APP_DIR ${PROJECT_SOURCE_DIR}/app)
set(EXT_DIR ${PROJECT_SOURCE_DIR}/ext)

set(ASSIMP_WARNINGS_AS_ERRORS OFF)
set(BUILD_SHARED_LIBS OFF)
set(ASSIMP_BUILD_ASSIMP_TOOL OFF)
set(ASSIMP_BUILD_TESTS OFF)
set(ASSIMP_INSTALL OFF)
set(ASSIMP_INSTALL_PDB OFF)
set(INJECT_DEBUG_POSTFIX OFF)
set(IMGUI_BUILD_GLFW_BINDING ON)
set(IMGUI_BUILD_VULKAN_BINDING ON)
set(GLM_ENABLE_CXX_20 ON)

file(COPY ${EXT_DIR}/.clang-tidy
        DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/_deps/)

include(cmake/InterproceduralOptimization.cmake)
include(cmake/CPM.cmake)
CPMAddPackage("gh:fmtlib/fmt#11.1.1")
CPMAddPackage("gh:KhronosGroup/Vulkan-Hpp@1.4.303")
CPMAddPackage("gh:g-truc/glm#1.0.1")
CPMAddPackage("gh:glfw/glfw#3.4")
PD_suppress_ipo()
CPMAddPackage("gh:assimp/assimp#v5.4.3")
PD_resume_ipo()

add_subdirectory(${EXT_DIR}/imgui-cmake)
add_subdirectory(${EXT_DIR}/stb-cmake)

add_subdirectory(${ENGINE_DIR})
if (PD_BUILD_APP)
    CPMAddPackage("gh:samhocevar/portable-file-dialogs#0.1.0")

    add_subdirectory(${EXT_DIR}/implot-cmake)
    add_subdirectory(${APP_DIR})
endif ()

include(cmake/Utils.cmake)
setup_msvc_if_needed()
