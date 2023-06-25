set(ENGINE_TARGET_NAME ${PROJECT_NAME}.Engine)

add_subdirectory(config)

file(GLOB_RECURSE ${ENGINE_TARGET_NAME}_SRC_LIST
    ${SRC_DIR}/*.h
    ${SRC_DIR}/*.cpp
    )

add_executable(${ENGINE_TARGET_NAME} ${${ENGINE_TARGET_NAME}_SRC_LIST})

target_link_libraries(${ENGINE_TARGET_NAME} PRIVATE PD::PD_options)
target_link_libraries(${ENGINE_TARGET_NAME} PRIVATE PD::PD_warnings)

target_include_directories(${ENGINE_TARGET_NAME} PRIVATE ${SRC_DIR})
target_include_directories(${ENGINE_TARGET_NAME} PRIVATE ${CMAKE_BINARY_DIR}/generated)

find_package(Vulkan REQUIRED)
target_link_system_libraries(
    ${ENGINE_TARGET_NAME}
    PRIVATE
    Vulkan::Vulkan glfw glm fmt)

target_compile_definitions(${ENGINE_TARGET_NAME} PRIVATE VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1 VULKAN_HPP_NO_EXCEPTIONS)
target_compile_definitions(${ENGINE_TARGET_NAME} PRIVATE VULKAN_HPP_ASSERT_ON_RESULT=EXPECT)
target_compile_definitions(${ENGINE_TARGET_NAME} PRIVATE VULKAN_HPP_ASSERT=DEBUG_EXPECT)

target_precompile_headers(${ENGINE_TARGET_NAME} PRIVATE pch.h)

add_subdirectory(shader)