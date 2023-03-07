set(ENGINE_TARGET_NAME ${PROJECT_NAME}.Engine)

file(GLOB_RECURSE ${ENGINE_TARGET_NAME}_SRC_LIST
    ${SRC_DIR}/*.h
    ${SRC_DIR}/*.cpp
    )

add_executable(${ENGINE_TARGET_NAME} ${${ENGINE_TARGET_NAME}_SRC_LIST})

target_include_directories(${ENGINE_TARGET_NAME} PRIVATE ${SRC_DIR})
target_include_directories(${ENGINE_TARGET_NAME} SYSTEM PRIVATE ${FMT_DIR})

find_package(Vulkan REQUIRED)
target_link_libraries(${ENGINE_TARGET_NAME} PRIVATE Vulkan::Vulkan glfw glm fmt::fmt)

if (${CMAKE_CXX_COMPILER_ID} STREQUAL GNU)
    target_compile_options(${ENGINE_TARGET_NAME} PRIVATE -Wall -Wextra -Wshadow -Wpessimizing-move -Wredundant-move -Wpedantic -Werror -ffast-math -Wnoexcept)
elseif (${CMAKE_CXX_COMPILER_ID} STREQUAL MSVC)
else ()
endif ()

target_compile_definitions(${ENGINE_TARGET_NAME} PRIVATE ENGINE_TARGET_NAME=\"$<TARGET_PROPERTY:NAME>\")
target_compile_definitions(${ENGINE_TARGET_NAME} PRIVATE PROJECT_NAME=\"${CMAKE_PROJECT_NAME}\")
target_compile_definitions(${ENGINE_TARGET_NAME} PRIVATE VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1)

target_precompile_headers(${ENGINE_TARGET_NAME}
    PRIVATE
    <vulkan/vulkan.hpp>
    <GLFW/glfw3.h>
    "app/Logger.h"
    )

add_subdirectory(shader)