panda_add_test(panda_system_application_startup system
    SOURCES ApplicationStartup.cpp
    LIBRARIES Panda::Tools PandaTest::Support glfw)

panda_add_test(panda_system_vulkan_context system
    SOURCES VulkanContext.cpp
    LIBRARIES Panda::Panda Vulkan::Headers)

panda_add_test(panda_system_buffer system
    SOURCES Buffer.cpp
    LIBRARIES Panda::Panda PandaTest::Support PandaTest::LogCapture)

panda_add_test(panda_system_texture system
    SOURCES Texture.cpp
    LIBRARIES Panda::Panda PandaTest::Support PandaTest::LogCapture)

panda_add_test(panda_system_mesh system
    SOURCES Mesh.cpp
    LIBRARIES Panda::Panda PandaTest::Support PandaTest::LogCapture)

panda_add_test(panda_system_upload system
    SOURCES Upload.cpp
    LIBRARIES Panda::Panda PandaTest::Support PandaTest::LogCapture)
target_include_directories(panda_system_upload PRIVATE "${PROJECT_SOURCE_DIR}/src/core/vulkan")
target_compile_definitions(panda_system_upload PRIVATE VULKAN_HPP_NO_CONSTRUCTORS VULKAN_HPP_NO_EXCEPTIONS)

panda_add_test(panda_system_memory_allocation system
    SOURCES MemoryAllocation.cpp
    LIBRARIES Panda::Panda Vulkan::Headers)
target_include_directories(panda_system_memory_allocation PRIVATE "${PROJECT_SOURCE_DIR}/src/core/vulkan"
    "${PROJECT_SOURCE_DIR}/src/core/vulkan/resources")
target_compile_definitions(panda_system_memory_allocation PRIVATE VULKAN_HPP_NO_CONSTRUCTORS VULKAN_HPP_NO_EXCEPTIONS)

panda_add_test(panda_system_windowed_context system
    SOURCES WindowedContext.cpp
    LIBRARIES Panda::Tools PandaTest::Support glfw)
