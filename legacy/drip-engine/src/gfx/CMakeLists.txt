add_library(drip_gfx STATIC)
add_library(drip::gfx ALIAS drip_gfx)

target_sources(
        drip_gfx
        PRIVATE
        src/gui/GuiManager.cpp

        src/resource/Mesh.cpp
        src/resource/MeshRenderable.cpp
        src/resource/ParticlesRenderable.cpp
        src/resource/Surface.cpp
        src/resource/Texture.cpp

        src/scene/Camera.cpp
        src/scene/Scene.cpp

        src/rendering/Renderer.cpp
        src/rendering/system/GuiRenderSystem.cpp
        src/rendering/system/MeshRenderSystem.cpp
        src/rendering/system/ParticlesRenderSystem.cpp

        src/vulkan/core/Context.cpp
        src/vulkan/core/Device.cpp
        src/vulkan/core/SwapChain.cpp

        src/vulkan/pipeline/CommandBuffer.cpp
        src/vulkan/pipeline/Pipeline.cpp
        src/vulkan/pipeline/Shader.cpp

        src/vulkan/memory/Buffer.cpp
        src/vulkan/memory/Descriptor.cpp
        src/vulkan/memory/SharedBuffer.cpp
)

target_include_directories(
        drip_gfx
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/configured_files/include>
        $<INSTALL_INTERFACE:include>
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_system_libraries(
        drip_gfx
        PRIVATE
        drip::common
        Vulkan::Vulkan
        glm::glm
        imgui::imgui)

target_compile_definitions(
        drip_gfx
        PUBLIC
        VULKAN_HPP_ASSERT_ON_RESULT=DRIP_EXPECT
        VULKAN_HPP_ASSERT=DRIP_DEBUG_EXPECT
        VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1
        VULKAN_HPP_NO_EXCEPTIONS=1
        VULKAN_HPP_NO_CONSTRUCTORS=1
        GLM_FORCE_DEPTH_ZERO_TO_ONE
        GLM_ENABLE_EXPERIMENTAL
        GLFW_INCLUDE_VULKAN
        $<$<PLATFORM_ID:Windows>:VK_USE_PLATFORM_WIN32_KHR>
        $<$<PLATFORM_ID:Linux>:VK_USE_PLATFORM_XLIB_KHR>
        PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:NOMINMAX>
)

set(ENGINE_TARGET_NAME "drip::engine")
add_subdirectory(config)
add_subdirectory(shader)
add_dependencies(drip_gfx drip_shader)

drip_library(drip_gfx)