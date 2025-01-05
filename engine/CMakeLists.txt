set(ENGINE_TARGET_NAME ${PROJECT_NAME}.Engine)
set(CONFIG_OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/generated)

file(GLOB_RECURSE ${ENGINE_TARGET_NAME}_SRC_LIST
        src/*.cpp
)

file(GLOB_RECURSE ${ENGINE_TARGET_NAME}_INC_LIST
        include/*.h
)

add_library(${ENGINE_TARGET_NAME})
add_library(PD::Engine ALIAS ${ENGINE_TARGET_NAME})

target_sources(${ENGINE_TARGET_NAME} PRIVATE ${${ENGINE_TARGET_NAME}_SRC_LIST})

target_include_directories(${ENGINE_TARGET_NAME} PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CONFIG_OUTPUT}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

find_package(Vulkan REQUIRED)
target_link_system_libraries(
        ${ENGINE_TARGET_NAME}
        PRIVATE
        Vulkan::Vulkan glm fmt assimp imgui stb::image)

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
