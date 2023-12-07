cmake_minimum_required(VERSION 3.25)

if (NOT DEFINED CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 20)
endif ()

set(CMAKE_CXX_EXTENSIONS OFF)

project(Panda)

set(EXT_DIR ${PROJECT_SOURCE_DIR}/ext)

include(cmake/PreventInSourceBuilds.cmake)
include(Options.cmake)

PD_setup_options()

PD_global_options()
PD_local_options(${EXT_DIR})

target_compile_features(PD_options INTERFACE cxx_std_${CMAKE_CXX_STANDARD})

add_library(PD::Options ALIAS PD_options)
add_library(PD::Warnings ALIAS PD_warnings)

set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DPD_DEBUG=1 -DPD_RELEASE=0")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -DPD_DEBUG=0 -DPD_RELEASE=1")
set(CMAKE_CXX_FLAGS_MINSIZEREL "${CMAKE_CXX_FLAGS_MINSIZEREL} -DPD_DEBUG=0 -DPD_RELEASE=1")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} -DPD_DEBUG=1 -DPD_RELEASE=1")

set(ENGINE_DIR ${PROJECT_SOURCE_DIR}/engine)
set(APP_DIR ${PROJECT_SOURCE_DIR}/app)

set(IMGUI_DIR ${EXT_DIR}/imgui-cmake)
set(IMPLOT_DIR ${EXT_DIR}/implot-cmake)
set(STB_DIR ${EXT_DIR}/stb-cmake)

set(ASSIMP_WARNINGS_AS_ERRORS OFF)
set(BUILD_SHARED_LIBS OFF)
set(ASSIMP_BUILD_ASSIMP_TOOL OFF)
set(ASSIMP_BUILD_TESTS OFF)
set(ASSIMP_INSTALL OFF)
set(ASSIMP_INSTALL_PDB OFF)
set(INJECT_DEBUG_POSTFIX OFF)
set(IMGUI_BUILD_GLFW_BINDING ON)
set(IMGUI_BUILD_VULKAN_BINDING ON)

file(COPY ${EXT_DIR}/.clang-tidy
        DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/_deps/)

include(cmake/InterproceduralOptimization.cmake)
include(cmake/CPM.cmake)

add_subdirectory(${STB_DIR})
add_subdirectory(${IMGUI_DIR})
PD_suppress_ipo()
CPMAddPackage("gh:assimp/assimp#v5.3.1")
PD_resume_ipo()
CPMAddPackage("gh:fmtlib/fmt#10.1.1")
CPMAddPackage("gh:KhronosGroup/Vulkan-Hpp#v1.3.272")
CPMAddPackage("gh:g-truc/glm#0.9.9.8")
CPMAddPackage("gh:glfw/glfw#3.3.8")

add_subdirectory(${ENGINE_DIR})
if (PD_BUILD_APP)
    add_subdirectory(${IMPLOT_DIR})
    CPMAddPackage("gh:samhocevar/portable-file-dialogs#0.1.0")

    add_subdirectory(${APP_DIR})
endif ()

setup_msvc_if_needed()
