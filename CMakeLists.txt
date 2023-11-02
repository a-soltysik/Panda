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

add_library(PD::PD_options ALIAS PD_options)
add_library(PD::PD_warnings ALIAS PD_warnings)

set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DPD_DEBUG=1 -DPD_RELEASE=0")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -DPD_DEBUG=0 -DPD_RELEASE=1")
set(CMAKE_CXX_FLAGS_MINSIZEREL "${CMAKE_CXX_FLAGS_MINSIZEREL} -DPD_DEBUG=0 -DPD_RELEASE=1")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} -DPD_DEBUG=1 -DPD_RELEASE=1")

set(ENGINE_DIR ${PROJECT_SOURCE_DIR}/engine)
set(APP_DIR ${PROJECT_SOURCE_DIR}/app)

set(FMT_DIR ${EXT_DIR}/fmt)
set(VULKAN_HPP_DIR ${EXT_DIR}/Vulkan-Hpp)
set(GLM_DIR ${EXT_DIR}/glm)
set(GLFW_DIR ${EXT_DIR}/glfw)
set(TINYOBJLOADER_DIR ${EXT_DIR}/tinyobjloader)
set(IMGUI_DIR ${EXT_DIR}/imgui-cmake)
set(IMPLOT_DIR ${EXT_DIR}/implot-cmake)
set(FREETYPE_DIR ${EXT_DIR}/freetype)

add_subdirectory(${FMT_DIR})
add_subdirectory(${TINYOBJLOADER_DIR})
add_subdirectory(${VULKAN_HPP_DIR})
add_subdirectory(${GLM_DIR})

add_subdirectory(${ENGINE_DIR})
if (PD_BUILD_APP)
    set(IMGUI_BUILD_GLFW_BINDING ON)
    set(IMGUI_BUILD_VULKAN_BINDING ON)

    add_subdirectory(${GLFW_DIR})
    add_subdirectory(${IMGUI_DIR})
    add_subdirectory(${IMPLOT_DIR})

    add_subdirectory(${APP_DIR})
endif ()

setup_msvc_if_needed()
