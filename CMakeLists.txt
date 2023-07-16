cmake_minimum_required(VERSION 3.16)

if (NOT DEFINED CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 20)
endif()

set(CMAKE_CXX_EXTENSIONS OFF)

project(Panda)

set(CONFIG_DIR src/config)
set(SRC_DIR ${PROJECT_SOURCE_DIR}/src)
set(EXT_DIR ${PROJECT_SOURCE_DIR}/ext)

include(cmake/PreventInSourceBuilds.cmake)
include(Options.cmake)

PD_setup_options()

PD_global_options()
PD_local_options(${EXT_DIR})

set(CMAKE_CXX_VISIBILITY_PRESET hidden)
target_compile_features(PD_options INTERFACE cxx_std_${CMAKE_CXX_STANDARD})

add_library(PD::PD_options ALIAS PD_options)
add_library(PD::PD_warnings ALIAS PD_warnings)

set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DPD_DEBUG=1 -DPD_RELEASE=0")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -DPD_DEBUG=0 -DPD_RELEASE=1")
set(CMAKE_CXX_FLAGS_MINSIZEREL "${CMAKE_CXX_FLAGS_MINSIZEREL} -DPD_DEBUG=0 -DPD_RELEASE=1")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} -DPD_DEBUG=1 -DPD_RELEASE=1")

set(FMT_DIR ${EXT_DIR}/fmt)
set(VULKAN_HPP_DIR ${EXT_DIR}/Vulkan-Hpp)
set(GLM_DIR ${EXT_DIR}/glm)
set(GLFW_DIR ${EXT_DIR}/glfw)
set(TINYOBJLOADER_DIR ${EXT_DIR}/tinyobjloader)

add_subdirectory(${FMT_DIR})
add_subdirectory(${TINYOBJLOADER_DIR})
add_subdirectory(${VULKAN_HPP_DIR})
add_subdirectory(${GLM_DIR})
add_subdirectory(${GLFW_DIR})

add_subdirectory(${SRC_DIR})

# If MSVC is being used, and ASAN is enabled, we need to set the debugger environment
# so that it behaves well with MSVC's debugger, and we can run the target from visual studio
if(MSVC)
    get_all_installable_targets(all_targets)
    message("all_targets=${all_targets}")
    set_target_properties(${all_targets} PROPERTIES VS_DEBUGGER_ENVIRONMENT "PATH=$(VC_ExecutablePath_x64);%PATH%")
endif()

set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT intro)