set(ENGINE_TARGET_NAME ${PROJECT_NAME}.Engine)
set(CONFIG_OUTPUT ${CMAKE_BINARY_DIR}/engine/generated)

file(GLOB_RECURSE ${ENGINE_TARGET_NAME}_SRC_LIST
        src/*.cpp
)

add_library(${ENGINE_TARGET_NAME} STATIC ${${ENGINE_TARGET_NAME}_SRC_LIST})

target_include_directories(${ENGINE_TARGET_NAME} PUBLIC include)
target_include_directories(${ENGINE_TARGET_NAME} PUBLIC ${CONFIG_OUTPUT})

find_package(Vulkan REQUIRED)
target_link_system_libraries(
        ${ENGINE_TARGET_NAME}
        PRIVATE
        Vulkan::Vulkan glm fmt assimp imgui)

target_compile_definitions(
        ${ENGINE_TARGET_NAME}
        PRIVATE
        VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1
        VULKAN_HPP_NO_EXCEPTIONS
        VULKAN_HPP_ASSERT_ON_RESULT=EXPECT
        VULKAN_HPP_ASSERT=DEBUG_EXPECT)

if (PD_ENABLE_PCH)
    target_precompile_headers(${ENGINE_TARGET_NAME} PRIVATE include/panda/Common.h)
endif ()

add_subdirectory(shader)
add_subdirectory(config)