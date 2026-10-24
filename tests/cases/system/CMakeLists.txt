panda_add_test(panda_system_application_startup system
    SOURCES ApplicationStartup.cpp
    LIBRARIES Panda::Tools PandaTest::Support glfw)

panda_add_test(panda_system_vulkan_context system
    SOURCES VulkanContext.cpp
    LIBRARIES Panda::Panda Vulkan::Headers)

panda_add_test(panda_system_buffer system
    SOURCES Buffer.cpp
    LIBRARIES Panda::Panda PandaTest::Support PandaTest::LogCapture)

panda_add_test(panda_system_external_buffer system
    SOURCES ExternalBuffer.cpp
    LIBRARIES Panda::Panda)

panda_add_test(panda_system_texture system
    SOURCES Texture.cpp
    LIBRARIES Panda::Panda PandaTest::Support PandaTest::LogCapture)

panda_add_test(panda_system_mesh system
    SOURCES Mesh.cpp
    LIBRARIES Panda::Panda PandaTest::Support PandaTest::LogCapture)

panda_add_test(panda_system_upload system
    VULKAN_INTERNAL
    SOURCES Upload.cpp
    LIBRARIES Panda::Panda PandaTest::Support PandaTest::LogCapture)

panda_add_test(panda_system_memory_allocation system
    VULKAN_INTERNAL
    INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/src/core/vulkan/resources"
    SOURCES MemoryAllocation.cpp
    LIBRARIES Panda::Panda Vulkan::Headers)

panda_add_test(panda_system_windowed_context system
    SOURCES WindowedContext.cpp
    LIBRARIES Panda::Tools PandaTest::Support glfw)
