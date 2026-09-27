cmake_minimum_required(VERSION 3.25)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

include(cmake/Setup.cmake)
include(ProjectOptions.cmake)
include(Dependencies.cmake)

drip_assure_out_of_source_builds()

project(
        drip
        DESCRIPTION "Adaptive SPH simulation with real-time Vulkan rendering"
        LANGUAGES CXX CUDA
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_CUDA_STANDARD 20)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_EXTENSIONS OFF)

drip_setup_options()
drip_global_options()

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

drip_setup_dependencies()

drip_local_options()

target_compile_features(drip_options INTERFACE cxx_std_${CMAKE_CXX_STANDARD})

add_library(drip::options ALIAS drip_options)
add_library(drip::warnings ALIAS drip_warnings)

include(DripLibrary)

add_subdirectory(src)

enable_testing()
add_subdirectory(tests)

if (MSVC)
    get_all_installable_targets(all_targets)
    set_target_properties(${all_targets} PROPERTIES VS_DEBUGGER_ENVIRONMENT "PATH=$(VC_ExecutablePath_x64);%PATH%")
endif ()

set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT drip)
